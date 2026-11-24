# Validacion final de la parte automatica

Fecha: 24 de noviembre de 2026.
Entorno de ejecucion: Windows, MSYS2 UCRT64.
Revision funcional: 34ca81515bd98484eefd6dd915c767bd93b6cbca.

## Ejecucion

El programa se compilo con C11, -Wall, -Wextra y -Wpedantic sin advertencias.
Se indicaron estos comandos en dos ventanas del mismo proyecto:

~~~bash
./estacion.exe escuchar --duracion 1320
~~~

~~~bash
./estacion.exe enviar --duracion 1200 2>&1 | tee diagnostico-latidos.log
~~~

La salida local del envio termino con este resumen:

~~~text
Resumen de latidos: turnos=120 enviados=120 descartados=0 omitidos=0 sin_resultado=0
~~~

Ese resumen describe el resultado local de libcurl. La recepcion remota se
comprobo despues mediante GET /api/v1/mi-progreso.

## Respuesta del servidor

~~~text
nivel0=5/5
nivel1=10/10
nivel2=15/15
nivel3=15/15
nivel4=10/10
nivel5=15/15
total=70/70
M1=4
M2=exacto
M3=24/24
M4=1
M5=100.0%
M6=2/2
M7=0
M8=100.0%
M9=100.0%
M10=0.21s
M11=0.0%
~~~

M5 confirma la completitud de telemetria y M6 la recuperacion en las dos
ventanas de caida. M7 registra cero reintentos adelantados. M8 y M9 muestran
disponibilidad de latidos y cobertura de alarmas completas; la mediana de
latencia de alarma fue de 0.21 segundos. M11 registra 0.0% de respuestas
HTTP 400 o 429 en la metrica evaluada.

Los 70 puntos corresponden a la parte automatica. La revision del codigo
y la defensa oral son evaluaciones del profesor.

## Comprobaciones del codigo

Las cuatro suites funcionales del codigo probado pasaron en
[GitHub Actions](https://github.com/eliezerdsantana/estacion-itla-20250442/actions/runs/45463677016):
64 casos aprobados en Linux y 62 aprobados en Windows, con dos casos de
SIGINT omitidos por depender de la consola. El cierre de escuchar con Ctrl+C
tambien se comprobo manualmente en Windows.

Los limites de publicacion MQTT se adaptan al periodo del latido: con diez
segundos de periodo, hasta cinco para conectar y ocho para la operacion.
Las peticiones HTTP mantienen sus propios limites de tres y cinco segundos.
