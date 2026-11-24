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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

typedef struct {
    char *texto;
    size_t capacidad;
    size_t usados;
} Buffer;

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
