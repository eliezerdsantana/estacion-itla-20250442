/*
 * Programacion para Mecatronicos - ITLA
 * Autor: Eliezer Daniels Santana. Matricula: 2025-0442.
 * Seccion: Miercoles. Practica: Estacion de Telemetria ITLA.
 * Fecha: 24/11/2026.
 * Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442
 */
#include "envio.h"
#include "datos.h"
#include "sensor.h"
#include "tiempo.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    sqlite3 *base;
    RedEnvio *red;
    Sensor sensor;
    sqlite3_int64 seq_en_curso, seq_ultima, seq_latido, pendientes_latido;
    unsigned int intervalo_s, latido_s, retroceso_s;
    double inicio, proxima_lectura, proximo_envio, proximo_latido;
} EstadoEnvio;

static volatile sig_atomic_t detener = 0;

/**
 * @brief Solicita la salida; el manejador no hace SQL, red ni impresiones.
 * @param senal Senal recibida por el proceso.
 * @return No devuelve un valor.
 */
static void pedir_salida(int senal) {
    (void)senal;
    detener = 1;
}

/**
 * @brief Lee un periodo positivo de la configuracion guardada.
 * @param base Base abierta.
 * @param clave Nombre de la opcion.
 * @param segundos Recibe el numero de segundos.
 * @return 1 si la opcion es valida; 0 si falta o no es un entero positivo.
 */
static int leer_periodo(sqlite3 *base, const char *clave, unsigned int *segundos) {
    char intervalo[32], *fin;
    unsigned long numero;
    if (!datos_leer_config(base, clave, intervalo, sizeof(intervalo))) {
        return 0;
    }
    errno = 0;
    numero = strtoul(intervalo, &fin, 10);
    if (!isdigit((unsigned char)intervalo[0]) || *fin != '\0' ||
        errno == ERANGE || numero == 0 || numero > UINT_MAX) {
        fprintf(stderr, "config.%s no es un periodo valido.\n", clave);
        return 0;
    }
    *segundos = (unsigned int)numero;
    return 1;
}

/**
 * @brief Comprueba la identidad y los periodos del sensor y de los latidos.
 * @param estado Estado que recibe ambos periodos.
 * @param matricula Identidad del entorno.
 * @return 1 si la configuracion permite empezar; 0 si falta o es invalida.
 */
static int preparar_configuracion(EstadoEnvio *estado, const char *matricula) {
    char guardada[32];
    if (!datos_leer_config(estado->base, "matricula", guardada, sizeof(guardada)) ||
        strcmp(guardada, matricula) != 0) {
        fputs("Primero ejecuta config para esta matricula.\n", stderr);
        return 0;
    }
    return leer_periodo(estado->base, "intervalo_s", &estado->intervalo_s) &&
           leer_periodo(estado->base, "latido_s", &estado->latido_s);
}

/**
 * @brief Produce una medicion y la conserva antes de cualquier envio.
 * @param estado Estado del sensor y de la base.
 * @param ahora Tiempo monotono de este ciclo.
 * @return 1 si se guardo; 0 ante un error de almacenamiento o reloj.
 */
static int guardar_lectura(EstadoEnvio *estado, double ahora) {
    double temp, corriente;
    sqlite3_int64 seq, pendientes;
    time_t ts = time(NULL);
    if (ts == (time_t)-1) {
        fputs("No se pudo leer la fecha de la medicion.\n", stderr);
        return 0;
    }
    sensor_leer(&estado->sensor, ahora - estado->inicio, &temp, &corriente);
    if (!datos_guardar_telemetria(estado->base, (sqlite3_int64)ts, temp,
                                 corriente, &seq) ||
        !datos_contar_pendientes(estado->base, &pendientes)) {
        return 0;
    }
    printf("Guardada seq=%lld temp=%.2f corriente=%.2f pendientes=%lld\n",
           (long long)seq, temp, corriente, (long long)pendientes);
    estado->seq_ultima = seq;
    estado->proxima_lectura += estado->intervalo_s;
    /* Una suspension larga del equipo no inventa mediciones del pasado. */
    if (estado->proxima_lectura <= ahora) {
        estado->proxima_lectura = ahora + estado->intervalo_s;
    }
    return 1;
}

/**
 * @brief Prepara el envio de la pendiente mas antigua, sin retirarla.
 * @param estado Estado del envio.
 * @return 1 si pudo consultar o preparar; 0 ante un error.
 */
static int iniciar_pendiente(EstadoEnvio *estado) {
    Telemetria lectura;
    char json[256];
    int existe, n;
    if (!datos_primera_pendiente(estado->base, &lectura, &existe)) {
        return 0;
    }
    if (!existe) {
        return 1;
    }
    n = snprintf(json, sizeof(json),
        "{\"seq\":%lld,\"ts\":%lld,\"temp\":%.2f,\"corriente\":%.2f}",
        (long long)lectura.seq, (long long)lectura.ts,
        lectura.temp, lectura.corriente);
    if (n < 0 || (size_t)n >= sizeof(json)) {
        fputs("El JSON de la medicion no cabe.\n", stderr);
        return 0;
    }
    if (!red_envio_iniciar(estado->red, "/api/v1/telemetria", json) ||
        !datos_registrar_intento(estado->base, lectura.seq)) {
        return 0;
    }
    estado->seq_en_curso = lectura.seq;
    return 1;
}

/**
 * @brief Busca una confirmacion exacta entre lineas clave=valor.
 * @param texto Respuesta que puede usar LF o CRLF.
 * @param buscada Linea completa esperada.
 * @return 1 si encontro la linea; 0 si no aparece.
 */
static int contiene_linea(const char *texto, const char *buscada) {
    while (*texto != '\0') {
        const char *fin = strchr(texto, '\n');
        size_t n = fin != NULL ? (size_t)(fin - texto) : strlen(texto);
        if (n > 0 && texto[n - 1] == '\r') {
            n--;
        }
        if (n == strlen(buscada) && memcmp(texto, buscada, n) == 0) {
            return 1;
        }
        if (fin == NULL) {
            break;
        }
        texto = fin + 1;
    }
    return 0;
}

/**
 * @brief Recoge una respuesta y decide confirmar o programar un reintento.
 * @param estado Cola y tiempos de espera.
 * @param ahora Tiempo monotono actual.
 * @return 1 si puede continuar; 0 ante un error persistente de datos o acceso.
 */
static int procesar_respuesta(EstadoEnvio *estado, double ahora) {
    ResultadoEnvio r;
    sqlite3_int64 pendientes;
    double espera;
    int confirmada;

    if (!red_envio_recoger(estado->red, &r)) {
        return 1;
    }
    confirmada = r.transporte_correcto &&
        ((r.codigo_http == 201 && contiene_linea(r.texto, "estado=creada")) ||
         (r.codigo_http == 200 && contiene_linea(r.texto, "estado=duplicada")));
    if (confirmada) {
        if (!datos_confirmar_telemetria(estado->base, estado->seq_en_curso) ||
            !datos_contar_pendientes(estado->base, &pendientes)) {
            return 0;
        }
        printf("HTTP %ld: seq=%lld confirmada; pendientes=%lld\n",
               r.codigo_http, (long long)estado->seq_en_curso,
               (long long)pendientes);
        estado->retroceso_s = 1;
        estado->proximo_envio = ahora + 0.250;
        return 1;
    }
    if (r.transporte_correcto && (r.codigo_http == 400 ||
        r.codigo_http == 401 || r.codigo_http == 403 || r.codigo_http == 404)) {
        fprintf(stderr, "HTTP %ld: revisa los datos o credenciales; "
                "seq=%lld sigue guardada.\n", r.codigo_http,
                (long long)estado->seq_en_curso);
        return 0;
    }
    if (r.transporte_correcto && (r.codigo_http == 503 || r.codigo_http == 429) &&
        r.tiene_retry_after) {
        espera = r.retry_after_s > 0.250 ? r.retry_after_s : 0.250;
    } else {
        espera = (double)estado->retroceso_s;
        estado->retroceso_s = estado->retroceso_s < 16 ?
                             estado->retroceso_s * 2 : 30;
    }
    estado->proximo_envio = ahora + espera;
    printf("seq=%lld pendiente: HTTP %ld, transporte=%s; reintento en %.2f s.\n",
           (long long)estado->seq_en_curso, r.codigo_http,
           r.transporte_correcto ? "ok" : "fallo", espera);
    return 1;
}

/**
 * @brief Muestra el resultado de la publicacion sin afectar la cola HTTP.
 * @param estado Estado que conserva los datos del latido que se publico.
 * @return No devuelve un valor.
 */
static void recoger_latido(EstadoEnvio *estado) {
    int correcto;
    if (red_latido_recoger(estado->red, &correcto)) {
        printf("Latido MQTT %s: seq=%lld pendientes=%lld\n",
               correcto ? "enviado" : "descartado",
               (long long)estado->seq_latido,
               (long long)estado->pendientes_latido);
    }
}

/**
 * @brief Publica el estado actual cuando corresponde, sin encolar latidos.
 * @param estado Estado de la sesion.
 * @param ahora Tiempo monotono actual.
 * @return 1 si continua; 0 si fallo la consulta de pendientes.
 */
static int programar_latido(EstadoEnvio *estado, double ahora) {
    char texto[100];
    sqlite3_int64 pendientes;
    time_t ts;
    int n;
    if (ahora < estado->proximo_latido) {
        return 1;
    }
    estado->proximo_latido += estado->latido_s;
    if (estado->proximo_latido <= ahora) {
        estado->proximo_latido = ahora + estado->latido_s;
    }
    if (red_latido_ocupado(estado->red)) {
        puts("Latido omitido: la publicacion anterior sigue en curso.");
        return 1;
    }
    if (!datos_contar_pendientes(estado->base, &pendientes)) {
        return 0;
    }
    ts = time(NULL);
    if (ts == (time_t)-1) {
        fputs("No se pudo leer la fecha del latido; se descarta.\n", stderr);
        return 1;
    }
    n = snprintf(texto, sizeof(texto), "LATIDO;%lld;%lld;%lld\n",
                 (long long)estado->seq_ultima, (long long)pendientes,
                 (long long)ts);
    if (n < 0 || (size_t)n >= sizeof(texto)) {
        fputs("El latido no cabe; se descarta.\n", stderr);
        return 1;
    }
    if (red_latido_iniciar(estado->red, texto)) {
        estado->seq_latido = estado->seq_ultima;
        estado->pendientes_latido = pendientes;
    }
    return 1;
}

/**
 * @brief Alterna tareas cortas sin dormir durante todo Retry-After.
 * @param estado Estado de la sesion.
 * @param duracion_s Limite opcional; 0 significa hasta Ctrl+C.
 * @return 1 si termino normalmente; 0 ante un error que requiere intervencion.
 */
static int ejecutar_bucle(EstadoEnvio *estado, unsigned int duracion_s) {
    while (!detener) {
        double ahora;
        if (!tiempo_monotono(&ahora)) {
            return 0;
        }
        if (duracion_s != 0 && ahora - estado->inicio >= duracion_s) {
            break;
        }
        if (ahora >= estado->proxima_lectura && !guardar_lectura(estado, ahora)) {
            return 0;
        }
        if (!procesar_respuesta(estado, ahora)) {
            return 0;
        }
        recoger_latido(estado);
        if (!red_envio_ocupado(estado->red) && ahora >= estado->proximo_envio &&
            !iniciar_pendiente(estado)) {
            return 0;
        }
        if (!programar_latido(estado, ahora)) {
            return 0;
        }
        if (!red_envio_avanzar(estado->red, 100)) {
            return 0;
        }
    }
    /* Si ya llego la confirmacion, se registra antes de cerrar. */
    {
        double ahora;
        recoger_latido(estado);
        return tiempo_monotono(&ahora) && procesar_respuesta(estado, ahora);
    }
}

/**
 * @brief Mantiene la cola y el sensor hasta el limite o la solicitud de salida.
 * @param conexion Conexion HTTP autenticada.
 * @param host Servidor leido del entorno, compartido por HTTP y MQTT.
 * @param matricula Identidad del entorno, que debe coincidir con config.
 * @param token Clave del entorno para autenticar los latidos MQTT.
 * @param duracion_s Limite opcional en segundos; 0 indica sin limite.
 * @return 1 si termino normalmente y libero sus recursos; 0 ante un error.
 */
int enviar(Conexion *conexion, const char *host, const char *matricula,
            const char *token, unsigned int duracion_s) {
    EstadoEnvio estado = {0};
    void (*anterior_int)(int), (*anterior_term)(int);
    sqlite3_int64 pendientes;
    int correcto = 0;

    /* En Windows, _IOLBF con tamano 0 es invalido. _IONBF muestra cada
     * mensaje inmediatamente y no necesita reservar un buffer. */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) {
        perror("No se pudo configurar la salida de telemetria");
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
    if (!datos_abrir(&estado.base) ||
        !preparar_configuracion(&estado, matricula) ||
        !tiempo_monotono(&estado.inicio)) {
        goto terminar_envio;
    }
    estado.red = red_envio_abrir(conexion);
    if (estado.red == NULL ||
        !red_latido_preparar(estado.red, host, matricula, token) ||
        !datos_contar_pendientes(estado.base, &pendientes)) {
        goto terminar_envio;
    }
    sensor_iniciar(&estado.sensor, (uint32_t)time(NULL));
    estado.retroceso_s = 1;
    estado.proxima_lectura = estado.inicio;
    estado.proximo_envio = estado.inicio;
    estado.proximo_latido = estado.inicio;
    printf("Telemetria: intervalo_s=%u; latido_s=%u; pendientes=%lld. "
           "Ctrl+C para detener.\n", estado.intervalo_s, estado.latido_s,
           (long long)pendientes);
    correcto = ejecutar_bucle(&estado, duracion_s);

terminar_envio:
    if (!red_envio_cerrar(estado.red)) {
        correcto = 0;
    }
    if (estado.base != NULL) {
        if (datos_contar_pendientes(estado.base, &pendientes)) {
            printf("Envio detenido; pendientes=%lld conservadas en SQLite.\n",
                   (long long)pendientes);
        } else {
            correcto = 0;
        }
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
    return correcto;
}
