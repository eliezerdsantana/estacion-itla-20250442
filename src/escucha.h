/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef ESCUCHA_H
#define ESCUCHA_H

#include "red.h"

/* Recibe, guarda y confirma alarmas; duracion_s=0 significa hasta Ctrl+C. */
int escuchar(Conexion *conexion, const char *host, const char *matricula,
              const char *token, unsigned int duracion_s);

#endif
