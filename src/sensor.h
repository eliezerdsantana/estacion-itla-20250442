/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

typedef struct { uint32_t estado; } Sensor;

/* Inicializa el generador de ruido; la semilla no es una credencial. */
void sensor_iniciar(Sensor *sensor, uint32_t semilla);

/* Simula temperatura de 25 a 45 C y corriente de 2 a 5 A. */
void sensor_leer(Sensor *sensor, double segundos, double *temp, double *corriente);

#endif
