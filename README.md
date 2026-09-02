# CC3069 Proyecto1 — Screensaver ECS

Screensaver de campo de estrellas + N sistemas solares, en C11 con [raylib](https://www.raylib.com/) y una arquitectura ECS orientada a datos (ver `src/ecs.h`).

## Requisitos

- gcc (o mingw-w64 en Windows) con soporte C11
- `make`
- raylib 5.x

## Windows (MSYS2 UCRT64)

raylib va en `vendor/raylib`

1. Instala [MSYS2](https://www.msys2.org/) y abre la terminal **UCRT64** (no MSYS ni MINGW64).
2. Instala el toolchain:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
   ```
3. Compila las versiones secuencial y paralela desde la raíz del proyecto:
   ```
   mingw32-make
   .\screensaver.exe 6
   .\screensaver_parallel.exe 6
   ```

> El proyecto debe vivir en una ruta sin espacios; el `Makefile` usa rutas relativas por eso.

## macOS

raylib **no** va vendorizado para macOS (`vendor/raylib` solo trae binarios de Windows); instálalo con Homebrew:

```
brew install raylib
make
./screensaver 6
./screensaver_parallel 6
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
| `--no-trails` | no dibujar las estelas |
| `--no-vsync` | sin vsync (para medir FPS reales) |
| `--hud` | arrancar con el panel de datos visible |
| `--deadstar` | activar la Estrella de la Muerte controlable |
| `--deadstar-static` | mostrar la Estrella de la Muerte sin animarla |
| `--target-fps F` | limitar la ejecución a F FPS mediante `SetTargetFPS` |
| `--benchmark` | medir FPS durante 10 segundos tras 3 segundos de calentamiento |
| `--frames K` | salir tras K fotogramas (pruebas) |
| `--screenshot RUTA` | guardar un PNG y seguir (pruebas) |
| `-h`, `--help` | ayuda |

Controles en ejecución: `H` panel de datos, `O` órbitas, `T` estelas, `P` pausa, `R` reset y `F` fullscreen.

Con `--deadstar`: `W`, `A`, `S` y `D` mueven la Estrella de la Muerte. `Espacio` dispara a un sistema solar elegido al azar y lo destruye.

## Medición reproducible de FPS

El benchmark desactiva los limitadores de frecuencia y usa `GetTime()` de
raylib para medir todos los fotogramas renderizados durante 10 segundos. Antes
descarta 3 segundos de calentamiento. Además, toma una muestra de `GetFPS()` por
segundo para calcular un promedio y un mínimo independientes.

Debe ejecutarse con `--no-vsync` y sin `--target-fps`:

```
screensaver 256 --stars 500 --seed 20260831 --width 1280 --height 720 --no-vsync --benchmark
```

El modo `stability` busca el máximo de sistemas estable a 10, 30, 60, 90 y
120 FPS con 500 estrellas. La búsqueda se ejecuta por separado para cada
versión:

```powershell
.\scripts\benchmark.ps1 -Mode stability -Version sequential
.\scripts\benchmark.ps1 -Mode stability -Version parallel -Threads 4
```

El archivo `runs` contiene cada corrida con sus parámetros, segundos,
fotogramas, FPS medio, peor intervalo de un segundo, commit y datos principales
del equipo. El archivo `summary` contiene una fila por cantidad medida con
media, mediana y desviación estándar. `stability-points` guarda el máximo
estable y el primer valor inestable para cada objetivo. Un input se considera
estable únicamente si el peor intervalo de un segundo de todas sus corridas
alcanza el objetivo.

El archivo `hardware` guarda en JSON la CPU, sus núcleos y frecuencia máxima,
junto con la GPU, controlador, resolución y frecuencia de actualización.

Cada corrida tarda cerca de 13 segundos, incluidos 3 segundos de calentamiento.
Para una revisión rápida puede usarse:

```powershell
.\scripts\benchmark.ps1 -Mode stability -TargetFpsValues 60 -MaxSystems 64 -Runs 2
```

Una ejecución interrumpida puede continuar a partir de su archivo `runs`:

```powershell
.\scripts\benchmark.ps1 -ResumeRunsFile .\benchmark-results\runs-AAAAMMDD-HHMMSS.csv
```

## Medición de speedup

El *speedup* no utiliza la búsqueda dinámica de estabilidad. El modo `speedup`
ejecuta las dos versiones con exactamente los mismos valores fijos de `N`,
500 estrellas, la misma semilla y la misma resolución:

```powershell
$systems = 1, 32, 64, 128, 256
.\scripts\benchmark.ps1 -Mode speedup -Version sequential -SystemsValues $systems
.\scripts\benchmark.ps1 -Mode speedup -Version parallel -Threads 4 -SystemsValues $systems
```

Después se comparan los dos archivos `summary-speedup` producidos:

```powershell
.\scripts\compare-benchmarks.ps1 `
  -SequentialSummary .\benchmark-results\summary-speedup-sequential-AAAAMMDD-HHMMSS.csv `
  -ParallelSummary .\benchmark-results\summary-speedup-parallel-AAAAMMDD-HHMMSS.csv
```

El comparador exige que ambos resúmenes contengan exactamente los mismos
valores de `N` y la misma configuración. El *speedup* se calcula con el tiempo
medio de actualización ECS, sin render:

`speedup = tiempo secuencial / tiempo paralelo`

La eficiencia se calcula como `speedup / hilos * 100`. Los FPS se conservan
como dato adicional, pero no se usan para calcular el *speedup* porque el
renderizado permanece serial.

La cantidad de hilos OpenMP puede fijarse con `OMP_NUM_THREADS` o con el
parámetro `-Threads`.

Se eligió paralelismo de datos porque los sistemas recorren arreglos SoA y
cada iteración actualiza una entidad o sistema solar independiente. OpenMP
reparte los bucles de deriva, centelleo, órbitas, estelas y vida. La creación
de estrellas permanece serial para conservar la secuencia del RNG y del
asignador ECS. El render también permanece serial porque raylib y OpenGL deben
usarse desde el hilo principal.

Para comparar resultados deben mantenerse la misma semilla, resolución,
cantidad de sistemas y estrellas, plan de energía y pantalla. También conviene
cerrar aplicaciones pesadas y esperar a que la temperatura del equipo se
estabilice. La semilla fija reproduce la escena, pero los controladores, la
temperatura y los procesos del sistema todavía pueden variar el rendimiento.

## Otros targets del Makefile

```
make run      # build + ./screensaver 6
make run-parallel # build + ./screensaver_parallel 6
make sequential   # solo la versión secuencial
make parallel     # solo la versión OpenMP
make test         # equivalencia secuencial/paralela
make clean    # borra objetos y el binario
```
