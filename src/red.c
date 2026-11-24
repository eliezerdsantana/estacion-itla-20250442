/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "red.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef struct {
    char *texto;
    size_t capacidad;
    size_t usados;
} Buffer;

struct RedEnvio {
    Conexion *conexion;
    CURLM *multi;
    int activo, completo;
    Buffer buffer;
    char json[512];
    ResultadoEnvio resultado;
};

/**
 * @brief Copia los fragmentos que entrega libcurl al buffer de respuesta.
 * @param datos Fragmento recibido; no tiene por que terminar en cero.
 * @param unidad Tamano de cada elemento recibido.
 * @param cantidad Numero de elementos recibidos.
 * @param usuario Buffer que contiene el destino, su capacidad y su longitud.
 * @return Bytes copiados, o 0 si el fragmento no cabe.
 */
static size_t recibir(char *datos, size_t unidad, size_t cantidad,
                      void *usuario) {
    Buffer *buffer = usuario;
    size_t bytes;

    if (unidad != 0 && cantidad > SIZE_MAX / unidad) {
        return 0;
    }
    bytes = unidad * cantidad;
    /* Reservamos siempre un byte para el cero que termina el texto. */
    if (bytes >= buffer->capacidad - buffer->usados) {
        return 0;
    }
    memcpy(buffer->texto + buffer->usados, datos, bytes);
    buffer->usados += bytes;
    buffer->texto[buffer->usados] = '\0';
    return bytes;
}

/**
 * @brief Prepara libcurl y un manejador reutilizable para las peticiones.
 * @param conexion Estructura donde se guardan el manejador y las cabeceras.
 * @param host Nombre del servidor o IPv4, sin protocolo ni puerto.
 * @param token Clave de 32 caracteres obtenida de una variable de entorno.
 * @return 1 si se pudo preparar la conexion; 0 si hubo un error.
 */
int red_abrir(Conexion *conexion, const char *host, const char *token) {
    size_t i;
    int escritos;
    CURLcode resultado;

    memset(conexion, 0, sizeof(*conexion));
    if (host[0] == '\0' || strlen(host) > 253 || strlen(token) != 32 ||
        strchr(token, '\r') != NULL || strchr(token, '\n') != NULL) {
        fputs("Host o token con formato incorrecto.\n", stderr);
        return 0;
    }
    for (i = 0; host[i] != '\0'; i++) {
        unsigned char letra = (unsigned char)host[i];
        if (!isalnum(letra) && letra != '.' && letra != '-') {
            fputs("ITLA_HOST debe contener solo el nombre o la IPv4.\n", stderr);
            return 0;
        }
    }
    escritos = snprintf(conexion->base_url, sizeof(conexion->base_url),
                        "http://%s:4000", host);
    if (escritos < 0 || (size_t)escritos >= sizeof(conexion->base_url)) {
        fputs("La direccion del servidor es demasiado larga.\n", stderr);
        return 0;
    }
    resultado = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (resultado != CURLE_OK) {
        fprintf(stderr, "No se pudo iniciar libcurl: %s\n",
                curl_easy_strerror(resultado));
        return 0;
    }
    conexion->inicializada = 1;
    conexion->manejador = curl_easy_init();
    conexion->cabeceras_json =
        curl_slist_append(NULL, "Content-Type: application/json");
    if (conexion->manejador == NULL || conexion->cabeceras_json == NULL) {
        fputs("No se pudieron reservar los recursos de red.\n", stderr);
        red_cerrar(conexion);
        return 0;
    }

    if (curl_easy_setopt(conexion->manejador, CURLOPT_XOAUTH2_BEARER,
                         token) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_HTTPAUTH,
                         (long)CURLAUTH_BEARER) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_USERAGENT,
                         "estacion-itla/1.0") != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_CONNECTTIMEOUT_MS,
                         3000L) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_TIMEOUT_MS,
                         5000L) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_NOSIGNAL, 1L) != CURLE_OK) {
        fputs("No se pudieron configurar las opciones de libcurl.\n", stderr);
        red_cerrar(conexion);
        return 0;
    }
    return 1;
}

/**
 * @brief Realiza una peticion HTTP con el manejador que ya esta abierto.
 * @param conexion Conexion preparada por red_abrir.
 * @param ruta Ruta de la API que se agrega a la URL base.
 * @param json Texto JSON para POST; NULL indica una peticion GET.
 * @param texto Destino de la respuesta, que quedara terminado en cero.
 * @param capacidad Espacio disponible en texto, incluido el cero final.
 * @param codigo_http Destino del estado HTTP, por ejemplo 200 o 401.
 * @return 1 si la transferencia y la consulta del estado funcionaron; 0 si no.
 */
static int peticion_http(Conexion *conexion, const char *ruta, const char *json,
                          char *texto, size_t capacidad, long *codigo_http) {
    char url[512];
    int escritos;
    Buffer buffer;
    CURLcode resultado, consulta;

    *codigo_http = 0;
    if (capacidad == 0) {
        fputs("El buffer de respuesta esta vacio.\n", stderr);
        return 0;
    }
    texto[0] = '\0';
    buffer.texto = texto;
    buffer.capacidad = capacidad;
    buffer.usados = 0;
    escritos = snprintf(url, sizeof(url), "%s%s", conexion->base_url, ruta);
    if (escritos < 0 || (size_t)escritos >= sizeof(url)) {
        fputs("La URL de la peticion es demasiado larga.\n", stderr);
        return 0;
    }
    if (curl_easy_setopt(conexion->manejador, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_WRITEFUNCTION,
                         recibir) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_WRITEDATA,
                         &buffer) != CURLE_OK) {
        fputs("No se pudo preparar la peticion HTTP.\n", stderr);
        return 0;
    }
    if (json == NULL) {
        if (curl_easy_setopt(conexion->manejador, CURLOPT_HTTPHEADER,
                             (struct curl_slist *)NULL) != CURLE_OK ||
            curl_easy_setopt(conexion->manejador, CURLOPT_POSTFIELDS,
                             (char *)NULL) != CURLE_OK ||
            curl_easy_setopt(conexion->manejador, CURLOPT_POSTFIELDSIZE,
                             0L) != CURLE_OK ||
            curl_easy_setopt(conexion->manejador, CURLOPT_HTTPGET,
                             1L) != CURLE_OK) {
            fputs("No se pudo configurar GET.\n", stderr);
            return 0;
        }
    } else {
        if (strlen(json) > (size_t)LONG_MAX) {
            fputs("El JSON supera el tamano admitido.\n", stderr);
            return 0;
        }
        if (curl_easy_setopt(conexion->manejador, CURLOPT_HTTPHEADER,
                             conexion->cabeceras_json) != CURLE_OK ||
            curl_easy_setopt(conexion->manejador, CURLOPT_POSTFIELDSIZE,
                             (long)strlen(json)) != CURLE_OK ||
            curl_easy_setopt(conexion->manejador, CURLOPT_POSTFIELDS,
                             json) != CURLE_OK) {
            fputs("No se pudo configurar POST.\n", stderr);
            return 0;
        }
    }

    resultado = curl_easy_perform(conexion->manejador);
    consulta = curl_easy_getinfo(conexion->manejador, CURLINFO_RESPONSE_CODE,
                                codigo_http);
    /* Un fallo de red y una respuesta HTTP de error son casos diferentes. */
    if (resultado != CURLE_OK) {
        fprintf(stderr, "Fallo de transferencia: %s\n",
                curl_easy_strerror(resultado));
        return 0;
    }
    if (consulta != CURLE_OK) {
        fprintf(stderr, "No se pudo consultar el estado HTTP: %s\n",
                curl_easy_strerror(consulta));
        return 0;
    }
    return 1;
}

/**
 * @brief Solicita una ruta sin enviar un cuerpo HTTP.
 * @param conexion Conexion abierta; ruta indica el recurso solicitado.
 * @param ruta Ruta de la API, comenzando por una barra.
 * @param texto Buffer de salida; capacidad incluye el cero final.
 * @param capacidad Tamano disponible en texto.
 * @param codigo_http Recibe el estado HTTP de la respuesta.
 * @return 1 si funciono el transporte; 0 si ocurrio un error.
 */
int red_get(Conexion *conexion, const char *ruta, char *texto,
            size_t capacidad, long *codigo_http) {
    return peticion_http(conexion, ruta, NULL, texto, capacidad, codigo_http);
}

/**
 * @brief Envia un cuerpo JSON y obtiene el texto de la respuesta.
 * @param conexion Conexion preparada por red_abrir.
 * @param ruta Ruta de destino de la API.
 * @param json Cuerpo que permanece valido hasta completar la peticion.
 * @param texto Buffer de salida.
 * @param capacidad Tamano de texto, incluido su cero final.
 * @param codigo_http Destino del estado HTTP.
 * @return 1 si funciono el transporte; 0 en caso contrario.
 */
int red_post_json(Conexion *conexion, const char *ruta, const char *json,
                  char *texto, size_t capacidad, long *codigo_http) {
    return peticion_http(conexion, ruta, json, texto, capacidad, codigo_http);
}

/**
 * @brief Introduce una pausa entre peticiones para respetar el ritmo de la API.
 * @param milisegundos Duracion de la pausa.
 * @return 1 si completo la espera; 0 si el sistema indico un error.
 */
int red_esperar_ms(unsigned int milisegundos) {
#ifdef _WIN32
    Sleep((DWORD)milisegundos);
    return 1;
#else
    struct timespec tiempo;
    tiempo.tv_sec = (time_t)(milisegundos / 1000u);
    tiempo.tv_nsec = (long)(milisegundos % 1000u) * 1000000L;
    while (nanosleep(&tiempo, &tiempo) != 0) {
        if (errno != EINTR) {
            fputs("No se pudo completar la pausa de red.\n", stderr);
            return 0;
        }
    }
    return 1;
#endif
}

/**
 * @brief Libera todos los recursos que pertenecen a la conexion.
 * @param conexion Conexion que se va a cerrar; queda vacia al terminar.
 * @return No devuelve un valor.
 */
void red_cerrar(Conexion *conexion) {
    if (conexion->manejador != NULL) {
        curl_easy_cleanup(conexion->manejador);
    }
    curl_slist_free_all(conexion->cabeceras_json);
    if (conexion->inicializada) {
        curl_global_cleanup();
    }
    memset(conexion, 0, sizeof(*conexion));
}

/**
 * @brief Lee Retry-After sin suponer que las cabeceras terminan en cero.
 * @param datos Linea completa entregada por libcurl.
 * @param unidad Tamano de cada elemento recibido.
 * @param cantidad Numero de elementos recibidos.
 * @param usuario Estado del envio que recibe la espera indicada.
 * @return Bytes procesados; 0 si el tamano no se puede representar.
 */
static size_t recibir_cabecera(char *datos, size_t unidad, size_t cantidad,
                               void *usuario) {
    RedEnvio *envio = usuario;
    const char nombre[] = "retry-after:";
    size_t n, i, inicio, fin;
    char valor[128], *resto;
    unsigned long segundos;
    time_t fecha, ahora;

    if (unidad != 0 && cantidad > SIZE_MAX / unidad) {
        return 0;
    }
    n = unidad * cantidad;
    /* Una nueva respuesta, por ejemplo tras un 100, reinicia sus cabeceras. */
    if (n >= 5 && memcmp(datos, "HTTP/", 5) == 0) {
        envio->resultado.tiene_retry_after = 0;
    }
    if (n < sizeof(nombre) - 1) {
        return n;
    }
    for (i = 0; i < sizeof(nombre) - 1; i++) {
        if (tolower((unsigned char)datos[i]) != nombre[i]) {
            return n;
        }
    }
    inicio = sizeof(nombre) - 1;
    fin = n;
    while (inicio < fin && isspace((unsigned char)datos[inicio])) {
        inicio++;
    }
    while (fin > inicio && isspace((unsigned char)datos[fin - 1])) {
        fin--;
    }
    if (fin == inicio || fin - inicio >= sizeof(valor)) {
        return n;
    }
    memcpy(valor, datos + inicio, fin - inicio);
    valor[fin - inicio] = '\0';
    errno = 0;
    segundos = strtoul(valor, &resto, 10);
    if (isdigit((unsigned char)valor[0]) && *resto == '\0' && errno != ERANGE) {
        envio->resultado.retry_after_s = (double)segundos;
        envio->resultado.tiene_retry_after = 1;
        return n;
    }
    fecha = curl_getdate(valor, NULL);
    ahora = time(NULL);
    if (fecha != (time_t)-1 && ahora != (time_t)-1) {
        double diferencia = difftime(fecha, ahora);
        envio->resultado.retry_after_s = diferencia > 0.0 ? diferencia : 0.0;
        envio->resultado.tiene_retry_after = 1;
    }
    return n;
}

/**
 * @brief Reserva la interfaz multi; la conexion sigue perteneciendo al llamador.
 * @param conexion Conexion abierta, sin otra peticion HTTP en curso.
 * @return Estado reservado, o NULL si no pudo prepararse.
 */
RedEnvio *red_envio_abrir(Conexion *conexion) {
    RedEnvio *envio = calloc(1, sizeof(*envio));
    if (envio == NULL) {
        fputs("No hay memoria para preparar el envio.\n", stderr);
        return NULL;
    }
    envio->conexion = conexion;
    envio->multi = curl_multi_init();
    if (envio->multi == NULL) {
        free(envio);
        fputs("No se pudo iniciar la interfaz multi.\n", stderr);
        return NULL;
    }
    return envio;
}

/**
 * @brief Prepara un POST cuyo texto permanece vivo hasta completar el envio.
 * @param envio Estado reservado por red_envio_abrir.
 * @param ruta Ruta HTTP de destino.
 * @param json Cuerpo que se copia al estado, sin conservar punteros temporales.
 * @return 1 si quedo preparada; 0 ante un error.
 */
int red_envio_iniciar(RedEnvio *envio, const char *ruta, const char *json) {
    CURL *manejador = envio->conexion->manejador;
    char url[512];
    int n;

    if (red_envio_ocupado(envio) || strlen(json) >= sizeof(envio->json)) {
        fputs("El envio anterior sigue ocupado o el JSON no cabe.\n", stderr);
        return 0;
    }
    n = snprintf(url, sizeof(url), "%s%s", envio->conexion->base_url, ruta);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return 0;
    }
    memcpy(envio->json, json, strlen(json) + 1);
    memset(&envio->resultado, 0, sizeof(envio->resultado));
    envio->buffer.texto = envio->resultado.texto;
    envio->buffer.capacidad = sizeof(envio->resultado.texto);
    envio->buffer.usados = 0;
    if (curl_easy_setopt(manejador, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_WRITEFUNCTION, recibir) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_WRITEDATA, &envio->buffer) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_HEADERFUNCTION,
                         recibir_cabecera) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_HEADERDATA, envio) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_HTTPHEADER,
                         envio->conexion->cabeceras_json) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_POSTFIELDSIZE,
                         (long)strlen(envio->json)) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_POSTFIELDS,
                         envio->json) != CURLE_OK) {
        fputs("No se pudo preparar el POST asincrono.\n", stderr);
        return 0;
    }
    if (curl_multi_add_handle(envio->multi, manejador) != CURLM_OK) {
        fputs("No se pudo agregar la peticion a multi.\n", stderr);
        return 0;
    }
    envio->activo = 1;
    return 1;
}

/**
 * @brief Indica si queda una transferencia o un resultado sin recoger.
 * @param envio Estado consultado.
 * @return 1 si esta ocupado; 0 si puede iniciarse otra peticion.
 */
int red_envio_ocupado(const RedEnvio *envio) {
    return envio->activo || envio->completo;
}

/**
 * @brief Avanza la red con una espera corta, incluso cuando no hay conexiones.
 * @param envio Estado que conserva el manejador HTTP reutilizable.
 * @param espera_ms Espera maxima, entre 0 y 200 milisegundos.
 * @return 1 si multi funciona; 0 si ocurre un fallo interno.
 */
int red_envio_avanzar(RedEnvio *envio, int espera_ms) {
    int activos, pendientes;
    CURLMsg *mensaje;
    CURLMcode resultado;

    if (espera_ms < 0 || espera_ms > 200) {
        return 0;
    }
    resultado = curl_multi_perform(envio->multi, &activos);
    if (resultado == CURLM_OK) {
        resultado = curl_multi_poll(envio->multi, NULL, 0, espera_ms, NULL);
    }
    if (resultado == CURLM_OK) {
        resultado = curl_multi_perform(envio->multi, &activos);
    }
    if (resultado != CURLM_OK) {
        fprintf(stderr, "Fallo interno de multi: %s\n",
                curl_multi_strerror(resultado));
        return 0;
    }
    while ((mensaje = curl_multi_info_read(envio->multi, &pendientes)) != NULL) {
        if (mensaje->msg == CURLMSG_DONE &&
            mensaje->easy_handle == envio->conexion->manejador) {
            CURLcode consulta = curl_easy_getinfo(mensaje->easy_handle,
                CURLINFO_RESPONSE_CODE, &envio->resultado.codigo_http);
            envio->resultado.transporte_correcto =
                mensaje->data.result == CURLE_OK && consulta == CURLE_OK;
            if (mensaje->data.result != CURLE_OK) {
                fprintf(stderr, "Fallo de transferencia: %s\n",
                        curl_easy_strerror(mensaje->data.result));
            }
            if (consulta != CURLE_OK) {
                fputs("No se pudo leer el estado HTTP.\n", stderr);
            }
            if (curl_multi_remove_handle(envio->multi,
                                         mensaje->easy_handle) != CURLM_OK) {
                return 0;
            }
            envio->activo = 0;
            envio->completo = 1;
        }
    }
    return 1;
}

/**
 * @brief Entrega una respuesta completada una sola vez.
 * @param envio Estado de la red.
 * @param resultado Recibe una copia del resultado.
 * @return 1 si habia respuesta; 0 si la peticion no termino.
 */
int red_envio_recoger(RedEnvio *envio, ResultadoEnvio *resultado) {
    if (!envio->completo) {
        return 0;
    }
    *resultado = envio->resultado;
    envio->completo = 0;
    return 1;
}

/**
 * @brief Cancela la transferencia pendiente y libera multi sin cerrar conexion.
 * @param envio Estado que se libera; admite NULL.
 * @return 1 si libero los recursos; 0 si libcurl indico un error.
 */
int red_envio_cerrar(RedEnvio *envio) {
    CURL *manejador;
    int correcto = 1;
    if (envio == NULL) {
        return 1;
    }
    manejador = envio->conexion->manejador;
    if (envio->activo &&
        curl_multi_remove_handle(envio->multi, manejador) != CURLM_OK) {
        correcto = 0;
    }
    if (curl_multi_cleanup(envio->multi) != CURLM_OK) {
        correcto = 0;
    }
    /* El easy no conserva punteros a los buffers que vamos a liberar. */
    if (curl_easy_setopt(manejador, CURLOPT_HEADERDATA, NULL) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_HEADERFUNCTION, NULL) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_WRITEDATA, NULL) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_WRITEFUNCTION, NULL) != CURLE_OK ||
        curl_easy_setopt(manejador, CURLOPT_POSTFIELDS, NULL) != CURLE_OK) {
        correcto = 0;
    }
    free(envio);
    if (!correcto) {
        fputs("No se pudieron liberar todos los recursos de envio.\n", stderr);
    }
    return correcto;
}
