# Plan de trabajo

Entrega: antes del jueves 26 de noviembre de 2026 a las 00:00, hora de
Santo Domingo. Clase: miercoles 25 a las 18:00.

## Base terminada el lunes 23

- [x] Preparar MSYS2 UCRT64, GCC, Make y libcurl con MQTT.
- [x] Comprobar la conexion y la autenticacion.
- [x] Crear el proyecto y añadir SQLite.
- [x] Implementar config y comprobar su guardado en SQLite.
- [x] Compilar y probar en Windows.
- [x] Subir el primer commit a GitHub: f7ec304.
- [x] Revisar la muestra CSV y repasar main.c.

## Checklist del martes 24

- [x] Recuperar la sesion y comprobar config.
- [x] Implementar, probar y subir descargar. Commit 71884a1.
- [x] Implementar, probar y subir analizar. Servidor: 24/24 al primer intento.
- [ ] Preparar la cola de telemetria y sus reintentos.
- [ ] Añadir los latidos MQTT.
- [ ] Añadir la recepcion y confirmacion de alarmas.
- [ ] Probar la sesion completa y guardar sus resultados.

Cada casilla se marca cuando tenemos su comprobacion. Cada avance funcional
probado se guarda en un commit propio.

## Punto actual

La descarga fue aceptada por el servidor: 2000 filas unicas y suma de IDs
7615032, correcto=1. Se subio el commit 71884a1 y el equipo quedo limpio.

El analisis del codigo e78804e se probo en Linux y Windows. El servidor del
curso confirmo los 24 campos en el primer intento, con intentos_restantes=9.
Las cuatro maquinas suman 1931 lecturas validas y 69 descartadas.
La evidencia esta en pruebas/validacion_analisis.md.

El siguiente avance es guardar cada lectura propia antes de intentar enviarla
y recuperar las pendientes cuando vuelva a funcionar la conexion.

La cola y los reintentos estan preparados en avance-telemetria. Compilaron
en Linux sin advertencias y pasaron 17 casos nuevos, ademas de los 19
anteriores. La casilla sigue pendiente hasta la prueba corta en Windows
y la incorporacion del avance probado a main. Los latidos y las alarmas
se añaden despues, antes de iniciar la sesion completa de evaluacion.

## Metodo de estudio

Primero describo la intencion en español. Despues leo como se expresa en C,
compruebo el orden de las acciones y practico una variacion pequeña.
