# Screensaver ECS - build
#
# Windows (MSYS2 UCRT64, raylib vendorizado en vendor/raylib):
#     mingw32-make
#     .\screensaver.exe 6
#
# Todas las rutas son relativas a propósito: el directorio del proyecto
# contiene espacios y make no los maneja bien en rutas absolutas.

CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS :=

SRC := src/ecs.c src/spawn.c src/systems.c src/deathstar.c src/main.c
OBJ := $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
  BIN     := screensaver.exe
  CFLAGS  += -Ivendor/raylib/include
  LDFLAGS += -Lvendor/raylib/lib
  LDLIBS  := -lraylib -lopengl32 -lgdi32 -lwinmm
  CLEAN    = cmd /c del /q /f $(subst /,\,$(OBJ) $(BIN) deathstar_test.exe) 2>NUL
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    BIN     := screensaver
    # raylib se instala vía Homebrew (brew install raylib); pkg-config
    # resuelve sus rutas (/opt/homebrew o /usr/local según el chip).
    CFLAGS  += $(shell pkg-config --cflags raylib)
    LDFLAGS += $(shell pkg-config --libs-only-L raylib)
    LDLIBS  := -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
  else
    BIN    := screensaver
    LDLIBS := -lraylib -lm -lpthread -ldl -lrt -lX11
  endif
  CLEAN   = rm -f $(OBJ) $(BIN) deathstar_test
endif

.PHONY: all run test clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

# Los .o dependen de todas las cabeceras: el proyecto es pequeño y así no hay
# builds a medias tras editar un header.
src/%.o: src/%.c src/ecs.h src/rng.h src/spawn.h src/systems.h src/deathstar.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) 6

test: tests/deathstar_test.c src/ecs.c src/spawn.c src/systems.c src/deathstar.c
	$(CC) $(CFLAGS) -Isrc $^ -o deathstar_test $(LDFLAGS) $(LDLIBS)
	./deathstar_test

clean:
	-$(CLEAN)
