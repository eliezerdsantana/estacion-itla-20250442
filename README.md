# Estacion de Telemetria ITLA

Eliezer Daniels Santana. Matricula: 2025-0442.
Programacion para Mecatronicos, seccion de los miercoles.

## Avance actual

Ya estan implementados `config` y `descargar`.

`config` pide la configuracion al servidor, comprueba la respuesta HTTP y guarda
los siete campos en SQLite. Despues imprime los valores consultados de la base.

`descargar` pide las lecturas por paginas, guarda una sola fila por ID y calcula
el numero de filas y la suma de IDs con SQL. Envia esos resultados al servidor
y comprueba que la respuesta contenga la linea `correcto=1`.

Quedan pendientes `analizar`, `enviar` y `escuchar`, junto con el modulo del
sensor y las pruebas de los niveles siguientes.

Repositorio: https://github.com/eliezerdsantana/estacion-itla-20250442

## Archivos

- `src/main.c`: lee el subcomando y las variables de entorno.
- `src/red.c` y `src/red.h`: manejador HTTP, autenticacion y recepcion del texto.
- `src/datos.c` y `src/datos.h`: tablas, transacciones y sentencias de SQLite.
- `src/descarga.c` y `src/descarga.h`: lectura del CSV, paginacion y comprobacion
  de la descarga.
- `lib/sqlite/sqlite3.c` y `sqlite3.h`: archivos originales de SQLite 3.54.1.
- `Makefile`: compila el programa y SQLite por separado.
- `plan.md`: checklist del trabajo y del avance probado.

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
- La conexion tiene un limite de 3 segundos y la peticion de 5 segundos.
- Se revisan el resultado de libcurl y el codigo HTTP por separado.
- El callback recibe fragmentos y evita escribir fuera del buffer.
- SQLite usa WAL y espera hasta 2000 ms si la base esta ocupada.
- Se crean las cuatro tablas del mandato; por ahora se llenan `config` y
  `lecturas_planta`.
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

La prueba en Windows y la confirmacion del servidor del curso se realizan
despues de copiar este avance al equipo.

## Apoyo utilizado

Use ChatGPT para organizar los pasos, proponer esta base de codigo, explicar
las funciones y preparar comprobaciones de compilacion y errores. Voy revisando
el codigo por partes para poder explicar su funcionamiento en la defensa.

## Fuentes

- Mandato de la practica y correo del profesor Wilkins Cedano.
- [Documentacion de libcurl](https://curl.se/libcurl/c/).
- [Documentacion de SQLite en C](https://www.sqlite.org/cintro.html).
- [Paquetes y herramientas de MSYS2](https://www.msys2.org/docs/package-management/).
