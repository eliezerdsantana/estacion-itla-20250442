/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include "escucha.h"
#include "datos.h"
#include "tiempo.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    sqlite3 *base;
    RedEnvio *red;
    char codigo_en_curso[80];
    double inicio, proxima_conexion, proximo_ack;
    unsigned int retroceso_mqtt, retroceso_http;
    int fallo_local;
} EstadoEscucha;

static volatile sig_atomic_t detener = 0;

/**
 * @brief Solicita la salida sin usar SQLite ni libcurl dentro de la senal.
 * @param senal Senal que recibio el proceso.
 * @return No devuelve un valor.
 */
static void pedir_salida(int senal) {
    (void)senal;
    detener = 1;
}

/**
 * @brief Interpreta los cuatro campos sin perder campos vacios.
 * @param texto Linea completa, sin el salto final.
 * @param alarma Recibe el codigo, la maquina y la fecha del servidor.
 * @return 1 si corresponde a una alarma valida; 0 si debe ignorarse.
 */
static int interpretar_alarma(const char *texto, Alarma *alarma) {
    char copia[100], *campos[4], *fin;
    long long ts;
    size_t n = strlen(texto);
    int i;
    if (n >= sizeof(copia)) {
        return 0;
    }
    memcpy(copia, texto, n + 1);
    campos[0] = copia;
    for (i = 1; i < 4; i++) {
        char *separador = strchr(campos[i - 1], ';');
        if (separador == NULL) {
            return 0;
        }
        *separador = '\0';
        campos[i] = separador + 1;
    }
    if (strcmp(campos[0], "ALARMA") != 0 ||
        campos[1][0] == '\0' || strlen(campos[1]) >= sizeof(alarma->codigo) ||
        strlen(campos[2]) != 2 || campos[2][0] != 'M' ||
        campos[2][1] < '1' || campos[2][1] > '4' ||
        !isdigit((unsigned char)campos[3][0])) {
        return 0;
    }
    errno = 0;
    ts = strtoll(campos[3], &fin, 10);
    if (errno == ERANGE || *fin != '\0' || ts <= 0) {
        return 0;
    }
    strcpy(alarma->codigo, campos[1]);
    strcpy(alarma->maquina, campos[2]);
    alarma->ts_servidor = (sqlite3_int64)ts;
    return 1;
}

/**
 * @brief Conserva cada alarma antes de permitir que se prepare su ACK HTTP.
 * @param texto Mensaje completo extraido del acumulador MQTT.
 * @param usuario Estado de la escucha y su base abierta.
 * @return 1 para continuar; 0 ante un fallo local que exige detenerse.
 */
static int recibir_alarma(const char *texto, void *usuario) {
    EstadoEscucha *estado = usuario;
    Alarma alarma;
    time_t ts;
    int nueva;
    estado->retroceso_mqtt = 1;
    if (strncmp(texto, "NINGUNA;", 8) == 0) {
        return 1;
    }
    if (!interpretar_alarma(texto, &alarma)) {
        fputs("Mensaje MQTT ignorado: no es una alarma valida.\n", stderr);
        return 1;
    }
    ts = time(NULL);
    if (ts == (time_t)-1) {
        fputs("No se pudo leer la fecha de recepcion.\n", stderr);
        estado->fallo_local = 1;
        return 0;
    }
    alarma.ts_recibida = (sqlite3_int64)ts;
    if (!datos_guardar_alarma(estado->base, &alarma, &nueva)) {
        estado->fallo_local = 1;
        return 0;
    }
    printf("Alarma %s: codigo=%s maquina=%s\n",
           nueva ? "guardada" : "repetida", alarma.codigo, alarma.maquina);
    return 1;
}

/**
 * @brief Escapa comillas, barras y caracteres de control del codigo para JSON.
 * @param codigo Identificador recibido, sin concatenarlo directamente al JSON.
 * @param json Destino de 512 bytes, igual que el buffer del envio HTTP.
 * @return 1 si cabe el cuerpo completo; 0 si no cabe.
 */
static int crear_json_ack(const char *codigo, char json[512]) {
    const unsigned char *p = (const unsigned char *)codigo;
    size_t usados;
    strcpy(json, "{\"codigo\":\"");
    usados = strlen(json);
    while (*p != '\0') {
        int n;
        if (*p == '"' || *p == '\\') {
            n = snprintf(json + usados, 512 - usados, "\\%c", *p);
        } else if (*p < 32) {
            n = snprintf(json + usados, 512 - usados, "\\u%04x", (unsigned int)*p);
        } else {
            n = snprintf(json + usados, 512 - usados, "%c", *p);
        }
        if (n < 0 || (size_t)n >= 512 - usados) {
            return 0;
        }
        usados += (size_t)n;
        p++;
    }
    return snprintf(json + usados, 512 - usados, "\"}") == 2 && usados + 2 < 512;
}

/**
 * @brief Inicia el ACK de la primera alarma que sigue pendiente en SQLite.
 * @param estado Cola local y conexion HTTP independiente de la suscripcion.
 * @return 1 si pudo consultar o iniciar; 0 ante un error local.
 */
static int iniciar_ack(EstadoEscucha *estado) {
    Alarma alarma;
    char json[512];
    int existe;
    if (!datos_primera_alarma_pendiente(estado->base, &alarma, &existe)) {
        return 0;
    }
    if (!existe) {
        return 1;
    }
    if (!crear_json_ack(alarma.codigo, json) ||
        !red_envio_iniciar(estado->red, "/api/v1/alarmas/ack", json)) {
        fputs("No se pudo preparar el ACK de la alarma.\n", stderr);
        return 0;
    }
    strcpy(estado->codigo_en_curso, alarma.codigo);
    return 1;
}

/**
 * @brief Confirma solo respuestas aceptadas y conserva los fallos para reintentar.
 * @param estado Codigo enviado y tiempos independientes de HTTP y MQTT.
 * @param ahora Tiempo monotono actual.
 * @return 1 si puede continuar; 0 ante un fallo local o un rechazo de acceso.
 */
static int recoger_ack(EstadoEscucha *estado, double ahora) {
    ResultadoEnvio r;
    double espera;
    if (!red_envio_recoger(estado->red, &r)) {
        return 1;
    }
    if (r.transporte_correcto && (r.codigo_http == 200 || r.codigo_http == 409)) {
        time_t ts = time(NULL);
        if (ts == (time_t)-1 || !datos_confirmar_alarma(estado->base,
                estado->codigo_en_curso, (sqlite3_int64)ts)) {
            fputs("No se pudo guardar la confirmacion de la alarma.\n", stderr);
            return 0;
        }
        printf("HTTP %ld: alarma=%s confirmada.\n", r.codigo_http,
               estado->codigo_en_curso);
        if (r.texto[0] != '\0') {
            printf("%s%s", r.texto, r.texto[strlen(r.texto) - 1] == '\n' ? "" : "\n");
        }
    } else if (r.transporte_correcto && r.codigo_http == 404) {
        if (!datos_alarma_no_vigente(estado->base, estado->codigo_en_curso)) {
            return 0;
        }
        printf("HTTP 404: alarma=%s no vigente; conservada sin ACK.\n",
               estado->codigo_en_curso);
    } else {
        if (r.transporte_correcto && (r.codigo_http == 400 ||
            r.codigo_http == 401 || r.codigo_http == 403)) {
            fprintf(stderr, "ACK rechazado con HTTP %ld; alarma conservada.\n",
                    r.codigo_http);
            return 0;
        }
        espera = estado->retroceso_http;
        if (r.transporte_correcto && (r.codigo_http == 503 || r.codigo_http == 429) &&
            r.tiene_retry_after && r.retry_after_s > espera) {
            espera = r.retry_after_s;
        }
        estado->retroceso_http = estado->retroceso_http < 16 ?
                                 estado->retroceso_http * 2 : 30;
        estado->proximo_ack = ahora + espera;
        printf("ACK pendiente: alarma=%s HTTP %ld; reintento en %.2f s.\n",
               estado->codigo_en_curso, r.codigo_http, espera);
        return 1;
    }
    estado->retroceso_http = 1;
    estado->proximo_ack = ahora + 0.25;
    return 1;
}

/**
 * @brief Alterna recepcion MQTT, ACK y reconexion sin bloquear otras tareas.
 * @param estado Recursos y relojes de la escucha.
 * @param duracion_s Limite de prueba; 0 indica hasta Ctrl+C.
 * @return 1 si termino normalmente; 0 ante un fallo local.
 */
static int ejecutar_bucle(EstadoEscucha *estado, unsigned int duracion_s) {
    while (!detener) {
        double ahora;
        int cierre_correcto;
        if (estado->fallo_local || !tiempo_monotono(&ahora)) {
            return 0;
        }
        if (duracion_s != 0 && ahora - estado->inicio >= duracion_s) {
            break;
        }
        if (!recoger_ack(estado, ahora)) {
            return 0;
        }
        if (red_alarmas_recoger(estado->red, &cierre_correcto)) {
            (void)cierre_correcto;
            estado->proxima_conexion = ahora + estado->retroceso_mqtt;
            printf("Suscripcion terminada; reconexion en %u s.\n",
                   estado->retroceso_mqtt);
            estado->retroceso_mqtt = estado->retroceso_mqtt < 16 ?
                                     estado->retroceso_mqtt * 2 : 30;
        }
        if (!red_alarmas_ocupado(estado->red) && ahora >= estado->proxima_conexion) {
            puts("Conectando al topico de alarmas MQTT...");
            if (!red_alarmas_iniciar(estado->red)) {
                return 0;
            }
        }
        if (!red_envio_ocupado(estado->red) && ahora >= estado->proximo_ack &&
            !iniciar_ack(estado)) {
            return 0;
        }
        if (!red_envio_avanzar(estado->red, 100)) {
            return 0;
        }
    }
    {
        double ahora;
        return !estado->fallo_local && tiempo_monotono(&ahora) &&
               recoger_ack(estado, ahora);
    }
}

/**
 * @brief Escucha alarmas y conserva su confirmacion junto al proceso enviar.
 * @param conexion Conexion HTTP autenticada para los ACK.
 * @param host Servidor leido del entorno.
 * @param matricula Identidad, que debe coincidir con la configuracion local.
 * @param token Clave del entorno para autenticar MQTT.
 * @param duracion_s Limite opcional; 0 significa hasta Ctrl+C.
 * @return 1 si termino y libero sus recursos; 0 ante un error.
 */
int escuchar(Conexion *conexion, const char *host, const char *matricula,
              const char *token, unsigned int duracion_s) {
    EstadoEscucha estado = {0};
    char guardada[32];
    void (*anterior_int)(int), (*anterior_term)(int);
    int correcto = 0;
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) {
        perror("No se pudo configurar la salida de alarmas");
        return 0;
    }
    detener = 0;
    anterior_int = signal(SIGINT, pedir_salida);
    if (anterior_int == SIG_ERR) {
        perror("No se pudo preparar el manejo de Ctrl+C");
        return 0;
    }
    anterior_term = signal(SIGTERM, pedir_salida);
    if (anterior_term == SIG_ERR) {
        perror("No se pudo preparar el manejo de SIGTERM");
        if (signal(SIGINT, anterior_int) == SIG_ERR) {
            fputs("No se pudo restaurar SIGINT.\n", stderr);
        }
        return 0;
    }
    if (!datos_abrir(&estado.base)) {
        goto terminar_escucha;
    }
    if (!datos_leer_config(estado.base, "matricula", guardada, sizeof(guardada)) ||
        strcmp(guardada, matricula) != 0) {
        fputs("Primero ejecuta config para esta matricula.\n", stderr);
        goto terminar_escucha;
    }
    if (!tiempo_monotono(&estado.inicio)) {
        goto terminar_escucha;
    }
    estado.red = red_envio_abrir(conexion);
    if (estado.red == NULL || !red_alarmas_preparar(estado.red, host, matricula,
            token, recibir_alarma, &estado, &detener)) {
        goto terminar_escucha;
    }
    estado.retroceso_mqtt = estado.retroceso_http = 1;
    puts("Alarmas: recepcion MQTT y confirmacion HTTP. Ctrl+C para detener.");
    correcto = ejecutar_bucle(&estado, duracion_s);
terminar_escucha:
    if (!red_envio_cerrar(estado.red)) {
        correcto = 0;
    }
    if (!datos_cerrar(estado.base)) {
        correcto = 0;
    }
    if (signal(SIGINT, anterior_int) == SIG_ERR) {
        perror("No se pudo restaurar SIGINT");
        correcto = 0;
    }
    if (signal(SIGTERM, anterior_term) == SIG_ERR) {
        perror("No se pudo restaurar SIGTERM");
        correcto = 0;
    }
    puts("Escucha detenida; alarmas conservadas en SQLite.");
    return correcto;
}
