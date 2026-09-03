#!/usr/bin/env bash
# benchmark.sh - arnes de medicion portable (macOS / Linux), equivalente en modo
# "speedup" al scripts/benchmark.ps1 de Windows. Corre el screensaver en
# --benchmark para una lista de N, RUNS veces cada uno, y agrega los resultados
# a CSVs con el MISMO esquema que la version PowerShell para que
# compare-benchmarks.ps1 y scripts/build_report.py los consuman igual.
#
# Requiere una sesion grafica activa: --benchmark abre una ventana real (no hay
# ruta headless). Si la pantalla esta bloqueada/dormida el binario falla con
# "Failed to initialize platform".
#
# Uso:
#   scripts/benchmark.sh                          # VERSION=sequential (default)
#   VERSION=parallel scripts/benchmark.sh         # barre THREADS_LIST hilos
#   SYSTEMS="1000 5000 25000" RUNS=10 scripts/benchmark.sh
#   scripts/benchmark.sh --ceiling 1000000        # 1 punto cualitativo, sin --benchmark
#
# Variables de entorno (con sus valores por defecto):
#   SYSTEMS="1000 5000 15000 30000 50000"     lista de N a medir (>=1000 para que
#                                             la ruta paralela se active)
#   RUNS=10                                    corridas por (N, hilos) — el PDF pide >=10
#   THREADS_LIST="1 2 4 8 16"                  conteos de hilos a barrer (solo parallel)
#   STARS=500  SEED=20260831  WIDTH=1280  HEIGHT=720
#   VERSION=sequential|parallel               que ruta de actualizacion medir
#   OUTDIR=benchmark-results
#   MAKE=make

set -euo pipefail

SYSTEMS="${SYSTEMS:-1000 5000 15000 30000 50000}"
RUNS="${RUNS:-10}"
STARS="${STARS:-500}"
SEED="${SEED:-20260831}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
VERSION="${VERSION:-sequential}"
THREADS_LIST="${THREADS_LIST:-1 2 4 8 16}"
OUTDIR="${OUTDIR:-benchmark-results}"
MAKE="${MAKE:-make}"

# Rutas relativas a la raiz del proyecto (el directorio tiene espacios).
cd "$(dirname "$0")/.."
PROJECT_ROOT="$(pwd)"
BIN="./screensaver"
TS="$(date +%Y%m%d-%H%M%S)"

case "$VERSION" in
  sequential) MODE_FLAG="--sequential"; THREADS_LIST="0" ;;
  parallel)   MODE_FLAG="--parallel" ;;
  *) echo "VERSION debe ser 'sequential' o 'parallel', no '$VERSION'" >&2; exit 2 ;;
esac

mkdir -p "$OUTDIR"

"$MAKE" >/dev/null
[ -x "$BIN" ] || { echo "No se encontro el ejecutable: $BIN" >&2; exit 1; }

# --- metadatos del equipo ---------------------------------------------------
commit="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
dirty="false"; [ -n "$(git status --porcelain 2>/dev/null)" ] && dirty="true"
gcc_ver="$(${CC:-gcc} --version 2>/dev/null | head -1)"
os_ver="$(sw_vers -productName 2>/dev/null && sw_vers -productVersion 2>/dev/null || uname -sr)"
os_ver="$(printf '%s' "$os_ver" | tr '\n' ' ' | sed 's/  */ /g;s/ $//')"

if command -v sysctl >/dev/null 2>&1 && sysctl -n machdep.cpu.brand_string >/dev/null 2>&1; then
  cpu="$(sysctl -n machdep.cpu.brand_string)"
  phys_cores="$(sysctl -n hw.physicalcpu 2>/dev/null || echo 0)"
  log_cores="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 0)"
  clock_hz="$(sysctl -n hw.cpufrequency_max 2>/dev/null || echo 0)"
  clock_mhz=$(( clock_hz / 1000000 ))
else
  cpu="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //' || echo unknown)"
  phys_cores="$(nproc --all 2>/dev/null || echo 0)"
  log_cores="$phys_cores"
  clock_mhz=0
fi

if command -v system_profiler >/dev/null 2>&1; then
  gpu="$(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/Chipset Model/{print $2; exit}')"
  gpu_res="$(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/Resolution/{print $2; exit}')"
else
  gpu="unknown"; gpu_res="unknown"
fi
[ -n "$gpu" ] || gpu="unknown"

HW_JSON="$OUTDIR/hardware-speedup-$VERSION-$TS.json"
cat > "$HW_JSON" <<EOF
{
  "Commit": "$commit",
  "DirtyWorkingTree": $dirty,
  "OperatingSystem": "$os_ver",
  "Gcc": "$gcc_ver",
  "Processors": [
    {
      "Name": "$cpu",
      "PhysicalCores": $phys_cores,
      "LogicalProcessors": $log_cores,
      "MaxClockMHz": $clock_mhz
    }
  ],
  "Graphics": [
    { "Name": "$gpu", "CurrentResolution": "$gpu_res" }
  ]
}
EOF
echo "Hardware -> $HW_JSON"

# --- modo ceiling: 1 punto cualitativo (sin --benchmark) -------------------
# scripts/benchmark.sh --ceiling [N]            (secuencial)
# VERSION=parallel CEIL_THREADS=8 scripts/benchmark.sh --ceiling 1000000
if [ "${1:-}" = "--ceiling" ]; then
  N="${2:-1000000}"
  CEIL_CSV="$OUTDIR/scaling-ceiling.csv"
  [ -f "$CEIL_CSV" ] || echo "Version,Threads,Systems,Entities,UpdateMs,MeanFps,Frames,Seconds,Commit,Timestamp" > "$CEIL_CSV"
  if [ "$VERSION" = parallel ]; then
    thr="${CEIL_THREADS:-4}"
    out="$("$BIN" "$N" --stars "$STARS" --seed "$SEED" --width "$WIDTH" --height "$HEIGHT" \
          "$MODE_FLAG" --threads "$thr" --frames 5 --no-vsync 2>&1 || true)"
  else
    thr=1
    out="$("$BIN" "$N" --stars "$STARS" --seed "$SEED" --width "$WIDTH" --height "$HEIGHT" \
          "$MODE_FLAG" --frames 5 --no-vsync 2>&1 || true)"
  fi
  echo "Ceiling: N=$N version=$VERSION hilos=$thr"
  ent="$(printf '%s\n' "$out" | awk -F'[ :]+' '/Entidades/{print $2; exit}')"
  ums="$(printf '%s\n' "$out" | awk -F'[ :]+' '/Actualizacion ECS/{print $4; exit}')"
  fps="$(printf '%s\n' "$out" | sed -n 's/.*(\([0-9.]*\) FPS medio).*/\1/p' | head -1)"
  fr="$(printf '%s\n'  "$out" | sed -n 's/Fotogramas *: \([0-9]*\) en.*/\1/p' | head -1)"
  sec="$(printf '%s\n' "$out" | sed -n 's/Fotogramas *: [0-9]* en \([0-9.]*\) s.*/\1/p' | head -1)"
  echo "$VERSION,$thr,$N,${ent:-},${ums:-},${fps:-},${fr:-},${sec:-},$commit,$(date +%FT%T)" >> "$CEIL_CSV"
  printf '%s\n' "$out" | grep -E 'Entidades|Actualizacion|Fotogramas|Aviso|Error' || true
  echo "-> $CEIL_CSV"
  exit 0
fi

# --- sweep -----------------------------------------------------------------
RUNS_CSV="$OUTDIR/runs-speedup-$VERSION-$TS.csv"
SUMMARY_CSV="$OUTDIR/summary-speedup-$VERSION-$TS.csv"
echo "Run,Timestamp,Commit,DirtyWorkingTree,Cpu,Gpu,OperatingSystem,Gcc,Mode,Version,Threads,Seed,Systems,Stars,Width,Height,Seconds,Frames,AverageFps,OneSecondMinFps,GetFpsAverage,GetFpsMin,Samples,UpdateMs" > "$RUNS_CSV"

run_one() {  # $1 = N, $2 = hilos (0 = sin --threads); imprime la linea BENCHMARK_CSV
  if [ "${2:-0}" -gt 0 ]; then
    "$BIN" "$1" --stars "$STARS" --seed "$SEED" --width "$WIDTH" --height "$HEIGHT" \
        "$MODE_FLAG" --threads "$2" --no-vsync --benchmark 2>/dev/null \
      | grep '^BENCHMARK_CSV,' | head -1
  else
    "$BIN" "$1" --stars "$STARS" --seed "$SEED" --width "$WIDTH" --height "$HEIGHT" \
        "$MODE_FLAG" --no-vsync --benchmark 2>/dev/null \
      | grep '^BENCHMARK_CSV,' | head -1
  fi
}

total=$(( $(echo $SYSTEMS | wc -w) * $(echo $THREADS_LIST | wc -w) * RUNS ))
done_runs=0
for N in $SYSTEMS; do
  for T in $THREADS_LIST; do
    accepted=0
    attempts=0
    max_attempts=$(( RUNS * 3 ))
    while [ "$accepted" -lt "$RUNS" ]; do
      attempts=$(( attempts + 1 ))
      if [ "$attempts" -gt "$max_attempts" ]; then
        echo "No se completaron $RUNS corridas validas con N=$N hilos=$T" >&2; exit 1
      fi
      tlabel="$([ "$VERSION" = parallel ] && echo " hilos=$T" || echo "")"
      echo "[$((done_runs+1))/$total] N=$N$tlabel  corrida $(( accepted + 1 ))/$RUNS"
      line="$(run_one "$N" "$T" || true)"
      if [ -z "$line" ]; then
        echo "  sin linea BENCHMARK_CSV (¿pantalla bloqueada?), reintentando" >&2
        sleep 1; continue
      fi
      IFS=',' read -r _tag mode thr seed sys stars w h secs frames afps s1min gfa gfm samples ums <<EOF
$line
EOF
      if [ "$samples" != "10" ]; then
        echo "  $samples intervalos (esperados 10) — N muy alto para esta maquina, reintentando" >&2
        continue
      fi
      run_no=$(( accepted + 1 ))
      printf '%s,%s,%s,%s,"%s","%s","%s","%s",speedup,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$run_no" "$(date +%FT%T)" "$commit" "$dirty" "$cpu" "$gpu" "$os_ver" "$gcc_ver" \
        "$mode" "$thr" "$seed" "$sys" "$stars" "$w" "$h" \
        "$secs" "$frames" "$afps" "$s1min" "$gfa" "$gfm" "$samples" "$ums" >> "$RUNS_CSV"
      accepted=$(( accepted + 1 ))
      done_runs=$(( done_runs + 1 ))
    done
  done
done
echo "Corridas -> $RUNS_CSV"

# --- agregacion: una fila por (N, hilos) -------------------------------------
awk -F',' '
  function median(a, n,   b, i, j, t) {
    for (i = 1; i <= n; i++) b[i] = a[i]
    for (i = 1; i < n; i++) for (j = i + 1; j <= n; j++)
      if (b[j] < b[i]) { t = b[i]; b[i] = b[j]; b[j] = t }
    if (n % 2) return b[int(n/2) + 1]
    return (b[n/2] + b[n/2 + 1]) / 2.0
  }
  NR == 1 { next }
  {
    v = $10; thr = $11 + 0; seed = $12; n = $13 + 0; stars = $14; w = $15; h = $16
    key = n SUBSEP thr
    cnt[key]++
    fps[key SUBSEP cnt[key]] = $19 + 0
    ums[key SUBSEP cnt[key]] = $24 + 0
    if (!( key in wmin ) || ($20 + 0) < wmin[key]) wmin[key] = $20 + 0
    ver[key] = v; nn[key] = n; tt[key] = thr; sd[key] = seed
    st[key] = stars; ww[key] = w; hh[key] = h
  }
  END {
    print "Version,Threads,Systems,Stars,Width,Height,Seed,Runs,MeanFps,MedianFps,WorstOneSecondFps,StandardDeviationFps,MeanUpdateMs,MedianUpdateMs,StandardDeviationUpdateMs"
    for (k in cnt) {
      m = cnt[k]
      fs = 0; us = 0
      for (i = 1; i <= m; i++) { fa[i] = fps[k SUBSEP i]; ua[i] = ums[k SUBSEP i]; fs += fa[i]; us += ua[i] }
      fmean = fs / m; umean = us / m
      fvar = 0; uvar = 0
      for (i = 1; i <= m; i++) { fvar += (fa[i]-fmean)^2; uvar += (ua[i]-umean)^2 }
      fvar /= m; uvar /= m
      printf "%s,%d,%d,%s,%s,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%.6f\n",
        ver[k], tt[k], nn[k], st[k], ww[k], hh[k], sd[k], m,
        fmean, median(fa, m), wmin[k], sqrt(fvar),
        umean, median(ua, m), sqrt(uvar)
    }
  }
' "$RUNS_CSV" | { read -r hdr; echo "$hdr"; sort -t',' -k3,3n -k2,2n; } > "$SUMMARY_CSV"

echo "Resumen  -> $SUMMARY_CSV"
column -t -s',' "$SUMMARY_CSV" 2>/dev/null || cat "$SUMMARY_CSV"
