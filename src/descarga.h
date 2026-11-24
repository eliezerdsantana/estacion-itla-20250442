/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef DESCARGA_H
#define DESCARGA_H

#include "red.h"

/* Descarga las paginas, guarda una instantanea y envia su comprobacion.
 * Devuelve 1 si el servidor confirma los resultados y 0 ante un error. */
int descargar(Conexion *conexion, const char *matricula);

#endif
