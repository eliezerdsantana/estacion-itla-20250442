/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
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

/**
 * @brief Finaliza una sentencia y deja su puntero en NULL.
 * @param sentencia Direccion del puntero que se va a liberar.
 * @return 1 si SQLite pudo finalizarla; 0 si indico un error.
 */
static int liberar_sentencia(sqlite3_stmt **sentencia) {
    int resultado = sqlite3_finalize(*sentencia);
    *sentencia = NULL;
    if (resultado != SQLITE_OK) {
        fputs("No se pudo finalizar una sentencia de SQLite.\n", stderr);
        return 0;
    }
    return 1;
}

/**
 * @brief Consulta un valor de configuracion mediante una clave enlazada.
 * @param base Base de datos abierta.
 * @param clave Nombre de la opcion que se quiere consultar.
 * @param valor Destino del texto, que queda terminado en cero.
 * @param capacidad Espacio disponible en valor.
 * @return 1 si se copio el valor; 0 si falta la opcion o hay un error.
 */
int datos_leer_config(sqlite3 *base, const char *clave, char *valor,
                      size_t capacidad) {
    sqlite3_stmt *sentencia = NULL;
    const unsigned char *texto;
    int bytes;
    int correcto = 0;

    if (capacidad == 0) {
        return 0;
    }
    valor[0] = '\0';
    if (sqlite3_prepare_v2(base, "SELECT valor FROM config WHERE clave=?;",
                           -1, &sentencia, NULL) != SQLITE_OK ||
        sqlite3_bind_text(sentencia, 1, clave, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(sentencia) != SQLITE_ROW) {
        fprintf(stderr, "No se pudo leer config.%s: %s\n", clave,
                sqlite3_errmsg(base));
        goto terminar_lectura;
    }
    texto = sqlite3_column_text(sentencia, 0);
    bytes = sqlite3_column_bytes(sentencia, 0);
    if (texto == NULL || bytes < 0 || (size_t)bytes >= capacidad) {
        fputs("El valor de config no cabe en su destino.\n", stderr);
        goto terminar_lectura;
    }
    memcpy(valor, texto, (size_t)bytes);
    valor[bytes] = '\0';
    correcto = 1;

terminar_lectura:
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Prepara una descarga completa que reemplaza la copia local anterior.
 * @param base Base de datos abierta.
 * @param insercion Recibe la sentencia INSERT OR IGNORE reutilizable.
 * @return 1 si la transaccion esta preparada; 0 si se revirtio el intento.
 */
int datos_preparar_descarga(sqlite3 *base, sqlite3_stmt **insercion) {
    const char *sql =
        "INSERT OR IGNORE INTO lecturas_planta "
        "(id,maquina,ts,temp,corriente,vibracion,estado) VALUES (?,?,?,?,?,?,?);";

    *insercion = NULL;
    if (!ejecutar_sql(base, "BEGIN IMMEDIATE;")) {
        return 0;
    }
    /* DELETE tambien pertenece a la transaccion: un fallo recupera la copia
     * anterior. La clave primaria absorbe duplicados de las paginas nuevas. */
    if (ejecutar_sql(base, "DELETE FROM lecturas_planta;") &&
        sqlite3_prepare_v2(base, sql, -1, insercion, NULL) == SQLITE_OK) {
        return 1;
    }
    fprintf(stderr, "No se pudo preparar la descarga: %s\n", sqlite3_errmsg(base));
    if (!liberar_sentencia(insercion)) {
        fputs("La sentencia de descarga no pudo liberarse.\n", stderr);
    }
    if (!ejecutar_sql(base, "ROLLBACK;")) {
        fputs("No se pudo revertir la preparacion de la descarga.\n", stderr);
    }
    return 0;
}

/**
 * @brief Enlaza un entero o NULL cuando el campo no estaba disponible.
 * @param sentencia Sentencia preparada que recibira el valor.
 * @param indice Posicion del parametro, comenzando en 1.
 * @param existe Indica si el CSV contenia un entero interpretable.
 * @param valor Numero convertido desde el CSV.
 * @return 1 si el enlace funciono; 0 en caso contrario.
 */
static int enlazar_entero(sqlite3_stmt *sentencia, int indice, int existe,
                           sqlite3_int64 valor) {
    int resultado;
    if (existe) {
        resultado = sqlite3_bind_int64(sentencia, indice, valor);
    } else {
        resultado = sqlite3_bind_null(sentencia, indice);
    }
    return resultado == SQLITE_OK;
}

/**
 * @brief Enlaza un real o NULL cuando el campo no estaba disponible.
 * @param sentencia Sentencia preparada que recibira el valor.
 * @param indice Posicion del parametro, comenzando en 1.
 * @param existe Indica si el CSV contenia un real interpretable.
 * @param valor Numero convertido desde el CSV.
 * @return 1 si el enlace funciono; 0 en caso contrario.
 */
static int enlazar_real(sqlite3_stmt *sentencia, int indice, int existe,
                        double valor) {
    int resultado;
    if (existe) {
        resultado = sqlite3_bind_double(sentencia, indice, valor);
    } else {
        resultado = sqlite3_bind_null(sentencia, indice);
    }
    return resultado == SQLITE_OK;
}

/**
 * @brief Guarda una lectura y conserva los campos ausentes como NULL.
 * @param base Base de datos abierta, usada tambien para consultar los cambios.
 * @param insercion INSERT OR IGNORE preparado al comenzar la descarga.
 * @param lectura Datos de una fila del CSV.
 * @param nueva Recibe 1 si se inserto una fila y 0 si su id ya existia.
 * @return 1 si SQLite proceso la fila; 0 si hubo un error.
 */
int datos_insertar_lectura(sqlite3 *base, sqlite3_stmt *insercion,
                          const LecturaPlanta *lectura, int *nueva) {
    *nueva = 0;
    if (sqlite3_bind_int64(insercion, 1, lectura->id) != SQLITE_OK ||
        sqlite3_bind_text(insercion, 2, lectura->maquina, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK ||
        !enlazar_entero(insercion, 3, lectura->tiene_ts, lectura->ts) ||
        !enlazar_real(insercion, 4, lectura->tiene_temp, lectura->temp) ||
        !enlazar_real(insercion, 5, lectura->tiene_corriente, lectura->corriente) ||
        !enlazar_real(insercion, 6, lectura->tiene_vibracion, lectura->vibracion) ||
        sqlite3_bind_text(insercion, 7, lectura->estado, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(insercion) != SQLITE_DONE) {
        fprintf(stderr, "No se pudo insertar la lectura: %s\n", sqlite3_errmsg(base));
        return 0;
    }
    *nueva = sqlite3_changes(base);
    if (sqlite3_reset(insercion) != SQLITE_OK ||
        sqlite3_clear_bindings(insercion) != SQLITE_OK) {
        fprintf(stderr, "No se pudo reutilizar INSERT: %s\n", sqlite3_errmsg(base));
        return 0;
    }
    return 1;
}

/**
 * @brief Finaliza INSERT y confirma la instantanea completa.
 * @param base Base de datos con la transaccion abierta.
 * @param insercion Puntero de la sentencia que se va a liberar.
 * @return 1 si se confirmo; 0 si fallo y se intento revertir.
 */
int datos_confirmar_descarga(sqlite3 *base, sqlite3_stmt **insercion) {
    if (liberar_sentencia(insercion) && ejecutar_sql(base, "COMMIT;")) {
        return 1;
    }
    if (!ejecutar_sql(base, "ROLLBACK;")) {
        fputs("No se pudo revertir la descarga tras fallar COMMIT.\n", stderr);
    }
    return 0;
}

/**
 * @brief Cancela una descarga para conservar la instantanea anterior.
 * @param base Base con la transaccion abierta.
 * @param insercion Sentencia de insercion que se va a liberar.
 * @return 1 si se liberaron los recursos y se revirtio; 0 ante un error.
 */
int datos_cancelar_descarga(sqlite3 *base, sqlite3_stmt **insercion) {
    int correcto = liberar_sentencia(insercion);
    if (!ejecutar_sql(base, "ROLLBACK;")) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Cuenta las filas y suma sus IDs mediante una consulta SQL.
 * @param base Base de datos con las lecturas recibidas.
 * @param filas Recibe COUNT(*).
 * @param suma_ids Recibe SUM(id), o 0 si la tabla esta vacia.
 * @return 1 si se obtuvieron ambos resultados; 0 si SQLite indico un error.
 */
int datos_resumen_descarga(sqlite3 *base, sqlite3_int64 *filas,
                           sqlite3_int64 *suma_ids) {
    sqlite3_stmt *sentencia = NULL;
    int correcto = 0;

    *filas = 0;
    *suma_ids = 0;
    if (sqlite3_prepare_v2(base,
            "SELECT COUNT(*),COALESCE(SUM(id),0) FROM lecturas_planta;",
            -1, &sentencia, NULL) != SQLITE_OK ||
        sqlite3_step(sentencia) != SQLITE_ROW) {
        fprintf(stderr, "No se pudo calcular el resumen: %s\n", sqlite3_errmsg(base));
        goto terminar_resumen;
    }
    *filas = sqlite3_column_int64(sentencia, 0);
    *suma_ids = sqlite3_column_int64(sentencia, 1);
    correcto = sqlite3_step(sentencia) == SQLITE_DONE;
    if (!correcto) {
        fprintf(stderr, "El resumen no termino correctamente: %s\n",
                sqlite3_errmsg(base));
    }

terminar_resumen:
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}
