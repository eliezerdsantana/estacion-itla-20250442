/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef ANALISIS_H
#define ANALISIS_H

#include "red.h"

/* Calcula e imprime el resumen. Con enviar=1 tambien hace un unico POST.
 * conexion puede ser NULL en el modo local. Devuelve 1 si funciona. */
int analizar(Conexion *conexion, const char *matricula, int enviar);

#endif
