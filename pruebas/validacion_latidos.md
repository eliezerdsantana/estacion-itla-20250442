# Validacion de los latidos MQTT

Fecha: 24 de noviembre de 2026.
Codigo probado: 493d883146826dd9b3ac85dafc35fb0d252cdca9.

## Comprobacion automatica

Compilacion sin advertencias en Linux y MSYS2 UCRT64 sobre Windows.
Pasaron las diez pruebas nuevas de MQTT en ambos sistemas.
En conjunto hubo 46 pruebas correctas en Linux y 45 en Windows, donde se
omite la prueba de SIGINT por proceso que se realiza en Linux.

[Resultado de GitHub Actions](https://github.com/eliezerdsantana/estacion-itla-20250442/actions/runs/45426348165)

## Corrida de 45 segundos en Windows

Ejecute make run ARGS="enviar --duracion 45" desde MSYS2 UCRT64:

~~~text
./estacion.exe enviar --duracion 45
Telemetria: intervalo_s=5; latido_s=10; pendientes=0. Ctrl+C para detener.
Guardada seq=7 temp=34.48 corriente=3.24 pendientes=1
HTTP 201: seq=7 confirmada; pendientes=0
Latido MQTT enviado: seq=7 pendientes=1
Guardada seq=8 temp=36.51 corriente=3.80 pendientes=1
HTTP 201: seq=8 confirmada; pendientes=0
Guardada seq=9 temp=35.82 corriente=4.32 pendientes=1
HTTP 201: seq=9 confirmada; pendientes=0
Latido MQTT enviado: seq=9 pendientes=1
Guardada seq=10 temp=37.42 corriente=4.16 pendientes=1
HTTP 201: seq=10 confirmada; pendientes=0
Guardada seq=11 temp=37.65 corriente=4.33 pendientes=1
HTTP 201: seq=11 confirmada; pendientes=0
Latido MQTT enviado: seq=11 pendientes=1
Guardada seq=12 temp=38.82 corriente=2.77 pendientes=1
HTTP 201: seq=12 confirmada; pendientes=0
Guardada seq=13 temp=40.00 corriente=4.90 pendientes=1
HTTP 201: seq=13 confirmada; pendientes=0
Latido MQTT enviado: seq=13 pendientes=1
Guardada seq=14 temp=39.93 corriente=3.75 pendientes=1
HTTP 201: seq=14 confirmada; pendientes=0
Guardada seq=15 temp=40.50 corriente=4.75 pendientes=1
HTTP 201: seq=15 confirmada; pendientes=0
Latido MQTT enviado: seq=15 pendientes=1
Envio detenido; pendientes=0 conservadas en SQLite.
~~~

Hubo nueve mediciones confirmadas y cinco publicaciones MQTT. El valor de
pendientes en cada latido es la instantanea tomada al preparar ese mensaje;
la cola final quedo en cero.

## Registro del servidor

Despues consulte GET /api/v1/mi-progreso usando el token del entorno:

~~~text
nivel0=5/5
nivel1=10/10
nivel2=15/15
nivel3=0/15
nivel4=10/10
nivel5=0/15
total=40/70
M1=4
M2=exacto
M3=24/24
M4=1
M5=100.0%
M6=0/0
M7=0
M8=100.0%
M9=-
M10=-
M11=0.0%
~~~

La consulta confirmo que los latidos fueron registrados y aceptados.
Las etiquetas M1 a M11 de esta respuesta son metricas de evaluacion.
La sesion larga de telemetria, la recuperacion durante caidas del servidor
del curso y la recepcion y confirmacion de alarmas siguen pendientes.
