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

## Resultado del 24 de noviembre de 2026

Los cinco casos pasaron en
[GitHub Actions](https://github.com/eliezerdsantana/estacion-itla-20250442/actions/runs/45474690773).
Se analizaron once ejecuciones del cliente C. Los once informes indicaron:

~~~text
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
~~~

El resultado corresponde a las rutas ejecutadas por estos casos. El codigo
C es el mismo de la revision funcional 34ca815; la revision 8b4b8ff incorpora
el procedimiento y su ejecucion automatica en Linux.
