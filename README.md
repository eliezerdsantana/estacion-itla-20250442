# Estacion de Telemetria ITLA

Eliezer Daniels Santana. Matricula: 2025-0442.
Programacion para Mecatronicos, seccion de los miercoles.

## Avance actual

Ya estan implementados `config`, `descargar`, `analizar`, `enviar` y `escuchar`.

`config` pide la configuracion al servidor, comprueba la respuesta HTTP y guarda
los siete campos en SQLite. Despues imprime los valores consultados de la base.

`descargar` pide las lecturas por paginas, guarda una sola fila por ID y calcula
el numero de filas y la suma de IDs con SQL. Envia esos resultados al servidor
y comprueba que la respuesta contenga la linea `correcto=1`.

El analisis ya fue aceptado por el servidor del curso. El avance de enviar
incluye sensor simulado, cola persistente y reintentos. La prueba corta en
Windows confirmo las seis lecturas generadas, con cero pendientes al terminar.

El envio incluye latidos MQTT y escuchar recibe, guarda y confirma las alarmas.
La comprobacion final del 24 de noviembre alcanzo 70/70 puntos automaticos.
La revision del codigo y la defensa corresponden a la evaluacion del profesor.
La respuesta completa esta en [la validacion final](pruebas/validacion_final.md).

Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442

## Archivos

- `src/main.c`: lee el subcomando y las variables de entorno.
- `src/red.c` y `src/red.h`: HTTP, MQTT, autenticacion y acumulacion de fragmentos.
- `src/datos.c` y `src/datos.h`: tablas, transacciones y sentencias de SQLite.
- `src/descarga.c` y `src/descarga.h`: lectura del CSV, paginacion y comprobacion
  de la descarga.
- `lib/sqlite/sqlite3.c` y `sqlite3.h`: archivos originales de SQLite 3.54.1.
- `Makefile`: compila el programa y SQLite por separado.
- `plan.md`: checklist del trabajo y del avance probado.
- `src/envio.c` y `src/envio.h`: alternan el muestreo y el envio de la cola.
- `src/escucha.c` y `src/escucha.h`: interpretan alarmas y coordinan su ACK HTTP.
- `src/sensor.c` y `src/sensor.h`: generan las mediciones simuladas.
- `src/tiempo.c` y `src/tiempo.h`: reloj monotono de Linux y Windows.
- `pruebas/verificar_envio.py`: pruebas de persistencia, reintentos y cierre.

Los archivos originales de SQLite se copian de `sqlite-amalgamation-3540100.zip`
en la [pagina oficial](https://www.sqlite.org/download.html).
La copia de trabajo usa esa version, sin modificar su codigo.

## Compilacion

En Linux hacen falta GCC, Make y los archivos de desarrollo de libcurl.
En Windows uso la terminal MSYS2 UCRT64 con GCC, Make y
`mingw-w64-ucrt-x86_64-curl`.

Desde la carpeta del proyecto:

```bash
make
```

SQLite se compila a `build/sqlite3.o`. El ejecutable se llama `estacion` en Linux
y `estacion.exe` en Windows.

## Configuracion y uso

En MSYS2 UCRT64 o Bash, primero defino el host y la matricula:

```bash
export ITLA_HOST="itla-meca.duckdns.org"
export ITLA_MATRICULA="20250442"
```

Introduzco el token de forma oculta. Al ejecutar la siguiente linea, la terminal
espera la clave: la pego y pulso Enter. El texto `ITLA_TOKEN` se copia tal como
aparece en el comando.

```bash
read -r -s ITLA_TOKEN
```

Cuando vuelve la linea para escribir:

```bash
export ITLA_TOKEN
make run
```

`make run` ejecuta el subcomando `config`. Tambien puedo usar
`./estacion.exe config` en Windows o `./estacion config` en Linux.

Despues de obtener la configuracion, ejecuto la descarga:

```bash
make run ARGS=descargar
```

Tambien puedo ejecutar `./estacion.exe descargar` en Windows o
`./estacion descargar` en Linux. Al terminar muestra el resumen y la respuesta
del servidor. Un estado HTTP 200 por si solo no confirma la integridad: tambien
se comprueba `correcto=1`.

Las variables pertenecen a esa terminal y sus programas hijos. Si abro otra
terminal, tengo que cargarlas de nuevo.

Para borrar los archivos de compilacion:

```bash
make clean
```

La base de datos se conserva al limpiar. El ejecutable, `build/`, las bases de
datos y los archivos `.env` quedan excluidos de Git.

## Decisiones de esta version

- El host, la matricula y el token se leen con `getenv`; la clave no se imprime.
- El manejador de libcurl se reutiliza y se libera al terminar.
- Bearer se configura con `CURLOPT_XOAUTH2_BEARER` y
  `CURLOPT_HTTPAUTH` con `CURLAUTH_BEARER`, como pide el mandato.
- HTTP tiene un limite de 3 segundos para conectar y de 5 segundos en total.
  La suscripcion MQTT es continua, con limite de conexion de 3 segundos.
- Se revisan el resultado de libcurl y el codigo HTTP por separado.
- El callback recibe fragmentos y evita escribir fuera del buffer.
- SQLite usa WAL y espera hasta 2000 ms si la base esta ocupada.
- Se crean las cuatro tablas del mandato. Una tabla auxiliar registra los
  codigos de alarma que el servidor rechazo con 404, sin inventar su ACK.
- Los valores recibidos se guardan con `prepare`, `bind`, `step` y `finalize`.
- La transaccion se revierte si la configuracion esta incompleta, repetida
  o pertenece a otra matricula.

## La descarga explicada en palabras

1. Compruebo que la matricula de la configuracion coincide con la del entorno.
2. Inicio una transaccion para preparar una nueva copia de las lecturas.
3. Empiezo desde el ID 0 y pido hasta 200 filas.
4. Separo las columnas conservando los campos vacios entre dos comas.
5. Guardo cada fila con `INSERT OR IGNORE`. Si su ID ya existe, no se duplica.
6. Si llegan 200 filas o mas, vuelvo a pedir desde el mayor ID recibido.
7. Cuando llegan menos de 200 filas, calculo `COUNT(*)` y `SUM(id)` con SQLite.
8. Confirmo la transaccion y envio esos dos valores en un JSON.
9. Compruebo que el servidor confirme la integridad de la descarga.

La copia anterior se reemplaza dentro de la transaccion. Si falla la descarga
antes de confirmar, `ROLLBACK` recupera esa copia anterior. No se borran las
tablas de configuracion, telemetria ni alarmas.

Las lecturas con temperatura -999 o campos ausentes tambien se guardan: hacen
falta para contar las descartadas en el analisis. Los campos ausentes se guardan
como NULL. Un ID ausente detiene la descarga, porque no permite identificar ni
deduplicar la fila con seguridad.

El buffer HTTP admite 64 KiB. Hay una pausa de 250 ms entre paginas y antes de
enviar la comprobacion. La cantidad de filas y la suma de IDs se obtienen de la
base; los valores del ejemplo del mandato no se usan como respuestas fijas.

La descarga se realiza antes de empezar la sesion de enviar y escuchar, para
completar esta transaccion de preparacion antes de los procesos continuos.

## Comprobaciones preparatorias

La version compila sin advertencias en Linux con los archivos de SQLite
incluidos en el proyecto. En la preparacion con ChatGPT se ejecutaron las
12 comprobaciones de config y 16 de descarga contra servidores locales de
prueba. Estas cubrieron 2000 filas, IDs con saltos, paginas repetidas, campos
ausentes, errores de red y recuperacion de la copia anterior con ROLLBACK.

Config y descargar tambien se probaron en Windows. El servidor del curso
confirmo 2000 filas unicas y suma_ids=7615032 con correcto=1 antes de subir
el commit 71884a1.

## Apoyo utilizado

Use ChatGPT para organizar los pasos, proponer esta base de codigo, explicar
las funciones y preparar comprobaciones de compilacion y errores. Voy revisando
el codigo por partes para poder explicar su funcionamiento en la defensa.

## Fuentes

- Mandato de la practica y correo del profesor Wilkins Cedano.
- [Documentacion de libcurl](https://curl.se/libcurl/c/).
- [Documentacion de SQLite en C](https://www.sqlite.org/cintro.html).
- [Paquetes y herramientas de MSYS2](https://www.msys2.org/docs/package-management/).

## Proposito del programa

Esta estacion recibe las mediciones de cuatro maquinas, las conserva en SQLite
y prepara un informe por maquina. Tambien envia lecturas propias, publica
latidos y confirma las alarmas que recibe del servidor.

## Analisis por maquina

El modulo src/analisis.c coordina el informe y su envio opcional. Todos los
calculos SQLite siguen en src/datos.c, con su interfaz en src/datos.h.

Para ver el informe sin consumir intentos de evaluacion:

~~~bash
make run ARGS=analizar
~~~

En Windows tambien sirve ./estacion.exe analizar. El informe local usa la
configuracion y las lecturas guardadas; no hace ninguna peticion HTTP.

La consulta SQL clasifica cada fila. Es valida si todos sus campos estan
presentes, estado es exactamente OK y temp es diferente de -999.0. No se
borran las filas invalidas: se cuentan como descartadas de su maquina.

SQL calcula validas, descartadas, temp_min, temp_max, temp_prom y sobre_umbral.
El promedio se redondea a dos decimales. Sobre el umbral significa estrictamente
temp > umbral_temp; una temperatura igual al umbral no se cuenta.

Una maquina sin lecturas validas conserva NULL en sus temperaturas. No se
inventan ceros para sustituirlas, ni se envia una evaluacion en esa situacion.
Tambien se evita enviar si faltan lecturas para completar las 2000, si aparece
una maquina desconocida o si la configuracion pertenece a otra matricula.

Despues de revisar el informe local, una evaluacion se solicita explicitamente:

~~~bash
make run ARGS="analizar --enviar"
~~~

Ese comando envia las cuatro maquinas en un solo JSON. No reintenta el POST:
cada evaluacion puede consumir un intento. HTTP 200 por si solo no basta;
se exigen las 24 lineas distintas de confirmacion con valor exactamente ok.
El servidor permite como maximo diez intentos y la meta es acertar en tres
o menos.

## Pruebas del analisis

Las 19 pruebas de pruebas/verificar_analisis.py crean una base temporal y un
servidor HTTP en 127.0.0.1. Usan una credencial aleatoria exclusiva de prueba:
nunca necesitan el token personal ni contactan el servidor del profesor.

Un calculo independiente en Python comprueba los seis campos de cada maquina.
Los casos incluyen campos ausentes, temperatura -999 aunque estado sea OK,
estados con mayusculas o espacios diferentes, valores cero, temperaturas
iguales al umbral, respuestas parciales y fallos de transporte. Tambien
comprueban que analizar sin --enviar no haga peticiones y que un fallo de
evaluacion no produzca reintentos automaticos. Hay regresiones de config y
descargar junto con el nuevo analisis.

Despues de compilar, se ejecutan en Linux con:

~~~bash
python3 pruebas/verificar_analisis.py
~~~

GitHub Actions ejecuta la compilacion con -Werror y las pruebas en Ubuntu y
Windows con MSYS2 UCRT64.

## Validacion del analisis con el servidor

El 24 de noviembre ejecute el informe local en Windows y despues lo envie
una sola vez. El servidor confirmo los 24 campos y respondio
intentos_restantes=9: el analisis quedo aceptado al primer intento.

Las cuatro maquinas tienen 500 lecturas cada una. El informe suma 1931 validas
y 69 descartadas; 114 lecturas validas superan el umbral de 74.23.

La salida y la comprobacion de Linux estan en
[la validacion del analisis](pruebas/validacion_analisis.md).

## Envio de mediciones

Una lectura propia contiene seq, ts, temp y corriente. seq es el numero de
medicion y ts indica cuando se tomo; son datos distintos. El sensor simulado
produce temperatura de 25 a 45 C con una onda lenta mas ruido y corriente
de 2 a 5 A.

Primero se guarda cada medicion en la tabla telemetria con enviado=0. La
sentencia SQL asigna MAX(seq)+1; la numeracion continua al reiniciar porque
las filas permanecen en SQLite. Despues se consulta la pendiente con menor seq
y se intenta entregarla a POST /api/v1/telemetria.

La fila solo se marca enviado=1 cuando llega HTTP 201 con estado=creada o
HTTP 200 con estado=duplicada. Si el servidor recibio la fila y se perdio la
respuesta, el siguiente intento usa la misma secuencia y el mismo timestamp.
La confirmacion de duplicada permite terminar ese envio sin crear otra fila.

Para una primera comprobacion corta:

~~~bash
make run ARGS="enviar --duracion 30"
~~~

Con el intervalo de 5 segundos se generan normalmente seis lecturas durante
esos 30 segundos. El comando termina por si mismo y conserva las pendientes.
Tambien se puede ejecutar sin limite:

~~~bash
make run ARGS=enviar
~~~

Ctrl+C solicita el cierre. El manejador de la senal solo marca una variable;
el bucle sale, cierra la red y conserva las lecturas que falten por confirmar.

## Esperas y recuperacion de la cola

La interfaz multi de libcurl permite avanzar el HTTP sin bloquear la tarea
que genera mediciones. El bucle usa esperas de hasta 100 ms, mientras cada
peticion mantiene sus limites de 3 segundos para conectar y 5 en total.

Ante HTTP 503 o 429 se respeta Retry-After, tanto en segundos como con una
fecha HTTP. Mientras vence ese plazo siguen guardandose las nuevas lecturas.
Ante errores de transporte o respuestas transitorias sin plazo se espera
1, 2, 4, 8, 16 y hasta 30 segundos. Una confirmacion reinicia esa espera.
Entre envios aceptados hay una pausa de al menos 250 ms para respetar el ritmo.

Las filas se mantienen aunque se cierre el programa. Al volver a ejecutarlo
se consultan otra vez las pendientes por orden de seq. No se inventan
mediciones correspondientes al tiempo en que el equipo estuvo suspendido.
Un error persistente de acceso o datos, como HTTP 401 o 400, termina con
un mensaje y mantiene la cola para corregir el problema.

El reloj monotono organiza los intervalos y los reintentos. Los timestamps
guardados usan la fecha del sistema en segundos desde Epoch.

## Pruebas del envio

Se comprobaron 17 casos locales: guardado anterior al POST, dos caidas con
Retry-After, recuperacion ordenada, respuesta perdida y confirmacion duplicada,
reintentos de 1/2/4 segundos, HTTP 429, Retry-After con fecha, cabeceras
interinas, reinicio, Ctrl+C durante una respuesta lenta, respuesta excesiva,
secuencias de 64 bits, configuracion invalida y limites del sensor.

~~~bash
python3 pruebas/verificar_analisis.py
python3 pruebas/verificar_envio.py
~~~

Las 19 comprobaciones anteriores tambien siguen pasando. Estas pruebas solo
usan 127.0.0.1, bases temporales y una credencial aleatoria de prueba.

El envio tambien publica latidos. escuchar recibe y confirma las alarmas.
La evaluacion de los niveles 3 a 5 requiere la sesion continua de 15 minutos
o mas con todos esos componentes.

El 24 de noviembre ejecute enviar durante 30 segundos en Windows. Las seis
mediciones se guardaron y el servidor del curso confirmo todas con HTTP 201.
El cierre automatico termino con pendientes=0. La salida completa esta en
[la validacion inicial del envio](pruebas/validacion_envio_inicial.md).

## Compatibilidad del arranque en Windows

La primera comprobacion en Windows encontro una salida silenciosa con codigo
1 antes de iniciar la telemetria. La llamada setvbuf usaba _IOLBF con un
tamano de cero, que no es valido para ese modo en el runtime de Windows.

La salida usa ahora _IONBF, que muestra los mensajes inmediatamente sin
necesitar un buffer. Los errores al preparar la salida o los manejadores de
senales tambien muestran un diagnostico antes de terminar.

La comprobacion automatica añade Windows con MSYS2 UCRT64, ademas de Linux.
Los servidores locales de prueba cierran explicitamente sus conexiones
SQLite para que Windows pueda liberar los archivos temporales.

[Referencia de setvbuf de Microsoft](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setvbuf)

## Latidos MQTT

Durante enviar se publica un aviso cada latido_s segundos, segun config.
Con la configuracion del curso son diez segundos. El topico es
itla/estudiantes/<MATRICULA>/latido, en el mismo host, puerto 1883.
El usuario es la matricula y la contraseña es el mismo token del entorno.
La clave no se agrega a la URL ni se imprime.

El texto tiene este formato y termina en salto de linea:

~~~text
LATIDO;<seq>;<pendientes>;<ts>
~~~

seq indica la ultima medicion generada. pendientes es cuantas lecturas
esperan confirmacion HTTP, y ts indica el momento de preparar ese latido.
Estos valores son una instantanea: una confirmacion HTTP puede llegar despues
de prepararlos y antes de que aparezca la linea del latido en la terminal.

HTTP y MQTT tienen manejadores separados dentro de la misma interfaz multi.
Mientras HTTP espera Retry-After, las mediciones siguen guardandose y los
latidos siguen publicandose. Una falla de MQTT tampoco detiene el envio HTTP.

Los latidos usan QoS 0. Si una publicacion falla, se descarta: la siguiente
usa la secuencia, pendientes y fecha actuales. No se guardan latidos en la
cola de SQLite ni se reenvian los que se perdieron. Si una publicacion
sigue activa cuando toca otra, se omite ese turno.

Cada publicacion dispone de hasta el 80% de su periodo, con un maximo de
8 segundos. Puede usar hasta 5 segundos para conectar, limitado tambien por
ese plazo total. Con latido_s=10 quedan 2 segundos de margen antes del
siguiente turno. HTTP mantiene sus limites propios de 3 y 5 segundos.

Latido MQTT enviado significa que libcurl termino la publicacion sin error.
QoS 0 no entrega un PUBACK; la recepcion que registra el servidor se consulta
en mi-progreso durante la evaluacion completa.

Para una comprobacion corta de HTTP y MQTT:

~~~bash
make run ARGS="enviar --duracion 45"
~~~

Con intervalo_s=5 y latido_s=10, normalmente se generan nueve mediciones y
cinco latidos durante esos 45 segundos. La numeracion continua desde la
ultima medicion que ya estaba guardada.

## Pruebas de los latidos

~~~bash
python3 pruebas/verificar_latidos.py
~~~

Doce casos usan el servidor HTTP local y un broker MQTT minimo, implementado
con la biblioteca estandar de Python. El broker decodifica CONNECT y PUBLISH
para comprobar usuario, clave de prueba, topico, QoS 0 y formato del mensaje.
Nunca usa la contraseña real ni contacta el servidor del profesor.

Los casos comprueban el periodo configurado, los latidos durante dos caidas
HTTP, una respuesta HTTP lenta, rechazo o cierre de MQTT, recuperacion sin
repetir un latido antiguo, reinicio con secuencia persistente y validacion
de la configuracion. Tambien se comprueba que una respuesta MQTT demorada
seis segundos termine dentro de un turno de diez y que una demora excesiva
venza antes de preparar el siguiente latido. Las pruebas se ejecutan en
Linux y Windows. Los contadores y los tiempos se explican en
[la guia de diagnostico](pruebas/diagnostico_latidos.md).

[Formato MQTT de curl](https://everything.curl.dev/usingcurl/mqtt.html)

## Validacion inicial de los latidos

El 24 de noviembre ejecute enviar durante 45 segundos en Windows. Las nueve
mediciones, de seq=7 a seq=15, recibieron HTTP 201; se publicaron cinco latidos
y el proceso termino con pendientes=0.

Despues consulte mi-progreso. El servidor asigno nivel4=10/10 y M8=100.0%.
El resultado de esa comprobacion fue total=40/70 en la parte automatica.
Esa salida corresponde a la comprobacion inicial, anterior a la sesion final.

La salida y la respuesta del servidor estan en
[la validacion de los latidos](pruebas/validacion_latidos.md).

## Recepcion y confirmacion de alarmas

En otra terminal ejecuto escuchar. Uso la misma carpeta y estacion.db, pero
cargo tambien las variables de entorno en esa terminal. enviar sigue activo:
el servidor solo genera alarmas cuando ha recibido un latido en los ultimos
60 segundos.

~~~bash
make run ARGS=escuchar
~~~

Tambien existe un limite opcional para las pruebas:

~~~bash
make run ARGS="escuchar --duracion 90"
~~~

escuchar se suscribe a itla/planta/<MATRICULA>/alarma. El callback de libcurl
entrega bytes: dos indican el largo del topico, luego vienen el topico y la
linea de texto. El acumulador conserva los fragmentos incompletos y extrae
todos los mensajes completos, aunque lleguen varios juntos. Acepta lineas
de menos de 100 bytes, terminadas en salto de linea. Un mensaje excesivo o
con bytes nulos cancela esa conexion para recuperarla mediante reconexion.

NINGUNA;<ts> se ignora. Para ALARMA;<codigo>;<maquina>;<ts>, primero se guarda
una fila en alarmas. codigo es su clave primaria: recibirlo de nuevo conserva
la primera recepcion y no crea otra fila. Una alarma ya confirmada tampoco
genera otro POST.

Las alarmas pendientes se consultan en SQLite. El ACK usa un manejador HTTP
distinto del de la suscripcion, dentro del mismo multi; por eso una respuesta
HTTP lenta no detiene la recepcion MQTT. Se envia a /api/v1/alarmas/ack el
JSON con codigo escapado correctamente. HTTP 200 confirma, y HTTP 409 permite
registrar una confirmacion que el servidor ya tenia. Solo entonces se escribe
ts_ack; no se confunde la recepcion local con la confirmacion remota.

Los fallos de red conservan las pendientes y usan espera creciente de hasta
30 segundos. HTTP 503 y 429 respetan Retry-After. Cada ACK aceptado deja una
pausa de 250 ms antes del siguiente. Si una alarma ya no existe y recibe 404,
queda con ts_ack NULL y su codigo pasa a alarmas_no_vigentes: no se inventa una
confirmacion ni se bloquea el resto de la cola. HTTP 400, 401 o 403 detienen
el proceso con un diagnostico y conservan la alarma para revisar el problema.

La suscripcion que termina se reconecta con esperas de 1, 2, 4, 8, 16 y hasta
30 segundos. Recibir un mensaje reinicia esa espera. El broker puede repetir
la ultima alarma retenida, y SQLite evita duplicar sus efectos. Al reiniciar
el programa tambien se recuperan las confirmaciones que faltaban.

Ctrl+C solo marca la solicitud de salida. La suscripcion tiene un callback
de progreso que puede abortarla, y el bucle comprueba la bandera cada 100 ms.
El cierre libera la red y SQLite; una alarma que no obtuvo confirmacion queda
pendiente para la siguiente ejecucion.

## Pruebas de las alarmas

~~~bash
python3 pruebas/verificar_alarmas.py
~~~

Dieciseis casos locales comprueban guardado anterior al ACK, mensajes
fragmentados y consecutivos, reconexion y duplicados retenidos, respuesta
perdida seguida de 409, Retry-After, reinicio, 404, caracteres especiales en
el codigo, mensajes invalidos, cargas excesivas o con NUL, rafagas, acceso
rechazado y validacion de argumentos. Tambien ejecutan enviar y escuchar a
la vez sobre la misma base, con alarmas producidas a partir de los latidos.
La prueba automatica de SIGINT se ejecuta en Linux. El cierre de escuchar
con Ctrl+C tambien se comprobo manualmente en la consola de Windows; esta
documentado en [la prueba de cierre](pruebas/validacion_cierre_windows.md).

Los servidores de estas pruebas solo escuchan en 127.0.0.1 y usan una clave
aleatoria. No consumen intentos del curso ni usan el token personal. GitHub
Actions ejecuta las pruebas anteriores y las nuevas en Linux y Windows.

## Sesion completa de evaluacion

Las pruebas locales comprueban el codigo; la puntuacion automatica procede
del servidor del profesor. La consulta final de mi-progreso confirmo los
seis niveles completos y total=70/70. M5, M8 y M9 quedaron en 100.0%,
M6 en 2/2, M7 en 0, M10 en 0.21s y M11 en 0.0%.

La salida local de la ultima sesion informo 120 turnos de latido, con los
120 envios terminados sin error. No hubo latidos descartados u omitidos ni
publicaciones sin resultado al cierre. El servidor confirmo la recepcion remota.

Para una sesion de 20 minutos se puede iniciar escuchar con duracion 1320 y
despues enviar con duracion 1200. El receptor queda un margen para las ultimas
alarmas. Durante las respuestas 503 se dejan ambos programas funcionando:
las lecturas siguen guardandose y se envian cuando termina la espera.

La evidencia esta en [la validacion final](pruebas/validacion_final.md).

## Memoria y recursos

En Linux, pruebas/verificar_memoria.py ejecuta cinco rutas bajo Valgrind:
descarga y analisis, recuperacion de dos caidas HTTP, respuesta perdida y
duplicada, reconexion MQTT y recuperacion de ACK 409. Usa los servidores
locales de prueba y credenciales aleatorias. El procedimiento y el alcance
estan en [la comprobacion de memoria](pruebas/valgrind.md).

Los cinco casos pasaron en Linux: once ejecuciones del cliente C terminaron
con cero errores de Memcheck y toda la memoria liberada en esos procesos.

[Formato de suscripcion MQTT](https://everything.curl.dev/usingcurl/mqtt.html)
y [callback de progreso](https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html).
