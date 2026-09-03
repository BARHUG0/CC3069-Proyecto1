# CC3069 Proyecto1 — Screensaver ECS

> # 📄 LEER: [**`Proyecto1-Informe.pdf`**](./Proyecto1-Informe.pdf)
> ## El informe del proyecto está en **[`Proyecto1-Informe.pdf`](./Proyecto1-Informe.pdf)** — ese es el documento que deben leer.

Screensaver de campo de estrellas + N sistemas solares, en C11 con [raylib](https://www.raylib.com/) y una arquitectura ECS orientada a datos (ver `src/ecs.h`).

## Requisitos

- gcc (o mingw-w64 en Windows) con soporte C11
- `make`
- raylib 5.x o 6.x
- OpenMP (viene con gcc/mingw; en macOS es `libomp` de Homebrew)

Se compila **un solo binario** `screensaver`, siempre con OpenMP. El modo se
elige al ejecutar: `--sequential` (por defecto) o `--parallel`.

## Windows (MSYS2 UCRT64)

raylib va en `vendor/raylib`

1. Instala [MSYS2](https://www.msys2.org/) y abre la terminal **UCRT64** (no MSYS ni MINGW64).
2. Instala el toolchain:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
   ```
3. Compila desde la raíz del proyecto:
   ```
   mingw32-make
   .\screensaver.exe 6
   .\screensaver.exe 6 --parallel
   ```

> El proyecto debe vivir en una ruta sin espacios; el `Makefile` usa rutas relativas por eso.

## macOS

raylib **no** va vendorizado para macOS (`vendor/raylib` solo trae binarios de Windows); instálalo con Homebrew junto con `libomp` (Apple clang no trae OpenMP):

```
brew install raylib libomp
make
./screensaver 6
./screensaver 6 --parallel
```

### Sequencial
```
./screensaver 1000 --sequential --fullscreen --deadstar
```

### Paralelo
```
./screensaver 1000 --parallel --fullscreen --deadstar
```

El `Makefile` usa `pkg-config` para ubicar raylib y `brew --prefix libomp` para el runtime de OpenMP, así que Homebrew debe estar en el PATH.

## Uso

```
screensaver N [opciones]
```

`N` = número de sistemas solares a generar (obligatorio).

| Opción | Descripción |
|---|---|
| `--parallel` | actualizar los sistemas con OpenMP (varios hilos) |
| `--sequential` | actualizar los sistemas en un solo hilo (por defecto) |
| `--stars M` | estrellas de fondo simultáneas, 1200 por defecto |
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
500 estrellas, la misma semilla y la misma resolución.

El screensaver acepta `--threads T` (con `--parallel`): `T = 0` usa el default de
4 hilos, `T > 0` fuerza exactamente esa cantidad (permite *oversubscription* —
más hilos que núcleos — para medir dónde deja de rendir).

### macOS / Linux — `scripts/benchmark.sh`

```bash
scripts/benchmark.sh                          # VERSION=sequential (default)
VERSION=parallel scripts/benchmark.sh         # barre THREADS_LIST="1 2 4 8 16"
SYSTEMS="1000 5000 15000 30000 50000" RUNS=10 scripts/benchmark.sh
scripts/benchmark.sh --ceiling 1000000        # 1 punto cualitativo (N enorme, sin --benchmark)
```

Necesita una sesión gráfica activa (`--benchmark` abre una ventana real). El modo
paralelo mide cada N con cada conteo de hilos de `THREADS_LIST`; escribe
`runs-speedup-*`, `summary-speedup-*` (una fila por N y por conteo de hilos) y
`hardware-speedup-*` en `benchmark-results/`.

Después:

```bash
python3 scripts/build_report.py               # -> benchmark-results/report.html
```

`report.html` es una presentación de diapositivas autocontenida (navegación con
flechas / clic / botones, sin scroll) con las gráficas de speedup, eficiencia,
FPS y ms de actualización, y las tablas de la bitácora.

### Windows — `scripts/benchmark.ps1`

```powershell
$systems = 1000, 5000, 25000, 100000, 200000
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
parámetro `-Threads`. La implementación usa como máximo 4 hilos porque las
pruebas en el equipo de referencia mostraron que 6 o 12 compiten con el hilo
de render y reducen los FPS totales.

Se eligió paralelismo de datos porque los sistemas recorren arreglos SoA y
cada iteración actualiza una entidad independiente. El binario es único y
siempre lleva OpenMP compilado: con `--sequential` (por defecto) el bucle de
actualización corre en un hilo, con `--parallel` OpenMP reparte ese mismo
recorrido —centelleo, órbitas y vida fusionados— y al terminar hace una única
unión antes de que el hilo principal lea posiciones para las estelas y el
render. No se usan mutexes, atómicos ni secciones críticas. La deriva,
creación, destrucción y estelas permanecen seriales porque sus recorridos son
pequeños o modifican estado global. Con menos de 6144 entidades, `--parallel`
usa directamente el recorrido secuencial para evitar que el costo de OpenMP
supere el trabajo.

El render permanece en el hilo principal porque raylib y OpenGL no son
reentrantes. Las estelas conservan todas sus 120 muestras y el mismo grosor,
color y alfa, pero sus triángulos se cargan en una malla dinámica y se dibujan
en una sola llamada. Esto evita miles de descargas del lote interno de raylib
sin reducir detalle visual.

Para comparar resultados deben mantenerse la misma semilla, resolución,
cantidad de sistemas y estrellas, plan de energía y pantalla. También conviene
cerrar aplicaciones pesadas y esperar a que la temperatura del equipo se
estabilice. La semilla fija reproduce la escena, pero los controladores, la
temperatura y los procesos del sistema todavía pueden variar el rendimiento.

## Otros targets del Makefile

```
make          # build del binario screensaver
make run      # build + ./screensaver 6
make test     # equivalencia secuencial/paralela
make clean    # borra objetos y el binario
```
