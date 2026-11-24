# Comprobacion de memoria en Linux

Despues de compilar y tener Valgrind instalado:

~~~bash
python3 pruebas/verificar_memoria.py
~~~

La comprobacion reutiliza cinco casos funcionales con servidores locales y
una credencial aleatoria. Cada ejecucion del cliente C pasa por Memcheck.
Cubre config, descarga y analisis, la cola HTTP durante dos caidas, respuesta
perdida y confirmacion duplicada, reconexion MQTT, alarmas retenidas y ACK 409.

Valgrind informa lecturas o escrituras invalidas, valores sin inicializar y
fugas. Se usa error-exitcode=99 y se consideran errores las fugas definitivas
e indirectas. Los informes tambien muestran los bytes posiblemente perdidos
y los que siguen accesibles, sin ocultar esas categorias.

Las ventanas del simulador empiezan en el primer POST para que la demora de
arranque de Valgrind no cambie el escenario de las dos caidas. La aplicacion
C mantiene sus propios intervalos y limites de red.

Los informes temporales se resumen en la salida de GitHub Actions. Una
comprobacion correcta de estas rutas no demuestra la ausencia de errores
en todos los caminos posibles del programa.

[Manual de Memcheck](https://valgrind.org/docs/manual/mc-manual.html).
