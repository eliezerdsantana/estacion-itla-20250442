/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include "datos.h"

#include <math.h>
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
 * @brief Abre la base, las cuatro tablas del mandato y el registro auxiliar 404.
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
        "ts_ack INTEGER);"
        "CREATE TABLE IF NOT EXISTS alarmas_no_vigentes ("
        "codigo TEXT PRIMARY KEY NOT NULL);";

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

/**
 * @brief Clasifica las lecturas y calcula las seis estadisticas con SQL.
 * @param base Base que contiene la instantanea descargada.
 * @param umbral Limite de temperatura leido de config.
 * @param resumen Recibe una fila por maquina, de M1 a M4.
 * @return 1 si la consulta termino; 0 si hubo un error.
 */
int datos_analizar(sqlite3 *base, double umbral,
                    ResumenMaquina resumen[CANT_MAQUINAS]) {
    const char *maquinas[] = {"M1", "M2", "M3", "M4"};
    const char *sql =
        "WITH maquinas(maquina) AS (VALUES ('M1'),('M2'),('M3'),('M4')),"
        "clasificadas AS ("
        " SELECT *, CASE WHEN id IS NOT NULL AND maquina IS NOT NULL"
        " AND maquina<>'' AND ts IS NOT NULL AND temp IS NOT NULL"
        " AND corriente IS NOT NULL AND vibracion IS NOT NULL"
        " AND estado='OK' AND temp<>-999.0 THEN 1 ELSE 0 END AS valida"
        " FROM lecturas_planta)"
        " SELECT m.maquina,"
        " COUNT(CASE WHEN l.valida=1 THEN 1 END),"
        " COUNT(CASE WHEN l.valida=0 THEN 1 END),"
        " MIN(CASE WHEN l.valida=1 THEN l.temp END),"
        " MAX(CASE WHEN l.valida=1 THEN l.temp END),"
        " ROUND(AVG(CASE WHEN l.valida=1 THEN l.temp END),2),"
        " SUM(CASE WHEN l.valida=1 AND l.temp>? THEN 1 ELSE 0 END)"
        " FROM maquinas AS m LEFT JOIN clasificadas AS l"
        " ON l.maquina=m.maquina GROUP BY m.maquina ORDER BY m.maquina;";
    sqlite3_stmt *sentencia = NULL;
    int correcto = 0;
    int i;

    memset(resumen, 0, CANT_MAQUINAS * sizeof(*resumen));
    if (!isfinite(umbral) ||
        sqlite3_prepare_v2(base, sql, -1, &sentencia, NULL) != SQLITE_OK ||
        sqlite3_bind_double(sentencia, 1, umbral) != SQLITE_OK) {
        fprintf(stderr, "No se pudo preparar el analisis: %s\n",
                sqlite3_errmsg(base));
        goto terminar_analisis;
    }
    /* El bucle copia las cuatro filas del resultado; SQL hace los calculos. */
    for (i = 0; i < CANT_MAQUINAS; i++) {
        const unsigned char *maquina;
        ResumenMaquina *r = &resumen[i];

        if (sqlite3_step(sentencia) != SQLITE_ROW) {
            fputs("Falta una maquina en el resultado SQL.\n", stderr);
            goto terminar_analisis;
        }
        maquina = sqlite3_column_text(sentencia, 0);
        if (maquina == NULL ||
            strcmp((const char *)maquina, maquinas[i]) != 0) {
            fputs("El analisis contiene una maquina inesperada.\n", stderr);
            goto terminar_analisis;
        }
        memcpy(r->maquina, maquinas[i], sizeof(r->maquina));
        r->validas = sqlite3_column_int64(sentencia, 1);
        r->descartadas = sqlite3_column_int64(sentencia, 2);
        r->sobre_umbral = sqlite3_column_int64(sentencia, 6);
        r->tiene_temperaturas = sqlite3_column_type(sentencia, 3) != SQLITE_NULL;
        if (r->tiene_temperaturas) {
            r->temp_min = sqlite3_column_double(sentencia, 3);
            r->temp_max = sqlite3_column_double(sentencia, 4);
            r->temp_prom = sqlite3_column_double(sentencia, 5);
            if (!isfinite(r->temp_min) || !isfinite(r->temp_max) ||
                !isfinite(r->temp_prom)) {
                fputs("Las estadisticas no contienen numeros finitos.\n", stderr);
                goto terminar_analisis;
            }
        }
    }
    if (sqlite3_step(sentencia) != SQLITE_DONE) {
        fprintf(stderr, "El analisis SQL no termino: %s\n", sqlite3_errmsg(base));
        goto terminar_analisis;
    }
    correcto = 1;

terminar_analisis:
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Guarda antes de enviar; SQLite asigna la secuencia sin una carrera.
 * @param base Base compartida con los otros subcomandos.
 * @param ts Momento de la medicion, en segundos desde Epoch.
 * @param temp Temperatura producida por el sensor.
 * @param corriente Corriente producida por el sensor.
 * @param seq Recibe la nueva secuencia persistente.
 * @return 1 si la fila quedo guardada; 0 ante un error.
 */
int datos_guardar_telemetria(sqlite3 *base, sqlite3_int64 ts, double temp,
                            double corriente, sqlite3_int64 *seq) {
    const char *sql =
        "INSERT INTO telemetria(seq,ts,temp,corriente,enviado,intentos)"
        " SELECT COALESCE(MAX(seq),0)+1,?,?,?,0,0 FROM telemetria;";
    sqlite3_stmt *sentencia = NULL;
    int correcto = 0;

    if (ts <= 0 || !isfinite(temp) || !isfinite(corriente)) {
        fputs("La medicion no contiene valores validos.\n", stderr);
        return 0;
    }
    if (sqlite3_prepare_v2(base, sql, -1, &sentencia, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(sentencia, 1, ts) != SQLITE_OK ||
        sqlite3_bind_double(sentencia, 2, temp) != SQLITE_OK ||
        sqlite3_bind_double(sentencia, 3, corriente) != SQLITE_OK ||
        sqlite3_step(sentencia) != SQLITE_DONE || sqlite3_changes(base) != 1) {
        fprintf(stderr, "No se pudo guardar la medicion: %s\n",
                sqlite3_errmsg(base));
        goto terminar_medicion;
    }
    *seq = sqlite3_last_insert_rowid(base);
    correcto = 1;
terminar_medicion:
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Consulta la pendiente mas antigua sin retirarla de la cola.
 * @param base Base abierta.
 * @param lectura Recibe los valores de la fila, si existe.
 * @param existe Recibe 1 si habia una pendiente; 0 si la cola esta vacia.
 * @return 1 si la consulta funciono; 0 ante un error.
 */
int datos_primera_pendiente(sqlite3 *base, Telemetria *lectura, int *existe) {
    const char *sql =
        "SELECT seq,ts,temp,corriente,intentos FROM telemetria"
        " WHERE enviado=0 ORDER BY seq LIMIT 1;";
    sqlite3_stmt *sentencia = NULL;
    int paso, correcto = 0;

    *existe = 0;
    if (sqlite3_prepare_v2(base, sql, -1, &sentencia, NULL) != SQLITE_OK) {
        goto terminar_pendiente;
    }
    paso = sqlite3_step(sentencia);
    if (paso == SQLITE_ROW) {
        lectura->seq = sqlite3_column_int64(sentencia, 0);
        lectura->ts = sqlite3_column_int64(sentencia, 1);
        lectura->temp = sqlite3_column_double(sentencia, 2);
        lectura->corriente = sqlite3_column_double(sentencia, 3);
        lectura->intentos = sqlite3_column_int64(sentencia, 4);
        *existe = 1;
        paso = sqlite3_step(sentencia);
    }
    correcto = paso == SQLITE_DONE;
terminar_pendiente:
    if (!correcto) {
        fprintf(stderr, "No se pudo consultar la pendiente: %s\n",
                sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Actualiza una fila mediante una instruccion fija y un seq enlazado.
 * @param base Base abierta.
 * @param sql UPDATE fijo del programa, con un solo parametro.
 * @param seq Secuencia que se modifica.
 * @return 1 si se actualizo exactamente una pendiente; 0 si fallo.
 */
static int actualizar_telemetria(sqlite3 *base, const char *sql,
                                  sqlite3_int64 seq) {
    sqlite3_stmt *sentencia = NULL;
    int correcto = 0;
    if (sqlite3_prepare_v2(base, sql, -1, &sentencia, NULL) == SQLITE_OK &&
        sqlite3_bind_int64(sentencia, 1, seq) == SQLITE_OK &&
        sqlite3_step(sentencia) == SQLITE_DONE && sqlite3_changes(base) == 1) {
        correcto = 1;
    }
    if (!correcto) {
        fprintf(stderr, "No se pudo actualizar seq=%lld: %s\n",
                (long long)seq, sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Cuenta el intento antes de comenzar la transferencia.
 * @param base Base abierta.
 * @param seq Secuencia pendiente.
 * @return 1 si quedo registrado; 0 ante un error.
 */
int datos_registrar_intento(sqlite3 *base, sqlite3_int64 seq) {
    return actualizar_telemetria(base,
        "UPDATE telemetria SET intentos=intentos+1 WHERE seq=? AND enviado=0;",
        seq);
}

/**
 * @brief Marca la lectura despues de recibir la confirmacion HTTP.
 * @param base Base abierta.
 * @param seq Secuencia que el servidor acepto.
 * @return 1 si se marco; 0 ante un error.
 */
int datos_confirmar_telemetria(sqlite3 *base, sqlite3_int64 seq) {
    return actualizar_telemetria(base,
        "UPDATE telemetria SET enviado=1 WHERE seq=? AND enviado=0;", seq);
}

/**
 * @brief Cuenta las pendientes mediante SQL.
 * @param base Base abierta.
 * @param cantidad Recibe el numero de filas sin confirmar.
 * @return 1 si pudo contarlas; 0 ante un error.
 */
int datos_contar_pendientes(sqlite3 *base, sqlite3_int64 *cantidad) {
    sqlite3_stmt *sentencia = NULL;
    int correcto = 0;
    if (sqlite3_prepare_v2(base,
            "SELECT COUNT(*) FROM telemetria WHERE enviado=0;",
            -1, &sentencia, NULL) == SQLITE_OK &&
        sqlite3_step(sentencia) == SQLITE_ROW) {
        *cantidad = sqlite3_column_int64(sentencia, 0);
        correcto = sqlite3_step(sentencia) == SQLITE_DONE;
    }
    if (!correcto) {
        fprintf(stderr, "No se pudo contar la cola: %s\n", sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&sentencia)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Guarda la primera recepcion de una alarma sin duplicar su codigo.
 * @param base Base abierta, compartida con enviar.
 * @param alarma Datos interpretados del mensaje MQTT.
 * @param nueva Recibe 1 si se inserto o 0 si ya existia el codigo.
 * @return 1 si se conservo el registro; 0 ante un error.
 */
int datos_guardar_alarma(sqlite3 *base, const Alarma *alarma, int *nueva) {
    const char *sql = "INSERT OR IGNORE INTO alarmas"
        "(codigo,maquina,ts_servidor,ts_recibida,ts_ack) VALUES(?,?,?,?,NULL);";
    sqlite3_stmt *s = NULL;
    int correcto = 0;
    *nueva = 0;
    if (sqlite3_prepare_v2(base, sql, -1, &s, NULL) == SQLITE_OK &&
        sqlite3_bind_text(s, 1, alarma->codigo, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_text(s, 2, alarma->maquina, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_int64(s, 3, alarma->ts_servidor) == SQLITE_OK &&
        sqlite3_bind_int64(s, 4, alarma->ts_recibida) == SQLITE_OK &&
        sqlite3_step(s) == SQLITE_DONE) {
        *nueva = sqlite3_changes(base);
        correcto = 1;
    }
    if (!correcto) {
        fprintf(stderr, "No se pudo guardar la alarma: %s\n", sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&s)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Busca una alarma sin ACK, excluyendo las que el servidor rechazo con 404.
 * @param base Base abierta.
 * @param alarma Recibe una copia de los valores, si existe una pendiente.
 * @param existe Recibe 1 si hay pendiente; 0 si no hay.
 * @return 1 si pudo consultar; 0 ante un error.
 */
int datos_primera_alarma_pendiente(sqlite3 *base, Alarma *alarma, int *existe) {
    const char *sql = "SELECT codigo,maquina,ts_servidor,ts_recibida"
        " FROM alarmas AS a WHERE ts_ack IS NULL AND NOT EXISTS"
        " (SELECT 1 FROM alarmas_no_vigentes AS n WHERE n.codigo=a.codigo)"
        " ORDER BY ts_recibida,codigo LIMIT 1;";
    sqlite3_stmt *s = NULL;
    int paso, correcto = 0;
    *existe = 0;
    if (sqlite3_prepare_v2(base, sql, -1, &s, NULL) != SQLITE_OK) {
        goto terminar_busqueda_alarma;
    }
    paso = sqlite3_step(s);
    if (paso == SQLITE_ROW) {
        const unsigned char *codigo = sqlite3_column_text(s, 0);
        const unsigned char *maquina = sqlite3_column_text(s, 1);
        int nc = sqlite3_column_bytes(s, 0), nm = sqlite3_column_bytes(s, 1);
        if (codigo == NULL || maquina == NULL || nc < 1 || nm < 1 ||
            (size_t)nc >= sizeof(alarma->codigo) ||
            (size_t)nm >= sizeof(alarma->maquina)) {
            fputs("Una alarma guardada no cabe en su destino.\n", stderr);
            goto terminar_busqueda_alarma;
        }
        memcpy(alarma->codigo, codigo, (size_t)nc);
        alarma->codigo[nc] = '\0';
        memcpy(alarma->maquina, maquina, (size_t)nm);
        alarma->maquina[nm] = '\0';
        alarma->ts_servidor = sqlite3_column_int64(s, 2);
        alarma->ts_recibida = sqlite3_column_int64(s, 3);
        *existe = 1;
        paso = sqlite3_step(s);
    }
    correcto = paso == SQLITE_DONE;
terminar_busqueda_alarma:
    if (!correcto) {
        fprintf(stderr, "No se pudo consultar la alarma pendiente: %s\n",
                sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&s)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Registra cuando se verifico la confirmacion 200 o 409 del servidor.
 * @param base Base abierta.
 * @param codigo Identificador de la alarma.
 * @param ts_ack Fecha local de la confirmacion.
 * @return 1 si se actualizo la alarma; 0 ante un error.
 */
int datos_confirmar_alarma(sqlite3 *base, const char *codigo,
                          sqlite3_int64 ts_ack) {
    sqlite3_stmt *s = NULL;
    int correcto = 0;
    if (sqlite3_prepare_v2(base, "UPDATE alarmas SET ts_ack=? WHERE codigo=?;",
                           -1, &s, NULL) == SQLITE_OK &&
        sqlite3_bind_int64(s, 1, ts_ack) == SQLITE_OK &&
        sqlite3_bind_text(s, 2, codigo, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(base) == 1) {
        correcto = 1;
    }
    if (!correcto) {
        fprintf(stderr, "No se pudo registrar el ACK: %s\n", sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&s)) {
        correcto = 0;
    }
    return correcto;
}

/**
 * @brief Conserva una alarma que ya no existe en el servidor sin inventar un ACK.
 * @param base Base abierta.
 * @param codigo Identificador que recibio HTTP 404.
 * @return 1 si se registro; 0 ante un error.
 */
int datos_alarma_no_vigente(sqlite3 *base, const char *codigo) {
    sqlite3_stmt *s = NULL;
    int correcto = 0;
    if (sqlite3_prepare_v2(base,
            "INSERT OR IGNORE INTO alarmas_no_vigentes(codigo) VALUES(?);",
            -1, &s, NULL) == SQLITE_OK &&
        sqlite3_bind_text(s, 1, codigo, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(s) == SQLITE_DONE) {
        correcto = 1;
    }
    if (!correcto) {
        fprintf(stderr, "No se pudo registrar el 404 de la alarma: %s\n",
                sqlite3_errmsg(base));
    }
    if (!liberar_sentencia(&s)) {
        correcto = 0;
    }
    return correcto;
}
