/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 23/11/2026. Actualizado: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#ifndef RED_H
#define RED_H

#include <stddef.h>
#include <signal.h>
#include <curl/curl.h>

#define TAM_RESPUESTA 65536

typedef struct {
    CURL *manejador;
    struct curl_slist *cabeceras_json;
    char base_url[320];
    int inicializada;
} Conexion;

typedef struct RedEnvio RedEnvio;

typedef struct {
    int transporte_correcto;
    long codigo_http;
    int tiene_retry_after;
    double retry_after_s;
    char texto[512];
} ResultadoEnvio;

/* Usa el manejador HTTP de conexion sin bloquear la generacion de lecturas. */
RedEnvio *red_envio_abrir(Conexion *conexion);
int red_envio_iniciar(RedEnvio *envio, const char *ruta, const char *json);
int red_envio_ocupado(const RedEnvio *envio);
int red_envio_avanzar(RedEnvio *envio, int espera_ms);
int red_envio_recoger(RedEnvio *envio, ResultadoEnvio *resultado);
int red_envio_cerrar(RedEnvio *envio);

/* Publicacion MQTT QoS 0 independiente del HTTP, dentro del mismo multi. */
int red_latido_preparar(RedEnvio *envio, const char *host,
                        const char *matricula, const char *token);
int red_latido_ocupado(const RedEnvio *envio);
int red_latido_iniciar(RedEnvio *envio, const char *texto);
int red_latido_recoger(RedEnvio *envio, int *correcto);

typedef int (*AlRecibirAlarma)(const char *texto, void *usuario);

/* Suscripcion con acumulacion de fragmentos; el ACK usa el easy HTTP separado. */
int red_alarmas_preparar(RedEnvio *envio, const char *host,
                         const char *matricula, const char *token,
                         AlRecibirAlarma recibir_alarma, void *usuario,
                         const volatile sig_atomic_t *detener);
int red_alarmas_iniciar(RedEnvio *envio);
int red_alarmas_ocupado(const RedEnvio *envio);
int red_alarmas_recoger(RedEnvio *envio, int *correcto);

/* Abre la conexion HTTP usando el host y el token; devuelve 1 si funciona. */
int red_abrir(Conexion *conexion, const char *host, const char *token);

/* Hace un GET, copia su texto y obtiene el codigo HTTP. Devuelve 0 ante un
 * error de transporte; el llamador comprueba aparte el codigo HTTP. */
int red_get(Conexion *conexion, const char *ruta, char *texto,
            size_t capacidad, long *codigo_http);

/* Envia JSON y recibe la respuesta. Devuelve 1 si funciono el transporte;
 * el llamador comprueba aparte el estado HTTP y el contenido recibido. */
int red_post_json(Conexion *conexion, const char *ruta, const char *json,
                  char *texto, size_t capacidad, long *codigo_http);

/* Espera los milisegundos indicados. Devuelve 1 al completar la espera. */
int red_esperar_ms(unsigned int milisegundos);

/* Libera el manejador y las cabeceras, y termina el uso de libcurl. */
void red_cerrar(Conexion *conexion);

#endif
