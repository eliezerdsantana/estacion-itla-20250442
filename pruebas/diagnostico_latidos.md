# Diagnostico de los latidos

El envio muestra un resumen por ejecucion:

- turnos: veces que correspondio preparar un latido.
- enviados: publicaciones que libcurl termino sin error.
- descartados: fallos al preparar o completar una publicacion.
- omitidos: turnos en que la publicacion anterior seguia ocupada.
- sin_resultado: turnos que no llegaron a tener un resultado recogido al cerrar.

La suma de enviados, descartados, omitidos y sin_resultado coincide con turnos.
Los enviados son un resultado local: MQTT QoS 0 no entrega un PUBACK y la
recepcion remota se comprueba con las metricas del servidor.

Cuando falla una publicacion se imprimen el codigo de libcurl y tres tiempos,
en milisegundos, consultados antes de retirar su manejador del multi:

| Campo | Significado |
| --- | --- |
| total_ms | Tiempo total de la transferencia que termino. |
| dns_ms | Tiempo desde el inicio hasta finalizar la resolucion del nombre. |
| conexion_ms | Tiempo desde el inicio hasta establecer la conexion TCP. |

Los valores son tiempos desde el mismo inicio; no se suman. Un valor positivo
de conexion_ms permite saber que TCP llego a establecerse. Con ello se puede
distinguir un fallo anterior a la conexion de uno en una fase posterior.
Un valor cero debe interpretarse junto con los otros tiempos y el error.
Si alguna consulta de tiempos falla, se informa tiempos_no_disponibles=1.

La frecuencia procede de latido_s. Cada publicacion MQTT dispone de hasta
el 80% de su periodo, con un maximo de 8 segundos. La conexion tiene un limite
de 5 segundos o el plazo total si este es menor. Con un periodo de 10 segundos
quedan 2 segundos de margen antes de preparar el siguiente latido.

Esto permite terminar una respuesta MQTT demorada dentro de su turno. Si se
agota el plazo, se descarta ese latido y el siguiente prepara los datos
actuales. La cadencia sigue procediendo del periodo configurado.
Las peticiones HTTP mantienen sus limites de 3 segundos para conectar y
5 segundos en total, independientemente del presupuesto MQTT.

## Comprobacion breve en MSYS2 UCRT64

Primero se actualiza y compila el programa con ambos procesos detenidos.
Las dos ventanas deben estar en la carpeta del proyecto y tener cargadas sus
variables de entorno. Se inicia escuchar y, enseguida, enviar:

~~~bash
./estacion.exe escuchar --duracion 200
~~~

En la otra ventana:

~~~bash
./estacion.exe enviar --duracion 180 2>&1 | tee diagnostico-latidos.log
~~~

El envio dura tres minutos y muestra el resumen al terminar. La salida tambien
queda en diagnostico-latidos.log, excluido de Git. Se revisan el resumen y las
lineas Fallo de latido MQTT para escoger una correccion con evidencia.

## Pruebas locales

pruebas/verificar_latidos.py compara el contador de enviados con las
publicaciones recibidas por su broker local y comprueba la suma del resumen.
El caso de respuesta MQTT demorada comprueba que se informa el codigo 28,
el tiempo total agotado y el tiempo de la conexion TCP que si se establecio.
Otros dos casos configuran un periodo de diez segundos: una respuesta demorada
seis segundos debe llegar en su turno; una demora de diez segundos debe vencer
a los ocho, permitiendo publicar datos actuales en el siguiente turno.
En ambos casos siguen generandose y enviandose las mediciones HTTP.
Las pruebas de alarmas comprueban tambien el manejador multi compartido.

Referencias: [tiempo total de libcurl](https://curl.se/libcurl/c/CURLINFO_TOTAL_TIME_T.html),
[tiempo de conexion](https://curl.se/libcurl/c/CURLINFO_CONNECT_TIME_T.html)
y [tiempo de resolucion](https://curl.se/libcurl/c/CURLINFO_NAMELOOKUP_TIME_T.html).
