/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Repositorio: pendiente de publicar.
 */
#include "red.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    char autorizacion[128];
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
    escritos = snprintf(autorizacion, sizeof(autorizacion),
                        "Authorization: Bearer %s", token);
    if (escritos < 0 || (size_t)escritos >= sizeof(autorizacion)) {
        fputs("No se pudo preparar la cabecera de autenticacion.\n", stderr);
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
    conexion->cabeceras = curl_slist_append(NULL, autorizacion);
    if (conexion->manejador == NULL || conexion->cabeceras == NULL) {
        fputs("No se pudieron reservar los recursos de red.\n", stderr);
        red_cerrar(conexion);
        return 0;
    }

    if (curl_easy_setopt(conexion->manejador, CURLOPT_HTTPHEADER,
                         conexion->cabeceras) != CURLE_OK ||
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
 * @brief Descarga texto mediante GET con el manejador que ya esta abierto.
 * @param conexion Conexion preparada por red_abrir.
 * @param ruta Ruta de la API que se agrega a la URL base.
 * @param texto Destino de la respuesta, que quedara terminado en cero.
 * @param capacidad Espacio disponible en texto, incluido el cero final.
 * @param codigo_http Destino del estado HTTP, por ejemplo 200 o 401.
 * @return 1 si la transferencia y la consulta del estado funcionaron; 0 si no.
 */
int red_get(Conexion *conexion, const char *ruta, char *texto,
            size_t capacidad, long *codigo_http) {
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
        curl_easy_setopt(conexion->manejador, CURLOPT_HTTPGET, 1L) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_WRITEFUNCTION,
                         recibir) != CURLE_OK ||
        curl_easy_setopt(conexion->manejador, CURLOPT_WRITEDATA,
                         &buffer) != CURLE_OK) {
        fputs("No se pudo preparar la peticion GET.\n", stderr);
        return 0;
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
 * @brief Libera todos los recursos que pertenecen a la conexion.
 * @param conexion Conexion que se va a cerrar; queda vacia al terminar.
 * @return No devuelve un valor.
 */
void red_cerrar(Conexion *conexion) {
    if (conexion->manejador != NULL) {
        curl_easy_cleanup(conexion->manejador);
    }
    curl_slist_free_all(conexion->cabeceras);
    if (conexion->inicializada) {
        curl_global_cleanup();
    }
    memset(conexion, 0, sizeof(*conexion));
}
