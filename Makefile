CC = gcc
CPPFLAGS = -Isrc -Ilib/sqlite
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
SQLITE_CFLAGS = -std=c11 -O2
LDLIBS = -lcurl -lm
ARGS ?= config

ifeq ($(OS),Windows_NT)
    PROGRAMA = estacion.exe
else
    PROGRAMA = estacion
    SQLITE_CFLAGS += -pthread
    LDLIBS += -ldl -pthread
endif

OBJETOS = build/main.o build/red.o build/datos.o build/descarga.o build/analisis.o build/sqlite3.o

.PHONY: all run clean

all: $(PROGRAMA)

$(PROGRAMA): $(OBJETOS)
	$(CC) $(LDFLAGS) $(OBJETOS) $(LDLIBS) -o $@

build/%.o: src/%.c $(wildcard src/*.h) lib/sqlite/sqlite3.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/sqlite3.o: lib/sqlite/sqlite3.c lib/sqlite/sqlite3.h | build
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) -c $< -o $@

build:
	mkdir -p build

run: all
	./$(PROGRAMA) $(ARGS)

clean:
	rm -rf build
	rm -f estacion estacion.exe
