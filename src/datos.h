/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef DATOS_H
#define DATOS_H

#include <stddef.h>
#include "sqlite3.h"

typedef struct {
    sqlite3_int64 id;
    const char *maquina;
    sqlite3_int64 ts;
    double temp, corriente, vibracion;
    const char *estado;
    int tiene_ts, tiene_temp, tiene_corriente, tiene_vibracion;
} LecturaPlanta;

#define CANT_MAQUINAS 4

typedef struct {
    char maquina[3];
    sqlite3_int64 validas, descartadas, sobre_umbral;
    double temp_min, temp_max, temp_prom;
    int tiene_temperaturas;
} ResumenMaquina;

typedef struct {
    sqlite3_int64 seq, ts, intentos;
    double temp, corriente;
} Telemetria;

/* Conserva primero la medicion y asigna MAX(seq)+1 de forma atomica. */
int datos_guardar_telemetria(sqlite3 *base, sqlite3_int64 ts, double temp,
                            double corriente, sqlite3_int64 *seq);
int datos_primera_pendiente(sqlite3 *base, Telemetria *lectura, int *existe);
int datos_registrar_intento(sqlite3 *base, sqlite3_int64 seq);
int datos_confirmar_telemetria(sqlite3 *base, sqlite3_int64 seq);
int datos_contar_pendientes(sqlite3 *base, sqlite3_int64 *cantidad);

/* Calcula los seis campos por maquina usando exclusivamente agregados SQL.
 * Devuelve 1 si obtuvo los cuatro resultados; 0 ante un error de SQLite. */
int datos_analizar(sqlite3 *base, double umbral,
                    ResumenMaquina resumen[CANT_MAQUINAS]);

/* Abre estacion.db, activa WAL y crea las tablas. Devuelve 1 si funciona. */
int datos_abrir(sqlite3 **base);

/* Valida y guarda la respuesta clave=valor en una transaccion.
 * La matricula recibida debe coincidir con la que indica el entorno.
 * Devuelve 1 si se guardo completa y 0 si se revirtio la operacion. */
int datos_guardar_config(sqlite3 *base, const char *texto,
                         const char *matricula);

/* Consulta la tabla config y la imprime. Devuelve 1 si funciona. */
int datos_mostrar_config(sqlite3 *base);

/* Lee una clave de config. Devuelve 1 si existe y cabe en el destino. */
int datos_leer_config(sqlite3 *base, const char *clave, char *valor,
                      size_t capacidad);

/* Inicia la transaccion, reemplaza la instantanea y prepara INSERT OR IGNORE. */
int datos_preparar_descarga(sqlite3 *base, sqlite3_stmt **insercion);

/* Inserta una lectura; nueva recibe 1 si se inserto y 0 si era duplicada. */
int datos_insertar_lectura(sqlite3 *base, sqlite3_stmt *insercion,
                          const LecturaPlanta *lectura, int *nueva);

/* Finaliza la sentencia y confirma o revierte la transaccion, respectivamente. */
int datos_confirmar_descarga(sqlite3 *base, sqlite3_stmt **insercion);
int datos_cancelar_descarga(sqlite3 *base, sqlite3_stmt **insercion);

/* Calcula COUNT y SUM con SQL. Devuelve 1 si pudo obtener ambos resultados. */
int datos_resumen_descarga(sqlite3 *base, sqlite3_int64 *filas,
                           sqlite3_int64 *suma_ids);

/* Cierra SQLite. Devuelve 1 si se pudieron liberar sus recursos. */
int datos_cerrar(sqlite3 *base);

#endif
