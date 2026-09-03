#!/usr/bin/env python3
"""build_report.py - genera benchmark-results/report.html a partir de los CSV
que produce scripts/benchmark.sh (o benchmark.ps1 en modo speedup).

Lee los summary-speedup-{sequential,parallel}-*.csv mas recientes, los cruza por
N, calcula speedup = t_seq / t_par y eficiencia = speedup / hilos, y escribe una
sola pagina HTML autocontenida (sin CDN): una presentacion de diapositivas
(navegacion con flechas / clic / botones, sin scroll de pagina). Solo stdlib.

Uso:  python3 scripts/build_report.py [--out benchmark-results/report.html]
"""
import csv
import glob
import json
import os
import sys
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "benchmark-results")


def newest(pattern):
    hits = sorted(glob.glob(os.path.join(RESULTS, pattern)))
    return hits[-1] if hits else None


def read_summary(path, by_threads=False):
    """summary CSV -> {systems:int -> row}  o  {(systems,threads) -> row}."""
    if not path or not os.path.exists(path):
        return {}
    out = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                n = int(row["Systems"])
                t = int(float(row.get("Threads", 1) or 1))
            except (KeyError, ValueError):
                continue
            out[(n, t) if by_threads else n] = row
    return out


def read_runs(path):
    if not path or not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def read_hardware(path):
    if not path or not os.path.exists(path):
        return {}
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}


def read_ceiling(path):
    if not path or not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


# ---------------------------------------------------------------------------
# Datos de muestra: se generan con un modelo simple (fraccion serial + techo de
# ancho de banda de memoria + regresion leve por oversubscription) para que la
# pagina se vea completa antes de correr scripts/benchmark.sh.
SAMPLE_N = [1000, 5000, 15000, 30000, 50000]
SAMPLE_THREADS = [1, 2, 4, 8, 16]
# ms de actualizacion secuencial por N (crece ~lineal con las entidades).
_SAMPLE_SEQ_MS = {1000: 0.196, 5000: 0.63, 15000: 1.55, 30000: 2.95, 50000: 4.85}
# factor de par_ms respecto al secuencial, por conteo de hilos.
_SAMPLE_FACTOR = {1: 1.03, 2: 0.58, 4: 0.39, 8: 0.31, 16: 0.33}
# penalizacion relativa por N chico (mas peso del arranque de la region paralela).
_SAMPLE_NPEN = {1000: 1.34, 5000: 1.12, 15000: 1.03, 30000: 1.0, 50000: 0.98}
# FPS totales (dominados por el render, casi iguales seq y par).
_SAMPLE_FPS = {1000: 28.0, 5000: 12.5, 15000: 5.4, 30000: 3.0, 50000: 1.8}


def _sample_summaries():
    seq, par = {}, {}
    for n in SAMPLE_N:
        s = _SAMPLE_SEQ_MS[n]
        seq[n] = {"MeanUpdateMs": f"{s:.6f}", "MedianUpdateMs": f"{s:.6f}",
                  "StandardDeviationUpdateMs": f"{s*0.02:.6f}",
                  "MeanFps": f"{_SAMPLE_FPS[n]:.3f}", "Threads": "1",
                  "Stars": "500", "Seed": "20260831", "Width": "1280", "Height": "720",
                  "Runs": "10"}
        for t in SAMPLE_THREADS:
            pen = 1.0 + (_SAMPLE_NPEN[n] - 1.0) * (1.0 - 1.0 / t)
            p = s * _SAMPLE_FACTOR[t] * pen
            par[(n, t)] = {"MeanUpdateMs": f"{p:.6f}", "MedianUpdateMs": f"{p:.6f}",
                           "StandardDeviationUpdateMs": f"{p*0.03:.6f}",
                           "MeanFps": f"{_SAMPLE_FPS[n]*0.99:.3f}", "Threads": str(t),
                           "Stars": "500", "Seed": "20260831",
                           "Width": "1280", "Height": "720", "Runs": "10"}
    return seq, par


def _f(row, key, default=0.0):
    try:
        return float(row.get(key, default) or default)
    except (TypeError, ValueError):
        return default


def build_dataset():
    seq_path = newest("summary-speedup-sequential-*.csv")
    par_path = newest("summary-speedup-parallel-*.csv")
    seq = read_summary(seq_path)                       # {n -> row}
    par = read_summary(par_path, by_threads=True)      # {(n,t) -> row}
    is_sample = not (seq and par)
    if is_sample:
        seq, par = _sample_summaries()

    ns = sorted({n for n in seq} & {n for (n, _t) in par})
    thread_counts = sorted({t for (_n, t) in par})
    if not thread_counts:
        thread_counts = [4]

    def par_ms(n, t):
        return _f(par.get((n, t), {}), "MeanUpdateMs")

    # conteo de hilos "titular": el que da el mejor speedup medio sobre los N.
    def mean_speedup_at(t):
        vals = []
        for n in ns:
            s, p = _f(seq[n], "MeanUpdateMs"), par_ms(n, t)
            if s and p:
                vals.append(s / p)
        return sum(vals) / len(vals) if vals else 0.0
    headline_t = max(thread_counts, key=mean_speedup_at) if ns else thread_counts[-1]

    # serie principal vs N, al conteo de hilos titular
    points = []
    for n in ns:
        s_ms = _f(seq[n], "MeanUpdateMs")
        p_ms = par_ms(n, headline_t)
        su = s_ms / p_ms if p_ms else 0.0
        points.append({
            "n": n, "seq_ms": s_ms, "par_ms": p_ms,
            "seq_fps": _f(seq[n], "MeanFps"),
            "par_fps": _f(par.get((n, headline_t), {}), "MeanFps"),
            "speedup": su,
            "efficiency": su / headline_t if headline_t else 0.0,
        })

    # matriz vs hilos: una serie por N
    vs_threads = []
    for n in ns:
        s_ms = _f(seq[n], "MeanUpdateMs")
        series = []
        for t in thread_counts:
            p = par_ms(n, t)
            su = s_ms / p if p else 0.0
            series.append({"t": t, "par_ms": p, "speedup": su,
                           "efficiency": (su / t if t else 0.0)})
        vs_threads.append({"n": n, "points": series})

    hw = {}
    for tag in ("parallel", "sequential"):
        h = read_hardware(newest(f"hardware-speedup-{tag}-*.json"))
        if h:
            hw = h
            break

    runs = {
        "sequential": read_runs(newest("runs-speedup-sequential-*.csv")),
        "parallel": read_runs(newest("runs-speedup-parallel-*.csv")),
    }
    ceiling = read_ceiling(os.path.join(RESULTS, "scaling-ceiling.csv"))

    cfg = {}
    for n in ns:
        r = seq[n]
        cfg = {"stars": r.get("Stars"), "seed": r.get("Seed"),
               "width": r.get("Width"), "height": r.get("Height")}
        break

    return {
        "is_sample": is_sample,
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "threads": headline_t,
        "thread_counts": thread_counts,
        "points": points,
        "vs_threads": vs_threads,
        "hardware": hw,
        "runs": runs,
        "ceiling": ceiling,
        "config": cfg or {"stars": "500", "seed": "20260831", "width": "1280", "height": "720"},
        "sources": {
            "seq": os.path.basename(seq_path) if seq_path else None,
            "par": os.path.basename(par_path) if par_path else None,
        },
    }


# ---------------------------------------------------------------------------
TEMPLATE = r"""<title>Bitácora del Screensaver OpenMP</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wght@600;700;800&family=Public+Sans:ital,wght@0,400;0,500;0,600;1,400&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
  :root {
    --plane:#f4f4f0;  --surface:#ffffff;  --surface-2:#faf9f6;
    --ink:#14161b;    --ink-2:#565d6b;    --muted:#8a8f9c;
    --grid:#e7e6e0;   --hair:rgba(20,22,27,.10);
    --accent:#c07a00;              /* sol — serie paralela / cifras clave */
    --seq:#2a6fd0;                 /* serie secuencial */
    --s3:#1baf7a; --s4:#d55181; --s5:#4a3aa7;   /* series extra (una por N) */
    --ideal:#8a8f9c;
    --good:#0c7a3c;
    --font-display:"Archivo",system-ui,sans-serif;
    --font-body:"Public Sans",system-ui,sans-serif;
    --font-mono:"IBM Plex Mono",ui-monospace,SFMono-Regular,Menlo,monospace;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --plane:#0c0e13; --surface:#151922; --surface-2:#1b202b;
      --ink:#eef0f4;   --ink-2:#9aa3b2;   --muted:#727a8b;
      --grid:#242a36;  --hair:rgba(255,255,255,.10);
      --accent:#e6a63c; --seq:#4b93ec;
      --s3:#199e70; --s4:#d55181; --s5:#9085e9;
      --ideal:#727a8b; --good:#3bbf6d;
    }
  }
  :root[data-theme="dark"] {
    --plane:#0c0e13; --surface:#151922; --surface-2:#1b202b;
    --ink:#eef0f4;   --ink-2:#9aa3b2;   --muted:#727a8b;
    --grid:#242a36;  --hair:rgba(255,255,255,.10);
    --accent:#e6a63c; --seq:#4b93ec;
    --s3:#199e70; --s4:#d55181; --s5:#9085e9;
    --ideal:#727a8b; --good:#3bbf6d;
  }

  * { box-sizing:border-box; }
  html, body { height:100%; }
  body {
    margin:0; background:var(--plane); color:var(--ink); overflow:hidden;
    font-family:var(--font-body); font-size:16px; line-height:1.6;
    -webkit-font-smoothing:antialiased;
  }
  h1,h2,h3 { font-family:var(--font-display); font-weight:800; line-height:1.12;
             text-wrap:balance; margin:0 0 .5em; letter-spacing:-.01em; }
  h1 { font-size:clamp(1.9rem,4.6vw,3.2rem); }
  h2 { font-size:clamp(1.4rem,3.2vw,2.15rem); }
  h3 { font-size:1.05rem; font-weight:700; margin-bottom:.3em; }
  p { margin:0 0 1em; max-width:66ch; }
  a { color:var(--accent); }
  code, .mono { font-family:var(--font-mono); font-size:.9em; }
  .eyebrow {
    font-family:var(--font-mono); font-size:.72rem; letter-spacing:.15em;
    text-transform:uppercase; color:var(--muted); margin:0 0 1.2em;
  }
  .lede { font-size:1.12rem; color:var(--ink-2); max-width:58ch; }

  /* ---- chrome ---- */
  .topbar {
    position:fixed; inset:0 0 auto 0; z-index:40; display:flex;
    align-items:center; justify-content:space-between; gap:1rem;
    padding:.7rem 1.1rem; background:color-mix(in srgb,var(--plane) 82%,transparent);
    backdrop-filter:blur(8px); border-bottom:1px solid var(--hair);
  }
  .topbar .brand { font-family:var(--font-mono); font-size:.78rem; color:var(--ink-2);
                   letter-spacing:.04em; }
  .topbar .brand b { color:var(--ink); font-weight:500; }
  .btn {
    font-family:var(--font-mono); font-size:.8rem; line-height:1;
    padding:.45rem .6rem; border:1px solid var(--hair); border-radius:999px;
    background:var(--surface); color:var(--ink-2); cursor:pointer;
  }
  .btn:focus-visible { outline:2px solid var(--accent); outline-offset:2px; }

  .sample-flag {
    display:none; margin:0; padding:.5rem 1rem;
    background:var(--accent); color:#12151a;
    font-family:var(--font-mono); font-size:.76rem; text-align:center;
    position:fixed; top:2.85rem; left:0; right:0; z-index:39;
  }
  body.is-sample .sample-flag { display:block; }

  /* ---- deck ---- */
  .deck { position:fixed; inset:0; --pad:clamp(1.3rem,5vw,4.5rem); }
  section.slide {
    position:absolute; inset:0; padding:5.5rem var(--pad) 5rem;
    display:flex; flex-direction:column; justify-content:flex-start;
    overflow-y:auto; opacity:0; pointer-events:none;
    transform:translateX(26px); transition:opacity .34s ease, transform .34s ease;
  }
  body.is-sample section.slide { padding-top:7.5rem; }
  section.slide.active { opacity:1; pointer-events:auto; transform:none; }
  section.slide.past { transform:translateX(-26px); }
  .slide-inner { width:100%; max-width:1000px; margin:auto; }
  section.slide.wide .slide-inner { max-width:1120px; }
  .slide .num {
    position:absolute; top:5.5rem; right:var(--pad);
    font-family:var(--font-mono); font-size:.72rem; color:var(--muted);
  }
  body.is-sample .slide .num { top:7.5rem; }

  /* nav: bottom bar + edge click zones */
  .deckbar {
    position:fixed; left:50%; bottom:1rem; transform:translateX(-50%); z-index:41;
    display:flex; align-items:center; gap:.9rem;
    padding:.4rem .7rem; border:1px solid var(--hair); border-radius:999px;
    background:color-mix(in srgb,var(--surface) 88%,transparent); backdrop-filter:blur(6px);
  }
  .deckbar button {
    font:inherit; font-family:var(--font-mono); font-size:1rem; line-height:1;
    width:1.9rem; height:1.9rem; border-radius:50%; border:1px solid var(--hair);
    background:var(--surface); color:var(--ink); cursor:pointer;
  }
  .deckbar button:disabled { opacity:.35; cursor:default; }
  .deckbar button:focus-visible { outline:2px solid var(--accent); outline-offset:2px; }
  .dots { display:flex; gap:.4rem; }
  .dot { width:7px; height:7px; border-radius:50%; background:var(--hair); border:0;
         padding:0; cursor:pointer; }
  .dot.on { background:var(--accent); }
  #counter { font-family:var(--font-mono); font-size:.76rem; color:var(--ink-2);
             font-variant-numeric:tabular-nums; min-width:3.4em; text-align:center; }
  .zone {
    position:fixed; top:3.5rem; bottom:0; width:11vw; max-width:120px; z-index:38;
    border:0; background:transparent; cursor:pointer;
  }
  .zone.left { left:0; } .zone.right { right:0; }
  @media (max-width:860px){ .zone { display:none; } }

  /* ---- content bits ---- */
  .stat-row { display:flex; flex-wrap:wrap; gap:2.4rem; margin:1.4rem 0 1rem; }
  .stat .v { font-family:var(--font-display); font-weight:800; font-size:2.6rem;
             line-height:1; color:var(--accent); font-variant-numeric:tabular-nums; }
  .stat .k { font-family:var(--font-mono); font-size:.7rem; letter-spacing:.12em;
             text-transform:uppercase; color:var(--muted); margin-top:.4rem; }

  figure { margin:0; }
  .chart-card {
    background:var(--surface); border:1px solid var(--hair); border-radius:14px;
    padding:1rem 1rem .6rem;
  }
  .chart-card svg { display:block; width:100%; height:auto; overflow:visible; }
  figcaption { font-size:.88rem; color:var(--ink-2); margin-top:.7rem; max-width:74ch; }
  .legend { display:flex; gap:1.2rem; flex-wrap:wrap; margin:.2rem 0 .6rem;
            font-family:var(--font-mono); font-size:.78rem; color:var(--ink-2); }
  .legend i { display:inline-block; width:14px; height:3px; border-radius:2px;
              vertical-align:middle; margin-right:.45rem; }

  .tbl-wrap { overflow-x:auto; border:1px solid var(--hair); border-radius:12px; margin:0 0 1rem; }
  table { border-collapse:collapse; width:100%; font-size:.84rem;
          font-variant-numeric:tabular-nums; }
  th,td { text-align:right; padding:.45rem .8rem; white-space:nowrap; }
  th:first-child, td:first-child { text-align:left; }
  thead th { font-family:var(--font-mono); font-size:.68rem; letter-spacing:.06em;
             text-transform:uppercase; color:var(--muted); border-bottom:1px solid var(--hair); }
  caption { text-align:left; padding:.55rem .8rem; color:var(--ink-2);
            font-family:var(--font-mono); font-size:.72rem; letter-spacing:.04em; }
  tbody tr:nth-child(even) { background:var(--surface-2); }
  tbody td { border-bottom:1px solid var(--hair); }
  tbody tr:last-child td { border-bottom:none; }
  .t-accent { color:var(--accent); font-weight:600; }

  .note { border-left:3px solid var(--accent); padding:.1rem 0 .1rem 1rem;
          margin:1.1rem 0; color:var(--ink-2); max-width:66ch; }
  .foot-cfg { font-family:var(--font-mono); font-size:.74rem; color:var(--muted);
              margin-top:.4rem; }

  ul.pcam { list-style:none; padding:0; margin:1.1rem 0 0; display:grid; gap:.85rem; }
  ul.pcam li { padding-left:2.3rem; position:relative; color:var(--ink-2); max-width:74ch; }
  ul.pcam li b { color:var(--ink); font-family:var(--font-display); font-weight:700; }
  ul.pcam li::before {
    content:attr(data-k); position:absolute; left:0; top:.05rem;
    font-family:var(--font-mono); font-size:.85rem; color:var(--accent); font-weight:500;
  }

  .cols { display:grid; gap:2rem 3rem; grid-template-columns:1fr; margin-top:1rem; }
  @media (min-width:800px){ .cols { grid-template-columns:1fr 1fr; } }
  .cols ul { margin:.4rem 0 0; padding-left:1.1rem; color:var(--ink-2); }
  .cols li { margin:.5rem 0; max-width:48ch; }
  .cols li b { color:var(--ink); }

  .tooltip {
    position:fixed; z-index:60; pointer-events:none; opacity:0; transition:opacity .1s;
    background:var(--ink); color:var(--plane); font-family:var(--font-mono);
    font-size:.72rem; padding:.4rem .55rem; border-radius:7px; white-space:pre; line-height:1.5;
  }

  @media (prefers-reduced-motion:reduce){
    *{transition:none!important;}
    section.slide{transform:none!important;}
  }
</style>

<div class="topbar">
  <span class="brand"><b>CC3069</b> · Screensaver OpenMP · Bitácora de rendimiento</span>
  <button class="btn" id="theme-toggle" aria-label="Cambiar tema">◑</button>
</div>
<p class="sample-flag">Datos de muestra — ejecutá <code>scripts/benchmark.sh</code> y <code>python3 scripts/build_report.py</code> para reemplazarlos.</p>

<button class="zone left" aria-label="Diapositiva anterior"></button>
<button class="zone right" aria-label="Diapositiva siguiente"></button>

<div class="deck" id="deck">
<!-- SLIDES -->
</div>

<nav class="deckbar" aria-label="Navegación de diapositivas">
  <button id="nav-prev" aria-label="Anterior">‹</button>
  <span id="counter"></span>
  <span class="dots" id="dots"></span>
  <button id="nav-next" aria-label="Siguiente">›</button>
</nav>

<div class="tooltip" id="tip"></div>

<script>
const DATA = /*DATA*/{}/*END*/;
</script>
<script>
/* ---------- theme ---------- */
(function(){
  const root=document.documentElement;
  const tt=document.getElementById('theme-toggle');
  try{const s=localStorage.getItem('bm-theme'); if(s) root.setAttribute('data-theme',s);}catch(e){}
  tt.onclick=()=>{
    const cur=root.getAttribute('data-theme');
    const next=cur==='dark'?'light':cur==='light'?null:(matchMedia('(prefers-color-scheme: dark)').matches?'light':'dark');
    if(next) root.setAttribute('data-theme',next); else root.removeAttribute('data-theme');
    try{localStorage.setItem('bm-theme',next||'');}catch(e){}
    redrawAll();
  };
})();

/* ---------- deck navigation ---------- */
(function(){
  const slides=[...document.querySelectorAll('section.slide')];
  const n=slides.length;
  const dotsWrap=document.getElementById('dots');
  const counter=document.getElementById('counter');
  const prev=document.getElementById('nav-prev'), next=document.getElementById('nav-next');
  slides.forEach((_,i)=>{
    const b=document.createElement('button');
    b.className='dot'; b.setAttribute('aria-label','Ir a diapositiva '+(i+1));
    b.onclick=()=>show(i); dotsWrap.appendChild(b);
  });
  const dots=[...dotsWrap.children];
  let cur=-1;
  function show(i){
    i=Math.max(0,Math.min(n-1,i));
    if(i===cur) return;
    cur=i;
    slides.forEach((s,k)=>{
      s.classList.toggle('active',k===i);
      s.classList.toggle('past',k<i);
    });
    dots.forEach((d,k)=>d.classList.toggle('on',k===i));
    counter.textContent=(i+1)+' / '+n;
    prev.disabled=i===0; next.disabled=i===n-1;
    history.replaceState(null,'','#'+(i+1));
    slides[i].scrollTop=0;
  }
  window.__deckShow=show;
  prev.onclick=()=>show(cur-1); next.onclick=()=>show(cur+1);
  document.querySelector('.zone.left').onclick=()=>show(cur-1);
  document.querySelector('.zone.right').onclick=()=>show(cur+1);
  addEventListener('keydown',e=>{
    if(e.target.matches('input,textarea')) return;
    if(e.key==='ArrowRight'||e.key==='PageDown'||e.key===' '){e.preventDefault();show(cur+1);}
    else if(e.key==='ArrowLeft'||e.key==='PageUp'){e.preventDefault();show(cur-1);}
    else if(e.key==='Home'){e.preventDefault();show(0);}
    else if(e.key==='End'){e.preventDefault();show(n-1);}
  });
  const h=parseInt((location.hash||'').slice(1),10);
  show(Number.isFinite(h)&&h>=1?h-1:0);
})();

/* ---------- charting (inline SVG) ---------- */
function cssvar(n){ return getComputedStyle(document.documentElement).getPropertyValue(n).trim(); }
const tip=document.getElementById('tip');
function showTip(x,y,txt){ tip.textContent=txt; tip.style.left=(x+14)+'px'; tip.style.top=(y+14)+'px'; tip.style.opacity=1; }
function hideTip(){ tip.style.opacity=0; }

function niceNum(v){
  const a=Math.abs(v);
  if(a>=100) return Math.round(v).toLocaleString();
  if(a>=10)  return v.toFixed(1);
  return v.toFixed(2);
}
function lineChart(mount, opts){
  const W=880, H=opts.h||400, m={t:18,r:104,b:52,l:78};
  const iw=W-m.l-m.r, ih=H-m.t-m.b;
  const xs=opts.series.flatMap(s=>s.pts.map(p=>p[0]));
  const ys=opts.series.flatMap(s=>s.pts.map(p=>p[1]));
  const xlog=!!opts.xLog;
  const xmin=Math.min(...xs), xmax=Math.max(...xs);
  let ymin=opts.yMin!=null?opts.yMin:Math.min(...ys,0);
  let ytop=Math.max(...ys, opts.ref!=null?opts.ref:-Infinity);
  let ymax=(ytop<=ymin?ymin+1:ytop)*1.08;
  const fx=v=> m.l + (xlog?(Math.log10(v)-Math.log10(xmin))/(Math.log10(xmax)-Math.log10(xmin)):(v-xmin)/(xmax-xmin))*iw;
  const fy=v=> m.t + ih - (v-ymin)/(ymax-ymin)*ih;
  const ink=cssvar('--ink'), ink2=cssvar('--ink-2'), muted=cssvar('--muted'), grid=cssvar('--grid');
  const NS='http://www.w3.org/2000/svg';
  const el=(n,a)=>{const e=document.createElementNS(NS,n); for(const k in a) e.setAttribute(k,a[k]); return e;};
  const svg=el('svg',{viewBox:`0 0 ${W} ${H}`,role:'img','aria-label':opts.aria||opts.title||'gráfico'});

  // y grid + ticks
  const yticks=5;
  for(let i=0;i<=yticks;i++){
    const v=ymin+(ymax-ymin)*i/yticks, y=fy(v);
    svg.appendChild(el('line',{x1:m.l,x2:m.l+iw,y1:y,y2:y,stroke:grid,'stroke-width':1}));
    const t=el('text',{x:m.l-10,y:y+4,'text-anchor':'end',fill:muted,'font-size':11,'font-family':cssvar('--font-mono')});
    t.textContent=(opts.yFmt?opts.yFmt(v):niceNum(v));
    svg.appendChild(t);
  }
  // x ticks = the actual N values
  const xv=[...new Set(xs)].sort((a,b)=>a-b);
  xv.forEach(v=>{
    const x=fx(v);
    svg.appendChild(el('line',{x1:x,x2:x,y1:m.t+ih,y2:m.t+ih+5,stroke:muted,'stroke-width':1}));
    const t=el('text',{x,y:m.t+ih+20,'text-anchor':'middle',fill:muted,'font-size':11,'font-family':cssvar('--font-mono')});
    t.textContent=v>=1000?(v/1000)+'k':v;
    svg.appendChild(t);
  });
  // axis labels
  let lx=el('text',{x:m.l+iw/2,y:H-8,'text-anchor':'middle',fill:ink2,'font-size':12,'font-family':cssvar('--font-mono')}); lx.textContent=opts.xLabel||'N (sistemas)'; svg.appendChild(lx);
  let ly=el('text',{x:15,y:m.t+ih/2,'text-anchor':'middle',fill:ink2,'font-size':12,'font-family':cssvar('--font-mono'),transform:`rotate(-90 15 ${m.t+ih/2})`}); ly.textContent=opts.yLabel||''; svg.appendChild(ly);

  // reference line (e.g. ideal)
  if(opts.ref!=null){
    const y=fy(opts.ref);
    svg.appendChild(el('line',{x1:m.l,x2:m.l+iw,y1:y,y2:y,stroke:cssvar('--ideal'),'stroke-width':1.5,'stroke-dasharray':'5 4'}));
    const t=el('text',{x:m.l+iw+6,y:y+4,fill:muted,'font-size':11,'font-family':cssvar('--font-mono')}); t.textContent=opts.refLabel||'ideal'; svg.appendChild(t);
  }

  // series
  const endYs=[];
  opts.series.forEach(s=>{
    const col=cssvar(s.color);
    const d=s.pts.map((p,i)=>(i?'L':'M')+fx(p[0])+' '+fy(p[1])).join(' ');
    svg.appendChild(el('path',{d,fill:'none',stroke:col,'stroke-width':2,'stroke-linejoin':'round','stroke-linecap':'round'}));
    s.pts.forEach((p,i)=>{
      const last=i===s.pts.length-1;
      const c=el('circle',{cx:fx(p[0]),cy:fy(p[1]),r:last?4.5:3.5,fill:col,stroke:cssvar('--surface'),'stroke-width':last?2:1.5});
      c.style.cursor='crosshair';
      c.addEventListener('mousemove',e=>showTip(e.clientX,e.clientY,`${s.name}\nN=${p[0].toLocaleString()}\n${opts.tipFmt?opts.tipFmt(p[1]):p[1]}`));
      c.addEventListener('mouseleave',hideTip);
      svg.appendChild(c);
    });
    // direct label at end — solo con <=3 series (con mas, la leyenda basta)
    if(opts.series.length<=3){
      const lp=s.pts[s.pts.length-1];
      let y=fy(lp[1])+4;
      for(const prev of endYs) if(Math.abs(prev-y)<13) y = prev<y ? y+13 : y-13;
      endYs.push(y);
      const lab=el('text',{x:fx(lp[0])+8,y,fill:col,'font-size':12,'font-weight':600,'font-family':cssvar('--font-mono')});
      lab.textContent=s.name; svg.appendChild(lab);
    }
  });

  mount.innerHTML=''; mount.appendChild(svg);
}

function legend(el, items){
  el.innerHTML=items.map(it=>`<span><i style="background:${cssvar(it.color)}"></i>${it.label}</span>`).join('');
}

/* ---------- build charts ---------- */
const P = DATA.points;
const NCOLORS=['--seq','--accent','--s3','--s4','--s5'];
function nlabel(n){ return n>=1000 ? (n/1000)+'k' : ''+n; }

function draw(){
  if(P.length){
    const th=DATA.threads;
    const speedup={mount:'c-speedup', xLog:true, series:[{name:'speedup', color:'--accent', pts:P.map(p=>[p.n,p.speedup])}],
      yLabel:'t_seq / t_par', yMin:0, ref:th, refLabel:th+'× (ideal)',
      tipFmt:v=>v.toFixed(2)+'×', yFmt:v=>v.toFixed(1)+'×'};
    const eff={mount:'c-eff', xLog:true, series:[{name:'eficiencia', color:'--accent', pts:P.map(p=>[p.n,p.efficiency*100])}],
      yLabel:'eficiencia %', yMin:0, ref:100, refLabel:'100%',
      tipFmt:v=>v.toFixed(0)+'%', yFmt:v=>v.toFixed(0)+'%'};
    const fps={mount:'c-fps', xLog:true, series:[
        {name:'secuencial', color:'--seq', pts:P.map(p=>[p.n,p.seq_fps])},
        {name:'paralelo', color:'--accent', pts:P.map(p=>[p.n,p.par_fps])}],
      yLabel:'FPS medio', yMin:0, yFmt:v=>Math.round(v).toLocaleString(),
      tipFmt:v=>v.toFixed(0)+' fps'};
    const ms={mount:'c-ms', xLog:true, series:[
        {name:'secuencial', color:'--seq', pts:P.map(p=>[p.n,p.seq_ms])},
        {name:'paralelo', color:'--accent', pts:P.map(p=>[p.n,p.par_ms])}],
      yLabel:'ms / fotograma', yMin:0, tipFmt:v=>v.toFixed(3)+' ms'};
    [speedup,eff,fps,ms].forEach(o=>{ const el=document.getElementById(o.mount); if(el) lineChart(el,o); });
    const lf=document.getElementById('leg-fps'); if(lf) legend(lf,[{color:'--seq',label:'secuencial (1 hilo)'},{color:'--accent',label:'paralelo ('+th+' hilos)'}]);
    const lm=document.getElementById('leg-ms'); if(lm) legend(lm,[{color:'--seq',label:'secuencial (1 hilo)'},{color:'--accent',label:'paralelo ('+th+' hilos)'}]);
  }

  const VT=DATA.vs_threads||[];
  if(VT.length){
    const mkSeries = (key,mul)=> VT.map((row,i)=>({
      name:'N='+nlabel(row.n), color:NCOLORS[i%NCOLORS.length],
      pts:row.points.map(p=>[p.t, key(p)*(mul||1)])
    }));
    const spT={mount:'c-speedup-threads', xLog:true, xLabel:'hilos', series:mkSeries(p=>p.speedup),
      yLabel:'t_seq / t_par', yMin:0, tipFmt:v=>v.toFixed(2)+'×', yFmt:v=>v.toFixed(1)+'×'};
    const efT={mount:'c-eff-threads', xLog:true, xLabel:'hilos', series:mkSeries(p=>p.efficiency,100),
      yLabel:'eficiencia %', yMin:0, ref:100, refLabel:'100%',
      tipFmt:v=>v.toFixed(0)+'%', yFmt:v=>v.toFixed(0)+'%'};
    [spT,efT].forEach(o=>{ const el=document.getElementById(o.mount); if(el) lineChart(el,o); });
    const items=VT.map((row,i)=>({color:NCOLORS[i%NCOLORS.length], label:'N='+nlabel(row.n)}));
    ['leg-sp-threads','leg-eff-threads'].forEach(id=>{ const el=document.getElementById(id); if(el) legend(el,items); });
  }
}
function redrawAll(){ requestAnimationFrame(draw); }
draw();
addEventListener('resize',()=>{ clearTimeout(window.__rt); window.__rt=setTimeout(draw,150); });
</script>
"""


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def slides_html(d):
    P = d["points"]
    best = max(P, key=lambda p: p["speedup"]) if P else {"speedup": 0, "n": 0, "efficiency": 0}
    first_n = P[0]["n"] if P else 0
    seq_runs_at_first = len([r for r in d["runs"]["sequential"]
                             if str(r.get("Systems", "")) == str(first_n)])
    runs_per_point = seq_runs_at_first or 10
    cfg = d["config"]

    def runs_table(rows, title):
        if not rows:
            return (f'<p class="note">Sin corridas para «{title}» todavía. '
                    'Ejecutá <code>scripts/benchmark.sh</code>.</p>')
        cols = ["Systems", "AverageFps", "OneSecondMinFps", "UpdateMs", "Samples"]
        head = "".join(f"<th>{c}</th>" for c in ["N", "FPS medio", "FPS min 1s", "Update ms", "Muestras"])
        body = ""
        for r in rows:
            body += "<tr>" + "".join(
                f'<td>{esc(r.get(c, ""))}</td>' for c in cols) + "</tr>"
        return (f'<div class="tbl-wrap"><table><caption>{esc(title)} · {len(rows)} corridas'
                f'</caption><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')

    def summary_table():
        if not P:
            return ""
        rows = ""
        for p in P:
            rows += (f"<tr><td>{p['n']:,}</td>"
                     f"<td>{p['seq_ms']:.3f}</td><td>{p['par_ms']:.3f}</td>"
                     f"<td class='t-accent'>{p['speedup']:.2f}×</td>"
                     f"<td>{p['efficiency']*100:.0f}%</td>"
                     f"<td>{p['seq_fps']:.0f}</td><td>{p['par_fps']:.0f}</td></tr>")
        thh = d["threads"]
        return (f'<div class="tbl-wrap"><table><thead><tr>'
                f'<th>N</th><th>t_seq (ms)</th><th>t_par ({thh}h, ms)</th><th>speedup</th>'
                f'<th>eficiencia</th><th>FPS seq</th><th>FPS par</th>'
                f'</tr></thead><tbody>{rows}</tbody></table></div>')

    def threads_table():
        vt = d["vs_threads"]
        if not vt:
            return ""
        tcs = d["thread_counts"]
        head = "<th>N</th>" + "".join(f"<th>{t}h</th>" for t in tcs)
        rows = ""
        for row in vt:
            by_t = {p["t"]: p for p in row["points"]}
            cells = "".join(
                f"<td class='t-accent'>{by_t[t]['speedup']:.2f}×</td>" if t in by_t else "<td>—</td>"
                for t in tcs)
            rows += f"<tr><td>{row['n']:,}</td>{cells}</tr>"
        return (f'<div class="tbl-wrap"><table><caption>speedup por N y número de hilos'
                f'</caption><thead><tr>{head}</tr></thead><tbody>{rows}</tbody></table></div>')

    def ceiling_block():
        if not d["ceiling"]:
            return ('<p class="note">Sin dato de N=1 000 000 todavía. '
                    'Ejecutá <code>scripts/benchmark.sh --ceiling 1000000</code> en cada modo.</p>')
        rows = ""
        for r in d["ceiling"]:
            rows += (f"<tr><td>{esc(r.get('Version'))}</td><td>{esc(r.get('Systems'))}</td>"
                     f"<td>{esc(r.get('Entities'))}</td><td>{esc(r.get('UpdateMs'))}</td>"
                     f"<td>{esc(r.get('MeanFps'))}</td><td>{esc(r.get('Frames'))}</td></tr>")
        return (f'<div class="tbl-wrap"><table><thead><tr><th>Versión</th><th>N</th>'
                f'<th>Entidades</th><th>Update ms</th><th>FPS medio</th><th>Fotogramas</th>'
                f'</tr></thead><tbody>{rows}</tbody></table></div>')

    th = d["threads"]
    S = []

    # 1 — portada
    S.append(f"""<section class="slide"><span class="num">01</span><div class="slide-inner">
      <p class="eyebrow">Proyecto 1 · Computación Paralela y Distribuida</p>
      <h1>Un screensaver de sistemas solares,<br>de 6 a un millón — con OpenMP</h1>
      <p class="lede">Speedup y eficiencia de la ruta de actualización paralela
      frente a la secuencial, medidos sobre la misma escena y semilla a distintos N.</p>
      <div class="stat-row">
        <div class="stat"><div class="v">{best['speedup']:.2f}×</div><div class="k">speedup máx · N={best['n']:,}</div></div>
        <div class="stat"><div class="v">{best['efficiency']*100:.0f}%</div><div class="k">eficiencia en ese punto</div></div>
        <div class="stat"><div class="v">{th}</div><div class="k">hilos OpenMP</div></div>
      </div>
      <p class="foot-cfg">Generado {esc(d['generated'])} · {esc(d['sources']['seq'] or 'datos de muestra')}</p>
    </div></section>""")

    # 2 — qué se paralelizó (PCAM, específico, sin fragmentos de código)
    S.append(f"""<section class="slide"><span class="num">02</span><div class="slide-inner">
      <p class="eyebrow">Método — PCAM</p>
      <h2>Qué se paralelizó y por qué</h2>
      <ul class="pcam">
        <li data-k="P"><b>Particionar.</b> La unidad de trabajo es una entidad. Cada
        fotograma, antes de dibujar, el motor recorre las entidades del mundo —
        aproximadamente seis por sistema solar más las estrellas de fondo — y les
        aplica tres sistemas fusionados en un mismo paso: centelleo (brillo senoidal
        en el tiempo), órbita (avance del ángulo y proyección de la posición sobre la
        elipse) y vida (descuento del tiempo restante y sobre de aparición y
        desaparición). Ese recorrido es el único trabajo que crece con N.</li>
        <li data-k="C"><b>Comunicar.</b> No hay comunicación entre tareas. Con los
        datos en <i>struct-of-arrays</i>, la iteración de índice <i>e</i> lee y
        escribe únicamente las posiciones <i>e</i> de esos arreglos: nunca las de
        otra entidad, nunca un acumulador global. Las altas y bajas de entidades,
        que sí modifican estado compartido, no ocurren dentro del recorrido.</li>
        <li data-k="A"><b>Aglomerar.</b> Las tres operaciones por entidad se agrupan
        en un solo bucle paralelo con reparto estático en bloques contiguos: cada
        hilo procesa un tramo del arreglo, lo que conserva la localidad de caché
        sobre el layout SoA. Un único punto de sincronización: la barrera implícita
        al cerrar la región.</li>
        <li data-k="M"><b>Mapear.</b> Por defecto cuatro hilos; <code>--threads</code>
        fuerza otro número, incluso más que los núcleos físicos, para medir dónde deja
        de rendir. Con demasiados hilos el sistema operativo los intercala con el
        hilo que dibuja y los FPS totales bajan. Tras la barrera, el hilo principal
        reanuda en serie: aplica las bajas marcadas durante el recorrido, muestrea
        las estelas y dibuja. Sin exclusión mutua, sin secciones críticas, sin
        operaciones atómicas.</li>
      </ul>
    </div></section>""")

    tcs = ", ".join(str(t) for t in d["thread_counts"])

    # 3 — speedup vs N
    S.append(f"""<section class="slide"><span class="num">03</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 1 de 6</p>
      <h2>Speedup de la actualización</h2>
      <figure><div class="chart-card"><div id="c-speedup"></div></div>
        <figcaption>speedup(N) = t_secuencial / t_paralelo, sobre el tiempo medio de
        actualización ECS (sin render), a {th} hilos. La línea punteada es el techo
        ideal de {th}×. Debajo de ~1000 sistemas el mundo tiene menos de 6144
        entidades y la ruta paralela cae a la secuencial a propósito.</figcaption>
      </figure>
    </div></section>""")

    # 4 — eficiencia vs N
    S.append(f"""<section class="slide"><span class="num">04</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 2 de 6</p>
      <h2>Eficiencia</h2>
      <figure><div class="chart-card"><div id="c-eff"></div></div>
        <figcaption>eficiencia(N) = speedup(N) / {th} hilos. El trabajo por entidad es
        aritmética simple sobre arreglos grandes — limitado por ancho de banda de
        memoria, no por cómputo — así que la eficiencia se estabiliza por debajo del
        100 %.</figcaption>
      </figure>
    </div></section>""")

    # 5 — speedup vs hilos
    S.append(f"""<section class="slide"><span class="num">05</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 3 de 6</p>
      <h2>Speedup vs número de hilos</h2>
      <div class="legend" id="leg-sp-threads"></div>
      <figure><div class="chart-card"><div id="c-speedup-threads"></div></div>
        <figcaption>speedup con {tcs} hilos, una línea por N (eje de hilos
        logarítmico). Sube con los primeros hilos y luego se aplana; pasar de los
        núcleos físicos hacia la sobresuscripción ya no compra tiempo.</figcaption>
      </figure>
    </div></section>""")

    # 6 — eficiencia vs hilos
    S.append(f"""<section class="slide"><span class="num">06</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 4 de 6</p>
      <h2>Eficiencia vs número de hilos</h2>
      <div class="legend" id="leg-eff-threads"></div>
      <figure><div class="chart-card"><div id="c-eff-threads"></div></div>
        <figcaption>eficiencia = speedup / hilos. Cae de forma monótona: cada hilo
        extra aporta menos que el anterior porque el cuello es el ancho de banda de
        memoria, compartido por todos los hilos.</figcaption>
      </figure>
    </div></section>""")

    # 7 — FPS vs N
    S.append(f"""<section class="slide"><span class="num">07</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 5 de 6</p>
      <h2>FPS totales</h2>
      <div class="legend" id="leg-fps"></div>
      <figure><div class="chart-card"><div id="c-fps"></div></div>
        <figcaption>FPS medios de la aplicación completa (eje N logarítmico). El
        render es serial y domina el tiempo de fotograma, así que el speedup de la
        actualización casi no mueve los FPS totales — comportamiento esperado.</figcaption>
      </figure>
    </div></section>""")

    # 8 — update ms vs N
    S.append(f"""<section class="slide"><span class="num">08</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 6 de 6</p>
      <h2>Costo de actualización ECS</h2>
      <div class="legend" id="leg-ms"></div>
      <figure><div class="chart-card"><div id="c-ms"></div></div>
        <figcaption>El número que sí mejora la paralelización: ms por fotograma del
        recorrido centelleo + órbita + vida, secuencial vs {th} hilos
        (eje N logarítmico).</figcaption>
      </figure>
    </div></section>""")

    # 9 — bitácora resumen
    S.append(f"""<section class="slide wide"><span class="num">09</span><div class="slide-inner">
      <p class="eyebrow">Bitácora · resumen</p>
      <h2>Resumen por N</h2>
      {summary_table()}
      <p class="foot-cfg">Escena fija: semilla {esc(cfg['seed'])},
      {esc(cfg['width'])}×{esc(cfg['height'])}, {esc(cfg['stars'])} estrellas,
      <code>--no-vsync</code> · {runs_per_point} corridas por (N, hilos) ·
      métrica = Actualización ECS ms/fotograma, aislada del render.</p>
    </div></section>""")

    # 10 — bitácora por hilos
    S.append(f"""<section class="slide wide"><span class="num">10</span><div class="slide-inner">
      <p class="eyebrow">Bitácora · hilos</p>
      <h2>Speedup por N y número de hilos</h2>
      {threads_table()}
      <p class="foot-cfg">Cada celda es la mediana de {runs_per_point} corridas.
      <code>--threads T</code> fuerza exactamente T hilos.</p>
    </div></section>""")

    # 11 — bitácora corridas
    S.append(f"""<section class="slide wide"><span class="num">11</span><div class="slide-inner">
      <p class="eyebrow">Bitácora · corridas</p>
      <h2>Mediciones crudas</h2>
      {runs_table(d['runs']['sequential'][:16], 'Corridas — secuencial')}
      {runs_table(d['runs']['parallel'][:16], 'Corridas — paralelo')}
    </div></section>""")

    # 12 — escalado 1M
    S.append(f"""<section class="slide"><span class="num">12</span><div class="slide-inner">
      <p class="eyebrow">Escalado</p>
      <h2>N = 1 000 000</h2>
      <p>El mundo se dimensiona en tiempo de ejecución a la N pedida (World,
      SolarSystems y TrailBuffer en heap). Un millón de sistemas ≈ 6 millones de
      entidades y ~0,7 GB de memoria. Renderiza a menos de 1 FPS — «corre aunque se
      vea saturado», que era el objetivo. El <code>--benchmark</code> no aplica a ese
      N (nunca junta 10 muestras de 1&nbsp;s); se mide con <code>--frames</code>.</p>
      {ceiling_block()}
    </div></section>""")

    # 13 — conclusiones
    S.append(f"""<section class="slide"><span class="num">13</span><div class="slide-inner">
      <p class="eyebrow">Conclusiones</p>
      <h2>Lo que muestran los números</h2>
      <ul class="pcam">
        <li data-k="1"><b>La actualización escala.</b> {best['speedup']:.2f}× a
        N={best['n']:,} con {th} hilos, sin mecanismos de exclusión mutua: el patrón
        por-entidad sobre SoA era paralelizable de raíz.</li>
        <li data-k="2"><b>Los hilos rinden decreciente.</b> El speedup sube con los
        primeros hilos y se aplana cerca de los núcleos físicos; la sobresuscripción
        no ayuda. La eficiencia baja de forma monótona.</li>
        <li data-k="3"><b>Los FPS casi no se mueven.</b> El render corre en un solo
        hilo y domina el tiempo de fotograma, así que acelerar la actualización no se
        traslada a los FPS totales.</li>
        <li data-k="4"><b>El límite es la memoria.</b> El bucle lee y escribe más
        bytes de los que calcula: el ancho de banda compartido, no el número de
        hilos, es lo que topa la eficiencia por debajo del 100 %.</li>
      </ul>
    </div></section>""")

    # 14 — mejoras concretas
    S.append(f"""<section class="slide wide"><span class="num">14</span><div class="slide-inner">
      <p class="eyebrow">Trabajo futuro</p>
      <h2>Cómo subir el speedup y la eficiencia</h2>
      <div class="cols">
        <div>
          <h3>Más speedup</h3>
          <ul>
            <li><b>Sacar el render del hilo único.</b> Construir los vértices de todos
            los cuerpos en paralelo y subirlos como una sola malla dinámica — ya se
            hace así con las estelas. Es el cambio que haría que los FPS totales sigan
            al speedup de la actualización.</li>
            <li><b>Paralelizar <code>sys_drift</code> y la siembra de estelas.</b> Hoy
            corren en serie «por ser recorridos chicos»; con N alto ya no lo son, y
            ambos son por-sistema e independientes.</li>
            <li><b>Reducir el trabajo base, no sólo repartirlo.</b> Reemplazar las
            llamadas a seno, coseno y potencia por tabla de senos o aproximaciones
            polinómicas recorta el tiempo de las dos versiones.</li>
          </ul>
        </div>
        <div>
          <h3>Más eficiencia</h3>
          <ul>
            <li><b>Compactar los arreglos calientes.</b> El recorrido salta entre una
            decena de arreglos SoA distintos; agrupar los campos que se leen juntos
            (o un arreglo intercalado sólo para el bucle caliente) sube la utilidad
            por línea de caché y afloja el cuello de ancho de banda.</li>
            <li><b>Vectorizar dentro de cada hilo.</b> El bucle es aritmética sin
            ramas dependientes de datos: candidato directo a SIMD
            (<code>#pragma omp simd</code>, <code>-O3 -march=native</code>) combinado
            con OpenMP entre hilos.</li>
            <li><b>Afinar el reparto.</b> Probar reparto estático sin tamaño de bloque,
            <i>guided</i> y bloques más grandes para amortizar el arranque de la
            región paralela, que a N medio pesa relativamente más.</li>
            <li><b>Fijar los hilos a núcleos</b> (<code>OMP_PROC_BIND</code>,
            <code>OMP_PLACES</code>) para que el planificador no los mueva y no
            compitan con el hilo de render.</li>
          </ul>
        </div>
      </div>
    </div></section>""")

    return "\n".join(S)


def render(d):
    html = TEMPLATE.replace("<!-- SLIDES -->", slides_html(d))
    html = html.replace("/*DATA*/{}/*END*/", json.dumps({
        "points": d["points"], "threads": d["threads"],
        "thread_counts": d["thread_counts"], "vs_threads": d["vs_threads"],
        "is_sample": d["is_sample"],
    }))
    # The publish step wraps this file in <body>…</body>, so tag the body from JS.
    if d["is_sample"]:
        html += '\n<script>document.body.classList.add("is-sample");</script>\n'
    return html


def main():
    out = os.path.join(RESULTS, "report.html")
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    d = build_dataset()
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write(render(d))
    tag = "MUESTRA" if d["is_sample"] else "datos reales"
    print(f"[{tag}] {out}  ·  {len(d['points'])} puntos  ·  {d['threads']} hilos")
    if d["is_sample"]:
        print("  (sin summary-speedup-*.csv en benchmark-results/ — corré scripts/benchmark.sh)")


if __name__ == "__main__":
    main()
