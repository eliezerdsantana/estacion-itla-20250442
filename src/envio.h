/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef ENVIO_H
#define ENVIO_H

#include "red.h"

/* Genera, guarda y envia hasta Ctrl+C o hasta duracion_s; 0 indica sin limite. */
int enviar(Conexion *conexion, const char *host, const char *matricula,
            const char *token, unsigned int duracion_s);

#endif
