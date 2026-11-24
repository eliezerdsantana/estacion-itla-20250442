/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include "descarga.h"
#include "datos.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOTE_DESCARGA 200

/**
 * @brief Obtiene una linea y avanza el cursor sin perder campos CSV vacios.
 * @param cursor Direccion de la posicion actual dentro del texto modificable.
 * @return Inicio de la linea, o NULL cuando ya no queda texto.
 */
static char *siguiente_linea(char **cursor) {
    char *linea, *salto;
    size_t longitud;

    if (**cursor == '\0') {
        return NULL;
    }
    linea = *cursor;
    salto = strchr(linea, '\n');
    if (salto != NULL) {
        *salto = '\0';
        *cursor = salto + 1;
    } else {
        *cursor += strlen(linea);
    }
    longitud = strlen(linea);
    if (longitud != 0 && linea[longitud - 1] == '\r') {
        linea[longitud - 1] = '\0';
    }
    return linea;
}

/**
 * @brief Comprueba si una linea solo contiene espacios, sin modificarla.
 * @param texto Linea terminada en cero.
 * @return 1 si esta vacia; 0 si contiene algun otro caracter.
 */
static int linea_vacia(const char *texto) {
    while (isspace((unsigned char)*texto)) {
        texto++;
    }
    return *texto == '\0';
}

/**
 * @brief Separa las siete columnas, incluidas las vacias entre dos comas.
 * @param linea Fila modificable: las comas se sustituyen por ceros finales.
 * @param campos Recibe los inicios de las columnas; las ausentes quedan NULL.
 * @return 1 si hay como maximo siete columnas; 0 si la fila tiene columnas extra.
 */
static int separar_campos(char *linea, char *campos[7]) {
    char *cursor;
    int cantidad = 1;
    int i;

    for (i = 0; i < 7; i++) {
        campos[i] = NULL;
    }
    campos[0] = linea;
    for (cursor = linea; *cursor != '\0'; cursor++) {
        if (*cursor == ',') {
            if (cantidad == 7) {
                return 0;
            }
            *cursor = '\0';
            campos[cantidad++] = cursor + 1;
        }
    }
    return 1;
}

/**
 * @brief Convierte un campo completo en un entero de 64 bits.
 * @param texto Columna recibida, que puede estar vacia o ausente.
 * @param valor Recibe el numero si la conversion funciona.
 * @return 1 si el campo era un entero completo y representable; 0 si no.
 */
static int leer_entero(const char *texto, sqlite3_int64 *valor) {
    char *fin;
    long long numero;

    if (texto == NULL || *texto == '\0') {
        return 0;
    }
    errno = 0;
    numero = strtoll(texto, &fin, 10);
    if (errno != 0 || fin == texto) {
        return 0;
    }
    while (isspace((unsigned char)*fin)) {
        fin++;
    }
    if (*fin != '\0') {
        return 0;
    }
    *valor = (sqlite3_int64)numero;
    return 1;
}

/**
 * @brief Convierte un campo completo en un real finito.
 * @param texto Columna recibida, que puede estar vacia o ausente.
 * @param valor Recibe el numero si la conversion funciona.
 * @return 1 si se pudo interpretar el numero; 0 si falta o no es valido.
 */
static int leer_real(const char *texto, double *valor) {
    char *fin;
    double numero;

    if (texto == NULL || *texto == '\0') {
        return 0;
    }
    errno = 0;
    numero = strtod(texto, &fin);
    if (errno != 0 || fin == texto || !isfinite(numero)) {
        return 0;
    }
    while (isspace((unsigned char)*fin)) {
        fin++;
    }
    if (*fin != '\0') {
        return 0;
    }
    *valor = numero;
    return 1;
}

/**
 * @brief Valida la cabecera y guarda todas las filas de una pagina.
 * @param base Base de datos con la transaccion de descarga abierta.
 * @param insercion Sentencia INSERT OR IGNORE reutilizable.
 * @param csv Texto recibido que se puede modificar al separar lineas y campos.
 * @param filas Recibe el numero de filas de datos, incluidos duplicados.
 * @param nuevas Recibe el numero de filas que SQLite inserto realmente.
 * @param mayor_id Recibe el mayor ID de esta pagina.
 * @return 1 si se proceso completa; 0 si falta un ID o hay un error de formato.
 */
static int guardar_pagina(sqlite3 *base, sqlite3_stmt *insercion, char *csv,
                          int *filas, int *nuevas, sqlite3_int64 *mayor_id) {
    const char *nombres[] = {"id", "maquina", "ts", "temp", "corriente",
                            "vibracion", "estado"};
    char *cursor = csv;
    char *linea = siguiente_linea(&cursor);
    char *campos[7];
    int i;

    *filas = 0;
    *nuevas = 0;
    *mayor_id = 0;
    if (linea == NULL || !separar_campos(linea, campos)) {
        fputs("La pagina no contiene una cabecera CSV valida.\n", stderr);
        return 0;
    }
    for (i = 0; i < 7; i++) {
        if (campos[i] == NULL || strcmp(campos[i], nombres[i]) != 0) {
            fputs("La cabecera CSV no coincide con las columnas esperadas.\n", stderr);
            return 0;
        }
    }

    while ((linea = siguiente_linea(&cursor)) != NULL) {
        LecturaPlanta lectura = {0};
        int nueva;

        if (linea_vacia(linea)) {
            continue;
        }
        if (!separar_campos(linea, campos)) {
            fputs("La pagina tiene demasiadas columnas.\n", stderr);
            return 0;
        }
        if (!leer_entero(campos[0], &lectura.id) || lectura.id <= 0) {
            fputs("Una lectura no tiene un ID valido; no se puede identificar.\n",
                  stderr);
            return 0;
        }
        lectura.maquina = campos[1] != NULL && *campos[1] != '\0' ? campos[1] : NULL;
        lectura.tiene_ts = leer_entero(campos[2], &lectura.ts);
        lectura.tiene_temp = leer_real(campos[3], &lectura.temp);
        lectura.tiene_corriente = leer_real(campos[4], &lectura.corriente);
        lectura.tiene_vibracion = leer_real(campos[5], &lectura.vibracion);
        lectura.estado = campos[6] != NULL && *campos[6] != '\0' ? campos[6] : NULL;
        /* -999 y los campos NULL se conservan para contar descartadas luego. */
        if (!datos_insertar_lectura(base, insercion, &lectura, &nueva)) {
            return 0;
        }
        (*filas)++;
        *nuevas += nueva;
        if (lectura.id > *mayor_id) {
            *mayor_id = lectura.id;
        }
    }
    return 1;
}

/**
 * @brief Busca la confirmacion exacta correcto=1 en las lineas de respuesta.
 * @param texto Respuesta de la API, terminada en cero.
 * @return 1 si una linea confirma el resultado; 0 si no existe esa confirmacion.
 */
static int respuesta_confirma(const char *texto) {
    const char *linea = texto;
    while (*linea != '\0') {
        const char *fin = strchr(linea, '\n');
        size_t longitud = fin != NULL ? (size_t)(fin - linea) : strlen(linea);
        if (longitud != 0 && linea[longitud - 1] == '\r') {
            longitud--;
        }
        if (longitud == 10 && strncmp(linea, "correcto=1", 10) == 0) {
            return 1;
        }
        if (fin == NULL) {
            break;
        }
        linea = fin + 1;
    }
    return 0;
}

/**
 * @brief Descarga paginas hasta completar la instantanea y envia COUNT y SUM.
 * @param conexion Manejador HTTP abierto que se reutiliza en cada peticion.
 * @param matricula Identificador del entorno; debe coincidir con config.
 * @return 1 si la descarga y su confirmacion funcionaron; 0 ante un error.
 */
int descargar(Conexion *conexion, const char *matricula) {
    sqlite3 *base = NULL;
    sqlite3_stmt *insercion = NULL;
    char pagina[TAM_RESPUESTA];
    char ruta[160], json[160], matricula_guardada[32];
    sqlite3_int64 desde_id = 0, mayor_id, filas_unicas, suma_ids;
    long codigo_http;
    int filas, nuevas, escritos;
    int paginas = 0;
    int transaccion_abierta = 0;
    int correcto = 0;

    if (!datos_abrir(&base)) {
        return 0;
    }
    if (!datos_leer_config(base, "matricula", matricula_guardada,
                           sizeof(matricula_guardada))) {
        fputs("Ejecuta primero ./estacion config.\n", stderr);
        goto terminar;
    }
    if (strcmp(matricula_guardada, matricula) != 0) {
        fputs("La base tiene otra matricula. Ejecuta config antes de descargar.\n",
              stderr);
        goto terminar;
    }
    if (!datos_preparar_descarga(base, &insercion)) {
        goto terminar;
    }
    transaccion_abierta = 1;

    do {
        escritos = snprintf(ruta, sizeof(ruta),
            "/api/v1/lecturas?desde_id=%lld&limite=%d",
            (long long)desde_id, LOTE_DESCARGA);
        if (escritos < 0 || (size_t)escritos >= sizeof(ruta)) {
            fputs("No se pudo preparar la ruta de descarga.\n", stderr);
            goto terminar;
        }
        if (!red_get(conexion, ruta, pagina, sizeof(pagina), &codigo_http)) {
            goto terminar;
        }
        if (codigo_http != 200) {
            fprintf(stderr, "La descarga respondio HTTP %ld.\n", codigo_http);
            goto terminar;
        }
        if (!guardar_pagina(base, insercion, pagina, &filas, &nuevas, &mayor_id)) {
            goto terminar;
        }
        paginas++;
        printf("Pagina %d: %d filas recibidas, %d nuevas.\n", paginas, filas, nuevas);
        if (filas >= LOTE_DESCARGA && mayor_id <= desde_id) {
            fputs("La pagina no permite avanzar al siguiente ID.\n", stderr);
            goto terminar;
        }
        if (mayor_id > desde_id) {
            desde_id = mayor_id;
        }
        if (!red_esperar_ms(250)) {
            goto terminar;
        }
    } while (filas >= LOTE_DESCARGA);

    if (!datos_resumen_descarga(base, &filas_unicas, &suma_ids)) {
        goto terminar;
    }
    correcto = datos_confirmar_descarga(base, &insercion);
    transaccion_abierta = 0;
    if (!correcto) {
        goto terminar;
    }
    correcto = 0;
    printf("filas_unicas=%lld\nsuma_ids=%lld\n",
           (long long)filas_unicas, (long long)suma_ids);
    escritos = snprintf(json, sizeof(json),
        "{\"filas_unicas\":%lld,\"suma_ids\":%lld}",
        (long long)filas_unicas, (long long)suma_ids);
    if (escritos < 0 || (size_t)escritos >= sizeof(json)) {
        fputs("No se pudo preparar el resumen JSON.\n", stderr);
        goto terminar;
    }
    if (!red_post_json(conexion, "/api/v1/respuestas/descarga", json,
                        pagina, sizeof(pagina), &codigo_http)) {
        goto terminar;
    }
    if (codigo_http != 200) {
        fprintf(stderr, "La comprobacion respondio HTTP %ld.\n", codigo_http);
        goto terminar;
    }
    fputs(pagina, stdout);
    correcto = respuesta_confirma(pagina);
    if (!correcto) {
        fputs("El servidor no confirmo la integridad de la descarga.\n", stderr);
    }

terminar:
    if (transaccion_abierta && !datos_cancelar_descarga(base, &insercion)) {
        correcto = 0;
    }
    if (!datos_cerrar(base)) {
        correcto = 0;
    }
    return correcto;
}
