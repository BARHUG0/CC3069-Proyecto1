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
LEGACY_OBJ := $(SRC:.c=.o)
SEQ_OBJ := $(SRC:.c=.seq.o)
OMP_OBJ := $(SRC:.c=.omp.o)

ifeq ($(OS),Windows_NT)
  SEQ_BIN := screensaver.exe
  OMP_BIN := screensaver_parallel.exe
  TEST_EXT := .exe
  CFLAGS  += -Ivendor/raylib/include
  LDFLAGS += -Lvendor/raylib/lib
  LDLIBS  := -lraylib -lopengl32 -lgdi32 -lwinmm
  OMP_CFLAGS := -fopenmp
  OMP_LDLIBS := -fopenmp
  CLEAN    = cmd /c del /q /f $(subst /,\,$(LEGACY_OBJ) $(SEQ_OBJ) $(OMP_OBJ) $(SEQ_BIN) $(OMP_BIN) deathstar_test.exe systems_test.exe) 2>NUL
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    SEQ_BIN := screensaver
    OMP_BIN := screensaver_parallel
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
    SEQ_BIN := screensaver
    OMP_BIN := screensaver_parallel
    LDLIBS := -lraylib -lm -lpthread -ldl -lrt -lX11
    OMP_CFLAGS := -fopenmp
    OMP_LDLIBS := -fopenmp
  endif
  TEST_EXT :=
  CLEAN   = rm -f $(LEGACY_OBJ) $(SEQ_OBJ) $(OMP_OBJ) $(SEQ_BIN) $(OMP_BIN) deathstar_test systems_test
endif

.PHONY: all sequential parallel run run-parallel test clean

all: sequential parallel

sequential: $(SEQ_BIN)

parallel: $(OMP_BIN)

$(SEQ_BIN): $(SEQ_OBJ)
	$(CC) $(SEQ_OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(OMP_BIN): $(OMP_OBJ)
	$(CC) $(OMP_OBJ) -o $@ $(LDFLAGS) $(LDLIBS) $(OMP_LDLIBS)

src/%.seq.o: src/%.c src/ecs.h src/rng.h src/spawn.h src/systems.h src/deathstar.h
	$(CC) $(CFLAGS) -c $< -o $@

src/%.omp.o: src/%.c src/ecs.h src/rng.h src/spawn.h src/systems.h src/deathstar.h
	$(CC) $(CFLAGS) -DPARALLEL_SYSTEMS $(OMP_CFLAGS) -c $< -o $@

run: $(SEQ_BIN)
	./$(SEQ_BIN) 6

run-parallel: $(OMP_BIN)
	./$(OMP_BIN) 6

test: tests/deathstar_test.c src/ecs.c src/spawn.c src/systems.c src/deathstar.c
	$(CC) $(CFLAGS) -Isrc $^ -o deathstar_test$(TEST_EXT) $(LDFLAGS) $(LDLIBS)
	./deathstar_test$(TEST_EXT)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -Isrc tests/systems_test.c src/ecs.c src/spawn.c src/systems.c -o systems_test$(TEST_EXT) $(LDFLAGS) $(LDLIBS) $(OMP_LDLIBS)
	./systems_test$(TEST_EXT)

clean:
	-$(CLEAN)
