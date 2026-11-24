/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "tiempo.h"
#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

/**
 * @brief Lee el reloj monotono para programar intervalos y reintentos.
 * @param segundos Recibe el tiempo; solo se comparan diferencias entre lecturas.
 * @return 1 si pudo leer el reloj; 0 si el sistema indico un error.
 */
int tiempo_monotono(double *segundos) {
#ifdef _WIN32
    *segundos = (double)GetTickCount64() / 1000.0;
    return 1;
#else
    struct timespec reloj;
    if (clock_gettime(CLOCK_MONOTONIC, &reloj) != 0) {
        fputs("No se pudo leer el reloj monotono.\n", stderr);
        return 0;
    }
    *segundos = (double)reloj.tv_sec + (double)reloj.tv_nsec / 1000000000.0;
    return 1;
#endif
}
