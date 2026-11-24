/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include "sensor.h"
#include <math.h>

/**
 * @brief Prepara una secuencia reproducible de ruido para el sensor simulado.
 * @param sensor Estado independiente del generador.
 * @param semilla Valor inicial; no se usa para seguridad ni autenticacion.
 * @return No devuelve un valor.
 */
void sensor_iniciar(Sensor *sensor, uint32_t semilla) {
    sensor->estado = semilla;
}

/**
 * @brief Produce un numero entre 0 y 1 usando aritmetica unsigned definida.
 * @param sensor Estado que avanza en cada llamada.
 * @return Numero de ruido; no sirve como generador criptografico.
 */
static double ruido(Sensor *sensor) {
    sensor->estado = UINT32_C(1664525) * sensor->estado + UINT32_C(1013904223);
    return (double)(sensor->estado >> 8) / 16777215.0;
}

/**
 * @brief Genera una temperatura con onda lenta mas ruido y una corriente.
 * @param sensor Estado del ruido.
 * @param segundos Tiempo transcurrido desde que comenzo enviar.
 * @param temp Recibe una temperatura de 25 a 45 C, con dos decimales.
 * @param corriente Recibe una corriente de 2 a 5 A, con dos decimales.
 * @return No devuelve un valor.
 */
void sensor_leer(Sensor *sensor, double segundos, double *temp,
                 double *corriente) {
    *temp = round((35.0 + 9.0 * sin(segundos / 60.0) +
                   (2.0 * ruido(sensor) - 1.0)) * 100.0) / 100.0;
    *corriente = round((2.0 + 3.0 * ruido(sensor)) * 100.0) / 100.0;
}
