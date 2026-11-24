# Prueba inicial del envio

Fecha: 24 de noviembre de 2026.
Codigo probado: a2a27221cf8ae58ca5c34ccc31bfe07067a508e7.

## Comprobaciones automaticas

El codigo compilo sin advertencias con -Wall -Wextra -Wpedantic -Werror
en Linux y en Windows con MSYS2 UCRT64.

Linux: 19 pruebas anteriores y 17 pruebas de envio correctas.
Windows: 19 anteriores y 16 de envio correctas. La prueba de SIGINT por
proceso solo se ejecuta en Linux; en Windows quedo omitida.

[Resultado de GitHub Actions](https://github.com/eliezerdsantana/estacion-itla-20250442/actions/runs/45417169510)

Los casos locales comprueban, entre otras cosas, dos caidas, Retry-After,
recuperacion ordenada de la cola y reinicio sin perder la numeracion.

## Ejecucion en el equipo

Despues de corregir el arranque de Windows, compile el cambio de envio.c
y ejecute desde MSYS2 UCRT64:

~~~bash
make run ARGS="enviar --duracion 30"
~~~

Salida recibida:

~~~text
./estacion.exe enviar --duracion 30
Telemetria: intervalo_s=5; pendientes=0. Ctrl+C para detener.
Guardada seq=1 temp=34.02 corriente=4.92 pendientes=1
HTTP 201: seq=1 confirmada; pendientes=0
Guardada seq=2 temp=34.84 corriente=2.88 pendientes=1
HTTP 201: seq=2 confirmada; pendientes=0
Guardada seq=3 temp=35.76 corriente=2.83 pendientes=1
HTTP 201: seq=3 confirmada; pendientes=0
Guardada seq=4 temp=37.51 corriente=3.30 pendientes=1
HTTP 201: seq=4 confirmada; pendientes=0
Guardada seq=5 temp=37.42 corriente=4.17 pendientes=1
HTTP 201: seq=5 confirmada; pendientes=0
Guardada seq=6 temp=39.11 corriente=2.26 pendientes=1
HTTP 201: seq=6 confirmada; pendientes=0
Envio detenido; pendientes=0 conservadas en SQLite.
~~~

Se generaron seis lecturas, se confirmaron las seis y quedaron cero pendientes.
El programa termino por el limite de duracion.

## Pendiente de evaluar

La corrida de 30 segundos comprueba la comunicacion y el funcionamiento inicial.
La sesion continua de 15 minutos o mas, con las caidas del servidor del curso,
los latidos y las alarmas, sigue pendiente.
