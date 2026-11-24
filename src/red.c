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
    CURL *latido;
    int latido_activo, latido_completo, latido_correcto;
    char texto_latido[100];
    CURL *suscripcion;
    int suscripcion_activa, suscripcion_completa, suscripcion_correcta;
    unsigned char rx[4096];
    size_t rx_usados;
    char topico_alarmas[128];
    AlRecibirAlarma recibir_alarma;
    void *usuario_alarma;
    const volatile sig_atomic_t *detener;
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
 * @brief Muestra tiempos numericos del latido fallido sin imprimir credenciales.
 * @param latido Manejador de la publicacion que acaba de terminar.
 * @param fallo Error de transporte informado por libcurl.
 * @return No devuelve un valor; un fallo al consultar tiempos no cambia el envio.
 */
static void mostrar_fallo_latido(CURL *latido, CURLcode fallo) {
    curl_off_t total = 0, dns = 0, conexion = 0;
    CURLcode r_total = curl_easy_getinfo(latido, CURLINFO_TOTAL_TIME_T, &total);
    CURLcode r_dns = curl_easy_getinfo(latido, CURLINFO_NAMELOOKUP_TIME_T, &dns);
    CURLcode r_conexion = curl_easy_getinfo(latido, CURLINFO_CONNECT_TIME_T,
                                           &conexion);
    fprintf(stderr, "Fallo de latido MQTT: %s (curl=%d",
            curl_easy_strerror(fallo), (int)fallo);
    if (r_total == CURLE_OK && r_dns == CURLE_OK && r_conexion == CURLE_OK) {
        fprintf(stderr, " total_ms=%.3f dns_ms=%.3f conexion_ms=%.3f",
                (double)total / 1000.0, (double)dns / 1000.0,
                (double)conexion / 1000.0);
    } else {
        fputs(" tiempos_no_disponibles=1", stderr);
    }
    fputs(").\n", stderr);
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
        } else if (mensaje->msg == CURLMSG_DONE &&
                   mensaje->easy_handle == envio->latido) {
            CURLcode estado = mensaje->data.result;
            if (estado != CURLE_OK) {
                mostrar_fallo_latido(envio->latido, estado);
            }
            if (curl_multi_remove_handle(envio->multi, envio->latido) != CURLM_OK) {
                fputs("No se pudo retirar la publicacion MQTT.\n", stderr);
                return 0;
            }
            envio->latido_activo = 0;
            envio->latido_completo = 1;
            envio->latido_correcto = estado == CURLE_OK;
        } else if (mensaje->msg == CURLMSG_DONE &&
                   mensaje->easy_handle == envio->suscripcion) {
            CURLcode estado = mensaje->data.result;
            if (curl_multi_remove_handle(envio->multi,
                                         envio->suscripcion) != CURLM_OK) {
                fputs("No se pudo retirar la suscripcion MQTT.\n", stderr);
                return 0;
            }
            envio->suscripcion_activa = 0;
            envio->suscripcion_completa = 1;
            envio->suscripcion_correcta = estado == CURLE_OK;
            if (estado != CURLE_OK &&
                (envio->detener == NULL || !*envio->detener)) {
                fprintf(stderr, "Fallo de suscripcion MQTT: %s\n",
                        curl_easy_strerror(estado));
            }
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
    if (envio->latido_activo &&
        curl_multi_remove_handle(envio->multi, envio->latido) != CURLM_OK) {
        correcto = 0;
    }
    if (envio->latido != NULL) {
        curl_easy_cleanup(envio->latido);
    }
    if (envio->suscripcion_activa &&
        curl_multi_remove_handle(envio->multi, envio->suscripcion) != CURLM_OK) {
        correcto = 0;
    }
    if (envio->suscripcion != NULL) {
        curl_easy_cleanup(envio->suscripcion);
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

/**
 * @brief Prepara un easy MQTT con credenciales del entorno.
 * @param destino Recibe el manejador; el propietario lo libera al cerrar.
 * @param host Servidor validado al abrir la conexion HTTP.
 * @param matricula Usuario MQTT; solo admite digitos.
 * @param token Clave leida del entorno, que no se agrega a la URL ni se imprime.
 * @param topico Ruta MQTT fija del programa, que ya incluye la matricula.
 * @param limite_ms Tiempo total; 0 permite una suscripcion continua.
 * @return 1 si MQTT esta disponible y configurado; 0 si no pudo prepararse.
 */
static int preparar_mqtt(CURL **destino, const char *host,
                         const char *matricula, const char *token,
                         const char *topico, long limite_ms) {
    const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
    const char *const *protocolo;
    char url[512];
    size_t i, longitud = strlen(matricula);
    int disponible = 0, n;

    for (protocolo = version != NULL ? version->protocols : NULL;
         protocolo != NULL && *protocolo != NULL; protocolo++) {
        if (strcmp(*protocolo, "mqtt") == 0) {
            disponible = 1;
        }
    }
    if (!disponible) {
        fputs("La libcurl vinculada no incluye MQTT.\n", stderr);
        return 0;
    }
    if (longitud == 0 || longitud > 31 || strlen(token) != 32) {
        fputs("Formato de credenciales MQTT incorrecto.\n", stderr);
        return 0;
    }
    for (i = 0; i < longitud; i++) {
        if (!isdigit((unsigned char)matricula[i])) {
            fputs("La matricula MQTT solo puede contener digitos.\n", stderr);
            return 0;
        }
    }
    n = snprintf(url, sizeof(url), "mqtt://%s:1883/%s", host, topico);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        fputs("La URL MQTT supera el espacio disponible.\n", stderr);
        return 0;
    }
    *destino = curl_easy_init();
    if (*destino == NULL) {
        fputs("No se pudo reservar el manejador MQTT.\n", stderr);
        return 0;
    }
    if (curl_easy_setopt(*destino, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(*destino, CURLOPT_USERNAME, matricula) != CURLE_OK ||
        curl_easy_setopt(*destino, CURLOPT_PASSWORD, token) != CURLE_OK ||
        curl_easy_setopt(*destino, CURLOPT_CONNECTTIMEOUT_MS, 3000L) != CURLE_OK ||
        curl_easy_setopt(*destino, CURLOPT_TIMEOUT_MS, limite_ms) != CURLE_OK ||
        curl_easy_setopt(*destino, CURLOPT_NOSIGNAL, 1L) != CURLE_OK) {
        fputs("No se pudo configurar MQTT.\n", stderr);
        return 0;
    }
    return 1;
}

/**
 * @brief Prepara la publicacion de latidos con un manejador independiente.
 * @param envio Estado multi.
 * @param host Servidor del entorno.
 * @param matricula Usuario MQTT.
 * @param token Clave del entorno.
 * @param periodo_s Periodo del latido, usado para limitar su tiempo en curso.
 * @return 1 si quedo preparado; 0 ante un error.
 */
int red_latido_preparar(RedEnvio *envio, const char *host,
                        const char *matricula, const char *token,
                        unsigned int periodo_s) {
    char topico[128];
    long limite_ms, conexion_ms;
    int n;
    if (periodo_s == 0) {
        fputs("El periodo del latido debe ser positivo.\n", stderr);
        return 0;
    }
    /* Cada intento dispone de hasta el 80% de su turno, con tope de 8 s.
     * El margen restante permite recogerlo antes de preparar el siguiente.
     * Comparar primero evita multiplicar periodos grandes fuera de long. */
    limite_ms = periodo_s >= 10 ? 8000L : (long)periodo_s * 800L;
    conexion_ms = limite_ms < 5000L ? limite_ms : 5000L;
    n = snprintf(topico, sizeof(topico),
                 "itla/estudiantes/%s/latido", matricula);
    if (n < 0 || (size_t)n >= sizeof(topico) ||
        !preparar_mqtt(&envio->latido, host, matricula, token, topico, limite_ms)) {
        return 0;
    }
    if (curl_easy_setopt(envio->latido, CURLOPT_CONNECTTIMEOUT_MS,
                         conexion_ms) != CURLE_OK ||
        curl_easy_setopt(envio->latido, CURLOPT_FORBID_REUSE, 1L) != CURLE_OK) {
        fputs("No se pudieron configurar los limites del latido.\n", stderr);
        return 0;
    }
    return 1;
}

/**
 * @brief Indica si la publicacion anterior sigue activa o falta recogerla.
 * @param envio Estado consultado.
 * @return 1 si esta ocupado; 0 si puede publicar un latido nuevo.
 */
int red_latido_ocupado(const RedEnvio *envio) {
    return envio->latido_activo || envio->latido_completo;
}

/**
 * @brief Conserva el texto mientras libcurl publica sin bloquear.
 * @param envio Estado con MQTT ya configurado.
 * @param texto Linea LATIDO terminada en salto de linea, menor de 100 bytes.
 * @return 1 si pudo comenzar; 0 si no esta preparado o el texto no cabe.
 */
int red_latido_iniciar(RedEnvio *envio, const char *texto) {
    size_t longitud = strlen(texto);
    if (envio->latido == NULL || red_latido_ocupado(envio) ||
        longitud == 0 || longitud >= sizeof(envio->texto_latido) ||
        texto[longitud - 1] != '\n') {
        fputs("No se pudo preparar el texto del latido.\n", stderr);
        return 0;
    }
    memcpy(envio->texto_latido, texto, longitud + 1);
    if (curl_easy_setopt(envio->latido, CURLOPT_POSTFIELDSIZE,
                         (long)longitud) != CURLE_OK ||
        curl_easy_setopt(envio->latido, CURLOPT_POSTFIELDS,
                         envio->texto_latido) != CURLE_OK ||
        curl_multi_add_handle(envio->multi, envio->latido) != CURLM_OK) {
        fputs("No se pudo iniciar la publicacion MQTT.\n", stderr);
        return 0;
    }
    envio->latido_activo = 1;
    return 1;
}

/**
 * @brief Recoge el resultado local del envio; QoS 0 no entrega un PUBACK.
 * @param envio Estado MQTT.
 * @param correcto Recibe 1 si libcurl termino sin error; 0 si el latido se perdio.
 * @return 1 si habia un resultado nuevo; 0 si la publicacion no termino.
 */
int red_latido_recoger(RedEnvio *envio, int *correcto) {
    if (!envio->latido_completo) {
        return 0;
    }
    *correcto = envio->latido_correcto;
    envio->latido_completo = 0;
    return 1;
}

/**
 * @brief Extrae todos los mensajes completos del acumulador MQTT.
 * @param envio Acumulador y funcion receptora de la aplicacion.
 * @return 1 si los datos son validos o falta un fragmento; 0 si debe reconectar.
 */
static int procesar_fragmentos(RedEnvio *envio) {
    while (envio->rx_usados >= 2) {
        size_t topico_n = ((size_t)envio->rx[0] << 8) | envio->rx[1];
        unsigned char *carga, *fin;
        size_t cabecera, longitud, consumidos;
        char texto[100];

        if (topico_n == 0 || topico_n >= sizeof(envio->topico_alarmas)) {
            fputs("Longitud de topico MQTT invalida; se reconectara.\n", stderr);
            return 0;
        }
        cabecera = 2 + topico_n;
        if (envio->rx_usados < cabecera) {
            return 1;
        }
        carga = envio->rx + cabecera;
        fin = memchr(carga, '\n', envio->rx_usados - cabecera);
        if (fin == NULL) {
            if (envio->rx_usados - cabecera >= sizeof(texto) - 1) {
                fputs("Mensaje MQTT excesivo; se reconectara.\n", stderr);
                return 0;
            }
            return 1;
        }
        longitud = (size_t)(fin - carga);
        if (longitud >= sizeof(texto) - 1 ||
            memchr(carga, '\0', longitud) != NULL) {
            fputs("Mensaje MQTT con formato invalido; se reconectara.\n", stderr);
            return 0;
        }
        consumidos = cabecera + longitud + 1;
        if (topico_n == strlen(envio->topico_alarmas) &&
            memcmp(envio->rx + 2, envio->topico_alarmas, topico_n) == 0) {
            memcpy(texto, carga, longitud);
            texto[longitud] = '\0';
            if (longitud != 0 && texto[longitud - 1] == '\r') {
                texto[longitud - 1] = '\0';
            }
            if (!envio->recibir_alarma(texto, envio->usuario_alarma)) {
                return 0;
            }
        }
        memmove(envio->rx, envio->rx + consumidos,
                envio->rx_usados - consumidos);
        envio->rx_usados -= consumidos;
    }
    return 1;
}

/**
 * @brief Acumula fragmentos sin asumir que una llamada contiene un mensaje.
 * @param datos Bytes entregados por libcurl, sin terminador obligatorio.
 * @param unidad Tamano de cada elemento.
 * @param cantidad Numero de elementos.
 * @param usuario Estado de la suscripcion.
 * @return Bytes procesados, o 0 si hay un mensaje invalido o un fallo local.
 */
static size_t recibir_alarmas(char *datos, size_t unidad, size_t cantidad,
                              void *usuario) {
    RedEnvio *envio = usuario;
    size_t total, usados = 0;
    if (unidad != 0 && cantidad > SIZE_MAX / unidad) {
        return 0;
    }
    total = unidad * cantidad;
    while (usados < total) {
        size_t libres = sizeof(envio->rx) - envio->rx_usados;
        size_t copiar = total - usados;
        if (libres == 0) {
            return 0;
        }
        if (copiar > libres) {
            copiar = libres;
        }
        memcpy(envio->rx + envio->rx_usados, datos + usados, copiar);
        envio->rx_usados += copiar;
        usados += copiar;
        if (!procesar_fragmentos(envio)) {
            return 0;
        }
    }
    return total;
}

/**
 * @brief Permite abortar la suscripcion cuando el manejador marca la salida.
 * @param usuario Estado de la red.
 * @param total_bajada Bytes esperados, si se conocen.
 * @param bajados Bytes recibidos.
 * @param total_subida Bytes esperados de subida.
 * @param subidos Bytes enviados.
 * @return 1 para cancelar; 0 para continuar.
 */
static int progreso_alarmas(void *usuario, curl_off_t total_bajada,
                            curl_off_t bajados, curl_off_t total_subida,
                            curl_off_t subidos) {
    RedEnvio *envio = usuario;
    (void)total_bajada;
    (void)bajados;
    (void)total_subida;
    (void)subidos;
    return envio->detener != NULL && *envio->detener;
}

/**
 * @brief Prepara la suscripcion continua con un easy distinto del HTTP.
 * @param envio Estado multi.
 * @param host Servidor del entorno.
 * @param matricula Identidad del estudiante.
 * @param token Clave del entorno.
 * @param recibir_alarma Funcion que recibe cada texto completo.
 * @param usuario Estado que necesita la funcion receptora.
 * @param detener Bandera que solo modifica el manejador de senales.
 * @return 1 si quedo preparada; 0 ante un error.
 */
int red_alarmas_preparar(RedEnvio *envio, const char *host,
                         const char *matricula, const char *token,
                         AlRecibirAlarma recibir_alarma, void *usuario,
                         const volatile sig_atomic_t *detener) {
    int n = snprintf(envio->topico_alarmas, sizeof(envio->topico_alarmas),
                      "itla/planta/%s/alarma", matricula);
    if (n < 0 || (size_t)n >= sizeof(envio->topico_alarmas) ||
        recibir_alarma == NULL || detener == NULL ||
        !preparar_mqtt(&envio->suscripcion, host, matricula, token,
                       envio->topico_alarmas, 0L)) {
        return 0;
    }
    envio->recibir_alarma = recibir_alarma;
    envio->usuario_alarma = usuario;
    envio->detener = detener;
    if (curl_easy_setopt(envio->suscripcion, CURLOPT_HTTPGET, 1L) != CURLE_OK ||
        curl_easy_setopt(envio->suscripcion, CURLOPT_WRITEFUNCTION,
                         recibir_alarmas) != CURLE_OK ||
        curl_easy_setopt(envio->suscripcion, CURLOPT_WRITEDATA, envio) != CURLE_OK ||
        curl_easy_setopt(envio->suscripcion, CURLOPT_NOPROGRESS, 0L) != CURLE_OK ||
        curl_easy_setopt(envio->suscripcion, CURLOPT_XFERINFOFUNCTION,
                         progreso_alarmas) != CURLE_OK ||
        curl_easy_setopt(envio->suscripcion, CURLOPT_XFERINFODATA, envio) != CURLE_OK) {
        fputs("No se pudo configurar la suscripcion MQTT.\n", stderr);
        return 0;
    }
    return 1;
}

/**
 * @brief Inicia o reconecta la suscripcion, descartando fragmentos antiguos.
 * @param envio Estado preparado.
 * @return 1 si se inicio; 0 ante un error interno.
 */
int red_alarmas_iniciar(RedEnvio *envio) {
    if (envio->suscripcion == NULL || red_alarmas_ocupado(envio)) {
        return 0;
    }
    envio->rx_usados = 0;
    if (curl_multi_add_handle(envio->multi, envio->suscripcion) != CURLM_OK) {
        fputs("No se pudo iniciar la suscripcion MQTT.\n", stderr);
        return 0;
    }
    envio->suscripcion_activa = 1;
    return 1;
}

/**
 * @brief Indica si queda una conexion o un cierre sin recoger.
 * @param envio Estado consultado.
 * @return 1 si esta ocupado; 0 si puede reconectar.
 */
int red_alarmas_ocupado(const RedEnvio *envio) {
    return envio->suscripcion_activa || envio->suscripcion_completa;
}

/**
 * @brief Recoge el final de una conexion MQTT una sola vez.
 * @param envio Estado consultado.
 * @param correcto Recibe si termino sin un error de transporte.
 * @return 1 si habia un resultado; 0 si la conexion sigue activa.
 */
int red_alarmas_recoger(RedEnvio *envio, int *correcto) {
    if (!envio->suscripcion_completa) {
        return 0;
    }
    *correcto = envio->suscripcion_correcta;
    envio->suscripcion_completa = 0;
    return 1;
}
