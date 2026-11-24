/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include "analisis.h"
#include "datos.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Convierte el umbral de config sin aceptar texto sobrante ni NaN.
 * @param texto Valor almacenado como texto en config.
 * @param umbral Recibe el numero convertido.
 * @return 1 si todo el texto representa un numero finito; 0 si es invalido.
 */
static int leer_umbral(const char *texto, double *umbral) {
    char *fin;
    errno = 0;
    *umbral = strtod(texto, &fin);
    if (fin == texto || errno == ERANGE || !isfinite(*umbral)) {
        return 0;
    }
    while (isspace((unsigned char)*fin)) {
        fin++;
    }
    return *fin == '\0';
}

/**
 * @brief Muestra las cuatro filas; una maquina sin validas conserva NULL.
 * @param resumen Estadisticas calculadas por SQL.
 * @return No devuelve un valor.
 */
static void mostrar_resumen(const ResumenMaquina resumen[CANT_MAQUINAS]) {
    int i;
    puts("maquina,validas,descartadas,temp_min,temp_max,temp_prom,sobre_umbral");
    for (i = 0; i < CANT_MAQUINAS; i++) {
        const ResumenMaquina *r = &resumen[i];
        printf("%s,%lld,%lld,", r->maquina, (long long)r->validas,
               (long long)r->descartadas);
        if (r->tiene_temperaturas) {
            printf("%.2f,%.2f,%.2f,", r->temp_min, r->temp_max, r->temp_prom);
        } else {
            fputs("NULL,NULL,NULL,", stdout);
        }
        printf("%lld\n", (long long)r->sobre_umbral);
    }
}

/**
 * @brief Forma el JSON del mandato sin superar el espacio disponible.
 * @param resumen Cuatro maquinas que ya tienen estadisticas validas.
 * @param json Destino del cuerpo HTTP.
 * @param capacidad Espacio total del destino.
 * @return 1 si el cuerpo quedo completo; 0 si no cabe.
 */
static int preparar_json(const ResumenMaquina resumen[CANT_MAQUINAS],
                         char *json, size_t capacidad) {
    size_t usados = 0;
    int n = snprintf(json, capacidad, "{\"maquinas\":[");
    int i;

    if (n < 0 || (size_t)n >= capacidad) {
        return 0;
    }
    usados = (size_t)n;
    for (i = 0; i < CANT_MAQUINAS; i++) {
        const ResumenMaquina *r = &resumen[i];
        n = snprintf(json + usados, capacidad - usados,
            "%s{\"maquina\":\"%s\",\"validas\":%lld,\"descartadas\":%lld,"
            "\"temp_min\":%.2f,\"temp_max\":%.2f,\"temp_prom\":%.2f,"
            "\"sobre_umbral\":%lld}",
            i == 0 ? "" : ",", r->maquina, (long long)r->validas,
            (long long)r->descartadas, r->temp_min, r->temp_max,
            r->temp_prom, (long long)r->sobre_umbral);
        if (n < 0 || (size_t)n >= capacidad - usados) {
            return 0;
        }
        usados += (size_t)n;
    }
    n = snprintf(json + usados, capacidad - usados, "]}");
    return n >= 0 && (size_t)n < capacidad - usados;
}

/**
 * @brief Exige las 24 confirmaciones distintas, con valor exactamente ok.
 * @param texto Respuesta HTTP modificable, separada por saltos de linea.
 * @return 1 si todos los campos fueron aceptados; 0 si falta o falla alguno.
 */
static int comprobar_respuesta(char *texto) {
    const char *campos[] = {"validas", "descartadas", "temp_min",
                           "temp_max", "temp_prom", "sobre_umbral"};
    int vistos[CANT_MAQUINAS][6] = {{0}};
    int aceptados = 0;
    int restantes = -1;
    char *cursor = texto;

    while (*cursor != '\0') {
        char *linea = cursor;
        char *salto = strchr(cursor, '\n');
        size_t longitud;
        int i, j, coincide = 0;
        if (salto != NULL) {
            *salto = '\0';
            cursor = salto + 1;
        } else {
            cursor += strlen(cursor);
        }
        longitud = strlen(linea);
        if (longitud != 0 && linea[longitud - 1] == '\r') {
            linea[longitud - 1] = '\0';
        }
        if (*linea == '\0' || strcmp(linea, "correcto=1") == 0) {
            continue;
        }
        if (strncmp(linea, "intentos_restantes=", 19) == 0) {
            char *fin;
            long numero;
            errno = 0;
            numero = strtol(linea + 19, &fin, 10);
            if (restantes != -1 || fin == linea + 19 || *fin != '\0' ||
                errno == ERANGE || numero < 0 || numero > 10) {
                return 0;
            }
            restantes = (int)numero;
            continue;
        }
        for (i = 0; i < CANT_MAQUINAS; i++) {
            for (j = 0; j < 6; j++) {
                char esperada[64];
                int n = snprintf(esperada, sizeof(esperada), "M%d.%s=ok",
                                 i + 1, campos[j]);
                if (n < 0 || (size_t)n >= sizeof(esperada)) {
                    return 0;
                }
                if (strcmp(linea, esperada) == 0) {
                    if (vistos[i][j]) {
                        return 0;
                    }
                    vistos[i][j] = 1;
                    aceptados++;
                    coincide = 1;
                }
            }
        }
        if (!coincide) {
            return 0;
        }
    }
    return aceptados == CANT_MAQUINAS * 6 && restantes >= 0;
}

/**
 * @brief Calcula el informe local y, solo si se pide, lo entrega al servidor.
 * @param conexion Conexion HTTP abierta; puede ser NULL si enviar vale 0.
 * @param matricula Matricula del entorno; el modo local admite NULL.
 * @param enviar Vale 1 para hacer una evaluacion; 0 para consultar sin enviar.
 * @return 1 si el informe local o la evaluacion terminaron bien; 0 ante error.
 */
int analizar(Conexion *conexion, const char *matricula, int enviar) {
    sqlite3 *base = NULL;
    ResumenMaquina resumen[CANT_MAQUINAS];
    char texto_umbral[64], matricula_guardada[32], maquinas[64];
    char json[2048], respuesta[TAM_RESPUESTA];
    sqlite3_int64 filas = 0, suma_ids = 0, clasificadas = 0;
    double umbral = 0.0;
    long codigo_http;
    int i, correcto = 0;

    if (!datos_abrir(&base)) {
        return 0;
    }
    if (!datos_leer_config(base, "matricula", matricula_guardada,
                           sizeof(matricula_guardada)) ||
        !datos_leer_config(base, "maquinas", maquinas, sizeof(maquinas)) ||
        !datos_leer_config(base, "umbral_temp", texto_umbral,
                           sizeof(texto_umbral))) {
        fputs("Primero ejecuta config y descargar.\n", stderr);
        goto cerrar_analisis;
    }
    if ((matricula != NULL && *matricula != '\0' &&
         strcmp(matricula, matricula_guardada) != 0) ||
        strcmp(maquinas, "M1,M2,M3,M4") != 0 ||
        !leer_umbral(texto_umbral, &umbral)) {
        fputs("La configuracion del analisis es invalida o de otra matricula.\n",
              stderr);
        goto cerrar_analisis;
    }
    if (!datos_analizar(base, umbral, resumen) ||
        !datos_resumen_descarga(base, &filas, &suma_ids)) {
        goto cerrar_analisis;
    }
    for (i = 0; i < CANT_MAQUINAS; i++) {
        clasificadas += resumen[i].validas + resumen[i].descartadas;
    }
    if (filas == 0 || clasificadas != filas) {
        fputs("No hay lecturas o alguna no pertenece a las maquinas configuradas.\n",
              stderr);
        goto cerrar_analisis;
    }
    correcto = 1;

cerrar_analisis:
    if (!datos_cerrar(base)) {
        correcto = 0;
    }
    if (!correcto) {
        return 0;
    }
    printf("umbral_temp=%.2f\n", umbral);
    mostrar_resumen(resumen);
    if (!enviar) {
        puts("Analisis local: no se envio una respuesta al servidor.");
        return 1;
    }
    /* Una evaluacion usa un intento: no enviar una descarga parcial. */
    if (conexion == NULL || filas != 2000) {
        fputs("Para enviar el analisis hacen falta las 2000 lecturas.\n", stderr);
        return 0;
    }
    for (i = 0; i < CANT_MAQUINAS; i++) {
        if (!resumen[i].tiene_temperaturas) {
            fputs("Una maquina no tiene temperaturas validas; no se envia.\n",
                  stderr);
            return 0;
        }
    }
    if (!preparar_json(resumen, json, sizeof(json))) {
        fputs("El JSON del analisis supera el espacio disponible.\n", stderr);
        return 0;
    }
    /* Es una sola peticion, sin reintentos automaticos que gasten intentos. */
    if (!red_post_json(conexion, "/api/v1/respuestas/analisis", json,
                       respuesta, sizeof(respuesta), &codigo_http)) {
        return 0;
    }
    printf("%s", respuesta);
    if (codigo_http != 200) {
        fprintf(stderr, "La evaluacion respondio HTTP %ld.\n", codigo_http);
        return 0;
    }
    if (!comprobar_respuesta(respuesta)) {
        fputs("El servidor no confirmo los 24 campos. Revisa la respuesta.\n",
              stderr);
        return 0;
    }
    puts("Analisis confirmado: 24 de 24 campos.");
    return 1;
}
