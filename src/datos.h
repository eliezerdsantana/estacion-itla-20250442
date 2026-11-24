/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Repositorio: pendiente de publicar.
 */
#ifndef DATOS_H
#define DATOS_H

#include "sqlite3.h"

/* Abre estacion.db, activa WAL y crea las tablas. Devuelve 1 si funciona. */
int datos_abrir(sqlite3 **base);

/* Valida y guarda la respuesta clave=valor en una transaccion.
 * La matricula recibida debe coincidir con la que indica el entorno.
 * Devuelve 1 si se guardo completa y 0 si se revirtio la operacion. */
int datos_guardar_config(sqlite3 *base, const char *texto,
                         const char *matricula);

/* Consulta la tabla config y la imprime. Devuelve 1 si funciona. */
int datos_mostrar_config(sqlite3 *base);

/* Cierra SQLite. Devuelve 1 si se pudieron liberar sus recursos. */
int datos_cerrar(sqlite3 *base);

#endif
