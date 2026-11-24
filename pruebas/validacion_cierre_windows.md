# Cierre manual de escuchar en Windows

Fecha: 24 de noviembre de 2026. Entorno: MSYS2 UCRT64.

La compilacion del programa en el equipo termino sin errores ni advertencias.
Ejecute el subcomando sin limite de tiempo:

~~~bash
./estacion.exe escuchar
~~~

Tras aparecer los mensajes de arranque, pulse Ctrl+C una sola vez. El programa
mostro su mensaje local de cierre:

~~~text
Escucha detenida; alarmas conservadas en SQLite.
~~~

La terminal volvio a mostrar el simbolo $ y permitio escribir otra orden.
Esta comprobacion corresponde al cierre de escuchar con Ctrl+C en la consola
de Windows; no sustituye las pruebas de recepcion, ACK ni de sesion completa.

Este registro contiene solamente la comprobacion del cliente local. No incluye
credenciales, respuestas ni metadatos del servidor del curso.
