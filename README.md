# Estacion de Telemetria ITLA

Eliezer Daniels Santana. Matricula: 2025-0442.
Programacion para Mecatronicos, seccion de los miercoles.

## Avance actual

Esta primera version implementa `config`: pide la configuracion al servidor,
comprueba la respuesta HTTP y guarda los siete campos en SQLite. Despues imprime
los valores que consulta de la tabla `config`.

Aun quedan pendientes `descargar`, `analizar`, `enviar` y `escuchar`, junto con
el modulo del sensor y las pruebas de los niveles siguientes.

## Archivos

- `src/main.c`: lee el subcomando y las variables de entorno.
- `src/red.c` y `src/red.h`: manejador HTTP, autenticacion y recepcion del texto.
- `src/datos.c` y `src/datos.h`: tablas, transacciones y sentencias de SQLite.
- `lib/sqlite/sqlite3.c` y `sqlite3.h`: archivos originales de SQLite 3.54.1.
- `Makefile`: compila el programa y SQLite por separado.

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
- La conexion tiene un limite de 3 segundos y la peticion de 5 segundos.
- Se revisan el resultado de libcurl y el codigo HTTP por separado.
- El callback recibe fragmentos y evita escribir fuera del buffer.
- SQLite usa WAL y espera hasta 2000 ms si la base esta ocupada.
- Se crean las cuatro tablas del mandato; por ahora solo se llena `config`.
- Los valores recibidos se guardan con `prepare`, `bind`, `step` y `finalize`.
- La transaccion se revierte si la configuracion esta incompleta, repetida
  o pertenece a otra matricula.

## Apoyo utilizado

Use ChatGPT para organizar los pasos, proponer esta base de codigo, explicar
las funciones y preparar comprobaciones de compilacion y errores. Voy revisando
el codigo por partes para poder explicar su funcionamiento en la defensa.

## Fuentes

- Mandato de la practica y correo del profesor Wilkins Cedano.
- [Documentacion de libcurl](https://curl.se/libcurl/c/).
- [Documentacion de SQLite en C](https://www.sqlite.org/cintro.html).
- [Paquetes y herramientas de MSYS2](https://www.msys2.org/docs/package-management/).

El enlace del repositorio se agregara cuando se publique.
