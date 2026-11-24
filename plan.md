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
- [ ] Implementar, probar y subir descargar.
- [ ] Implementar, probar y subir analizar.
- [ ] Preparar la cola de telemetria y sus reintentos.
- [ ] Añadir los latidos MQTT.
- [ ] Añadir la recepcion y confirmacion de alarmas.
- [ ] Probar la sesion completa y guardar sus resultados.

Cada casilla se marca cuando tenemos su comprobacion. Cada avance funcional
probado se guarda en un commit propio.

## Punto actual

La version de descargar esta preparada para probarla en el equipo con el
servidor del curso. La casilla sigue pendiente hasta completar esa prueba y
subir el siguiente commit.

## Metodo de estudio

Primero describo la intencion en español. Despues leo como se expresa en C,
compruebo el orden de las acciones y practico una variacion pequeña.
