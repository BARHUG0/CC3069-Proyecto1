# Screensaver ECS - build
#
# Un solo binario, siempre compilado con OpenMP. El modo se elige en tiempo de
# ejecucion: `./screensaver 6` (secuencial, por defecto) o `./screensaver 6 --parallel`.
#
# Windows (MSYS2 UCRT64, raylib vendorizado en vendor/raylib):
#     mingw32-make
#     .\screensaver.exe 6 --parallel
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
  TEST_EXT := .exe
  CFLAGS  += -Ivendor/raylib/include
  LDFLAGS += -Lvendor/raylib/lib
  LDLIBS  := -lraylib -lopengl32 -lgdi32 -lwinmm
  OMP_CFLAGS := -fopenmp
  OMP_LDLIBS := -fopenmp
  CLEAN    = cmd /c del /q /f $(subst /,\,$(OBJ) $(BIN) deathstar_test.exe systems_test.exe) 2>NUL
else
  UNAME_S := $(shell uname -s)
  BIN     := screensaver
  TEST_EXT :=
  ifeq ($(UNAME_S),Darwin)
    # raylib se instala vía Homebrew (brew install raylib); pkg-config
    # resuelve sus rutas (/opt/homebrew o /usr/local según el chip).
    CFLAGS  += $(shell pkg-config --cflags raylib)
    LDFLAGS += $(shell pkg-config --libs-only-L raylib)
    LDLIBS  := -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    # Apple clang no acepta -fopenmp directo: necesita el runtime de Homebrew.
    LIBOMP     := $(shell brew --prefix libomp 2>/dev/null)
    # -Wno-c23-extensions: el omp.h de Homebrew usa enums > INT_MAX y dispara
    # avisos de -Wpedantic que no son de nuestro codigo.
    OMP_CFLAGS := -Xpreprocessor -fopenmp -I$(LIBOMP)/include -Wno-c23-extensions
    OMP_LDLIBS := -L$(LIBOMP)/lib -lomp
  else
    LDLIBS := -lraylib -lm -lpthread -ldl -lrt -lX11
    OMP_CFLAGS := -fopenmp
    OMP_LDLIBS := -fopenmp
  endif
  CLEAN   = rm -f $(OBJ) $(BIN) deathstar_test systems_test
endif

.PHONY: all run test clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS) $(OMP_LDLIBS)

# Los .o dependen de todas las cabeceras: el proyecto es pequeño y así no hay
# builds a medias tras editar un header.
src/%.o: src/%.c src/ecs.h src/rng.h src/spawn.h src/systems.h src/deathstar.h
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) 6

test: tests/deathstar_test.c src/ecs.c src/spawn.c src/systems.c src/deathstar.c
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -Isrc $^ -o deathstar_test$(TEST_EXT) $(LDFLAGS) $(LDLIBS) $(OMP_LDLIBS)
	./deathstar_test$(TEST_EXT)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -Isrc tests/systems_test.c src/ecs.c src/spawn.c src/systems.c -o systems_test$(TEST_EXT) $(LDFLAGS) $(LDLIBS) $(OMP_LDLIBS)
	./systems_test$(TEST_EXT)

clean:
	-$(CLEAN)
