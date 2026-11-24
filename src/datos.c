/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Repositorio: pendiente de publicar.
 */
#include "datos.h"

#include <stdio.h>
#include <string.h>

#define TAM_CONFIG 8192

/**
 * @brief Ejecuta exclusivamente instrucciones SQL fijas del programa.
 * @param base Base de datos abierta.
 * @param sql Instruccion fija, sin concatenar valores recibidos del usuario.
 * @return 1 si SQLite devuelve SQLITE_OK; 0 si ocurre un error.
 */
static int ejecutar_sql(sqlite3 *base, const char *sql) {
    char *mensaje = NULL;
    int resultado = sqlite3_exec(base, sql, NULL, NULL, &mensaje);

    if (resultado != SQLITE_OK) {
        fprintf(stderr, "Error de SQLite: %s\n",
                mensaje != NULL ? mensaje : sqlite3_errmsg(base));
    }
    sqlite3_free(mensaje);
    return resultado == SQLITE_OK;
}

/**
 * @brief Cierra la base cuando ya no quedan sentencias preparadas activas.
 * @param base Base abierta, o NULL si no se llego a abrir.
 * @return 1 si la base se cerro; 0 si SQLite indico un error.
 */
int datos_cerrar(sqlite3 *base) {
    if (base == NULL) {
        return 1;
    }
    if (sqlite3_close(base) != SQLITE_OK) {
        fprintf(stderr, "No se pudo cerrar SQLite: %s\n", sqlite3_errmsg(base));
        return 0;
    }
    return 1;
}

/**
 * @brief Abre el archivo compartido y prepara las cuatro tablas del mandato.
 * @param base Recibe el manejador abierto; queda NULL si falla la apertura.
 * @return 1 si la apertura y la preparacion funcionaron; 0 si hubo un error.
 */
int datos_abrir(sqlite3 **base) {
    const char *esquema =
        "CREATE TABLE IF NOT EXISTS config ("
        "clave TEXT PRIMARY KEY NOT NULL, valor TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS lecturas_planta ("
        "id INTEGER PRIMARY KEY, maquina TEXT, ts INTEGER, temp REAL, "
        "corriente REAL, vibracion REAL, estado TEXT);"
        "CREATE TABLE IF NOT EXISTS telemetria ("
        "seq INTEGER PRIMARY KEY, ts INTEGER NOT NULL, temp REAL NOT NULL, "
        "corriente REAL NOT NULL, enviado INTEGER NOT NULL DEFAULT 0 "
        "CHECK (enviado IN (0,1)), intentos INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS alarmas ("
        "codigo TEXT PRIMARY KEY NOT NULL, maquina TEXT NOT NULL, "
        "ts_servidor INTEGER NOT NULL, ts_recibida INTEGER NOT NULL, "
        "ts_ack INTEGER);";

    *base = NULL;
    if (sqlite3_open("estacion.db", base) != SQLITE_OK) {
        fprintf(stderr, "No se pudo abrir estacion.db: %s\n",
                *base != NULL ? sqlite3_errmsg(*base) : "sin memoria");
        if (datos_cerrar(*base)) {
            *base = NULL;
        }
        return 0;
    }
    if (sqlite3_busy_timeout(*base, 2000) != SQLITE_OK ||
        !ejecutar_sql(*base, "PRAGMA journal_mode=WAL;") ||
        !ejecutar_sql(*base, esquema)) {
        if (datos_cerrar(*base)) {
            *base = NULL;
        }
        return 0;
    }
    return 1;
}

/**
 * @brief Valida la configuracion y la guarda completa usando prepare/bind/step.
 * @param base Base de datos abierta.
 * @param texto Respuesta de config, con una pareja clave=valor por linea.
 * @param matricula Identificador leido de ITLA_MATRICULA.
 * @return 1 si se confirmo la transaccion; 0 si hubo un error.
 */
int datos_guardar_config(sqlite3 *base, const char *texto,
                         const char *matricula) {
    const char *claves[] = {"matricula", "hora", "umbral_temp", "intervalo_s",
                           "latido_s", "maquinas", "lote_max"};
    const char *sql =
        "INSERT INTO config (clave, valor) VALUES (?, ?) "
        "ON CONFLICT(clave) DO UPDATE SET valor=excluded.valor;";
    char copia[TAM_CONFIG];
    char *cursor;
    sqlite3_stmt *sentencia = NULL;
    int vistos[7] = {0};
    int recibidos = 0;
    int correcto = 0;

    if (strlen(texto) >= sizeof(copia)) {
        fputs("La configuracion supera el espacio disponible.\n", stderr);
        return 0;
    }
    memcpy(copia, texto, strlen(texto) + 1);
    cursor = copia;
    if (!ejecutar_sql(base, "BEGIN IMMEDIATE;")) {
        return 0;
    }
    if (sqlite3_prepare_v2(base, sql, -1, &sentencia, NULL) != SQLITE_OK) {
        fprintf(stderr, "No se pudo preparar config: %s\n", sqlite3_errmsg(base));
        goto terminar;
    }

    while (*cursor != '\0') {
        char *linea = cursor;
        char *salto = strchr(cursor, '\n');
        char *igual, *valor;
        size_t longitud;
        int indice;

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
        if (*linea == '\0') {
            continue;
        }
        igual = strchr(linea, '=');
        if (igual == NULL || igual == linea || igual[1] == '\0') {
            fputs("Linea de configuracion incompleta.\n", stderr);
            goto terminar;
        }
        *igual = '\0';
        valor = igual + 1;
        for (indice = 0; indice < 7; indice++) {
            if (strcmp(linea, claves[indice]) == 0) {
                break;
            }
        }
        if (indice == 7 || vistos[indice]) {
            fputs("Clave de configuracion desconocida o repetida.\n", stderr);
            goto terminar;
        }
        if (indice == 0 && strcmp(valor, matricula) != 0) {
            fputs("La configuracion pertenece a otra matricula.\n", stderr);
            goto terminar;
        }
        if (sqlite3_bind_text(sentencia, 1, linea, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(sentencia, 2, valor, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_step(sentencia) != SQLITE_DONE ||
            sqlite3_reset(sentencia) != SQLITE_OK ||
            sqlite3_clear_bindings(sentencia) != SQLITE_OK) {
            fprintf(stderr, "No se pudo guardar config: %s\n", sqlite3_errmsg(base));
            goto terminar;
        }
        vistos[indice] = 1;
        recibidos++;
    }
    /* Cada clave se cuenta una sola vez; deben llegar las siete. */
    if (recibidos != 7) {
        fputs("Faltan campos de configuracion.\n", stderr);
        goto terminar;
    }
    correcto = 1;

terminar:
    if (sqlite3_finalize(sentencia) != SQLITE_OK) {
        fputs("No se pudo finalizar la sentencia de config.\n", stderr);
        correcto = 0;
    }
    if (correcto && ejecutar_sql(base, "COMMIT;")) {
        return 1;
    }
    if (!ejecutar_sql(base, "ROLLBACK;")) {
        fputs("No se pudo revertir la transaccion.\n", stderr);
    }
    return 0;
}

/**
 * @brief Imprime las parejas clave=valor consultadas de la base de datos.
 * @param base Base que contiene la configuracion ya guardada.
 * @return 1 si la consulta termino sin errores; 0 en caso contrario.
 */
int datos_mostrar_config(sqlite3 *base) {
    sqlite3_stmt *sentencia = NULL;
    int resultado, correcto;

    if (sqlite3_prepare_v2(base, "SELECT clave, valor FROM config ORDER BY clave;",
                           -1, &sentencia, NULL) != SQLITE_OK) {
        fprintf(stderr, "No se pudo consultar config: %s\n", sqlite3_errmsg(base));
        if (sqlite3_finalize(sentencia) != SQLITE_OK) {
            fputs("No se pudo liberar la consulta.\n", stderr);
        }
        return 0;
    }
    while ((resultado = sqlite3_step(sentencia)) == SQLITE_ROW) {
        const unsigned char *clave = sqlite3_column_text(sentencia, 0);
        const unsigned char *valor = sqlite3_column_text(sentencia, 1);
        if (clave == NULL || valor == NULL) {
            break;
        }
        printf("%s=%s\n", (const char *)clave, (const char *)valor);
    }
    correcto = resultado == SQLITE_DONE;
    if (!correcto) {
        fprintf(stderr, "Error al leer config: %s\n", sqlite3_errmsg(base));
    }
    if (sqlite3_finalize(sentencia) != SQLITE_OK) {
        fputs("No se pudo finalizar la consulta de config.\n", stderr);
        correcto = 0;
    }
    return correcto;
}
