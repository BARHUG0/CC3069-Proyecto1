# CC3069 Proyecto1 — Screensaver ECS

Screensaver de campo de estrellas + N sistemas solares, en C11 con [raylib](https://www.raylib.com/) y una arquitectura ECS orientada a datos (ver `src/ecs.h`).

## Requisitos

- gcc (o mingw-w64 en Windows) con soporte C11
- `make`
- raylib 5.x

## Windows (MSYS2 UCRT64)

raylib va vendorizado en `vendor/raylib` — no hace falta instalarlo aparte.

1. Instala [MSYS2](https://www.msys2.org/) y abre la terminal **UCRT64** (no MSYS ni MINGW64).
2. Instala el toolchain:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
   ```
3. Compila y corre desde la raíz del proyecto:
   ```
   mingw32-make
   .\screensaver.exe 6
   ```

> El proyecto debe vivir en una ruta sin espacios; el `Makefile` usa rutas relativas por eso.

## macOS

raylib **no** va vendorizado para macOS (`vendor/raylib` solo trae binarios de Windows); instálalo con Homebrew:

```
brew install raylib
make
./screensaver 6
```

El `Makefile` usa `pkg-config` para ubicar los headers/libs de raylib que instala Homebrew, así que `pkg-config` debe estar en el PATH (viene con Homebrew).

## Uso

```
screensaver N [opciones]
```

`N` = número de sistemas solares a generar (obligatorio).

| Opción | Descripción |
|---|---|
| `--stars M` | estrellas de fondo simultáneas |
| `--width W` / `--height H` | tamaño de ventana |
| `--seed S` | semilla del generador (default: reloj) |
| `--fullscreen` | pantalla completa sin borde |
| `--no-rings` | no dibujar las órbitas |
| `--no-vsync` | sin vsync (para medir FPS reales) |
| `--hud` | arrancar con el panel de datos visible |
| `--frames K` | salir tras K fotogramas (pruebas) |
| `--screenshot RUTA` | guardar un PNG y seguir (pruebas) |
| `-h`, `--help` | ayuda |

Controles en ejecución: `H` panel de datos, `O` órbitas, `Espacio` pausa, `R` reset, `F` fullscreen.

## Otros targets del Makefile

```
make run      # build + ./screensaver 6
make clean    # borra objetos y el binario
```
