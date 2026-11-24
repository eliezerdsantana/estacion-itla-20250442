/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "red.h"
#include "datos.h"
#include "descarga.h"

/**
 * @brief Lee la orden y ejecuta config o descargar.
 * @param argc Numero de elementos recibidos en la linea de comandos.
 * @param argv Nombre del programa y subcomando solicitado.
 * @return 0 si se completo la operacion; 1 ante un error.
 */
int main(int argc, char *argv[]) {
    const char *host = getenv("ITLA_HOST");
    const char *matricula = getenv("ITLA_MATRICULA");
    const char *token = getenv("ITLA_TOKEN");
    Conexion conexion;
    sqlite3 *base = NULL;
    char respuesta[TAM_RESPUESTA];
    long codigo_http;
    int correcto;

    if (argc != 2 || (strcmp(argv[1], "config") != 0 &&
                     strcmp(argv[1], "descargar") != 0)) {
        fputs("Uso: ./estacion config | descargar\n", stderr);
        return 1;
    }
    if (host == NULL || *host == '\0' ||
        matricula == NULL || *matricula == '\0' ||
        token == NULL || *token == '\0') {
        fputs("Define ITLA_HOST, ITLA_MATRICULA e ITLA_TOKEN en la terminal.\n",
              stderr);
        return 1;
    }
    if (!red_abrir(&conexion, host, token)) {
        return 1;
    }
    if (strcmp(argv[1], "descargar") == 0) {
        correcto = descargar(&conexion, matricula);
        red_cerrar(&conexion);
        return correcto ? 0 : 1;
    }
    if (!red_get(&conexion, "/api/v1/config", respuesta,
                 sizeof(respuesta), &codigo_http)) {
        red_cerrar(&conexion);
        return 1;
    }
    if (codigo_http != 200) {
        fprintf(stderr, "El servidor respondio HTTP %ld.\n", codigo_http);
        red_cerrar(&conexion);
        return 1;
    }
    if (!datos_abrir(&base)) {
        red_cerrar(&conexion);
        return 1;
    }
    correcto = datos_guardar_config(base, respuesta, matricula);
    if (correcto) {
        correcto = datos_mostrar_config(base);
    }
    if (!datos_cerrar(base)) {
        correcto = 0;
    }
    red_cerrar(&conexion);
    return correcto ? 0 : 1;
}
