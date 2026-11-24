# Validacion del analisis

Fecha: 24 de noviembre de 2026.
Codigo probado: e78804eb865b86e44ce6c344771cdf99e26f7938.

## Comprobacion en Linux

La compilacion en Ubuntu termino sin advertencias con
-std=c11 -Wall -Wextra -Wpedantic -Werror. Pasaron las 19 pruebas locales,
que usan datos inventados y un servidor HTTP en 127.0.0.1.

[Resultado de GitHub Actions](https://github.com/eliezerdsantana/estacion-itla-20250442/actions/runs/45403713493)

## Informe local en Windows

Ejecute el programa desde MSYS2 UCRT64 con la configuracion y las 2000
lecturas que ya estaban guardadas en estacion.db:

```text
./estacion.exe analizar
umbral_temp=74.23
maquina,validas,descartadas,temp_min,temp_max,temp_prom,sobre_umbral
M1,486,14,35.16,81.50,55.60,9
M2,481,19,36.14,82.90,61.24,38
M3,486,14,35.11,78.59,54.25,4
M4,478,22,48.62,84.98,66.58,63
Analisis local: no se envio una respuesta al servidor.
```

Cada maquina suma 500 lecturas entre validas y descartadas. En total hay
1931 validas, 69 descartadas y 114 validas por encima del umbral.

## Confirmacion del servidor del curso

Despues ejecute una vez ./estacion.exe analizar --enviar. El servidor devolvio:

```text
M1.validas=ok
M1.descartadas=ok
M1.temp_min=ok
M1.temp_max=ok
M1.temp_prom=ok
M1.sobre_umbral=ok
M2.validas=ok
M2.descartadas=ok
M2.temp_min=ok
M2.temp_max=ok
M2.temp_prom=ok
M2.sobre_umbral=ok
M3.validas=ok
M3.descartadas=ok
M3.temp_min=ok
M3.temp_max=ok
M3.temp_prom=ok
M3.sobre_umbral=ok
M4.validas=ok
M4.descartadas=ok
M4.temp_min=ok
M4.temp_max=ok
M4.temp_prom=ok
M4.sobre_umbral=ok
intentos_restantes=9
Analisis confirmado: 24 de 24 campos.
```

Los 24 valores fueron aceptados al primer intento. Esta comprobacion
corresponde al analisis; los niveles de telemetria, latidos y alarmas
todavia estan pendientes.
