/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef RED_H
#define RED_H

#include <stddef.h>
#include <curl/curl.h>

#define TAM_RESPUESTA 65536

typedef struct {
    CURL *manejador;
    struct curl_slist *cabeceras_json;
    char base_url[320];
    int inicializada;
} Conexion;

/* Abre la conexion HTTP usando el host y el token; devuelve 1 si funciona. */
int red_abrir(Conexion *conexion, const char *host, const char *token);

/* Hace un GET, copia su texto y obtiene el codigo HTTP. Devuelve 0 ante un
 * error de transporte; el llamador comprueba aparte el codigo HTTP. */
int red_get(Conexion *conexion, const char *ruta, char *texto,
            size_t capacidad, long *codigo_http);

/* Envia JSON y recibe la respuesta. Devuelve 1 si funciono el transporte;
 * el llamador comprueba aparte el estado HTTP y el contenido recibido. */
int red_post_json(Conexion *conexion, const char *ruta, const char *json,
                  char *texto, size_t capacidad, long *codigo_http);

/* Espera los milisegundos indicados. Devuelve 1 al completar la espera. */
int red_esperar_ms(unsigned int milisegundos);

/* Libera el manejador y las cabeceras, y termina el uso de libcurl. */
void red_cerrar(Conexion *conexion);

#endif
