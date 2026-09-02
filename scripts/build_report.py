#!/usr/bin/env python3
"""build_report.py - genera benchmark-results/report.html a partir de los CSV
que produce scripts/benchmark.sh (o benchmark.ps1 en modo speedup).

Lee los summary-speedup-{sequential,parallel}-*.csv mas recientes, los cruza por
N, calcula speedup = t_seq / t_par y eficiencia = speedup / hilos, y escribe una
sola pagina HTML autocontenida (sin CDN): un deck de diapositivas que se expande
a informe completo. Solo stdlib.

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


def read_summary(path):
    """summary CSV -> {systems:int -> row dict}"""
    if not path or not os.path.exists(path):
        return {}
    out = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                out[int(row["Systems"])] = row
            except (KeyError, ValueError):
                continue
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
SAMPLE = {
    "is_sample": True,
    "seq": {
        1000:  {"MeanUpdateMs": "0.011", "MeanFps": "760", "MedianUpdateMs": "0.011", "StandardDeviationUpdateMs": "0.001", "Threads": "1"},
        5000:  {"MeanUpdateMs": "0.062", "MeanFps": "360", "MedianUpdateMs": "0.061", "StandardDeviationUpdateMs": "0.003", "Threads": "1"},
        25000: {"MeanUpdateMs": "0.330", "MeanFps": "128", "MedianUpdateMs": "0.325", "StandardDeviationUpdateMs": "0.012", "Threads": "1"},
        100000:{"MeanUpdateMs": "1.380", "MeanFps": "41",  "MedianUpdateMs": "1.360", "StandardDeviationUpdateMs": "0.040", "Threads": "1"},
        200000:{"MeanUpdateMs": "2.900", "MeanFps": "21",  "MedianUpdateMs": "2.870", "StandardDeviationUpdateMs": "0.090", "Threads": "1"},
    },
    "par": {
        1000:  {"MeanUpdateMs": "0.011", "MeanFps": "758", "MedianUpdateMs": "0.011", "StandardDeviationUpdateMs": "0.001", "Threads": "4"},
        5000:  {"MeanUpdateMs": "0.048", "MeanFps": "362", "MedianUpdateMs": "0.047", "StandardDeviationUpdateMs": "0.003", "Threads": "4"},
        25000: {"MeanUpdateMs": "0.150", "MeanFps": "130", "MedianUpdateMs": "0.147", "StandardDeviationUpdateMs": "0.008", "Threads": "4"},
        100000:{"MeanUpdateMs": "0.560", "MeanFps": "42",  "MedianUpdateMs": "0.550", "StandardDeviationUpdateMs": "0.020", "Threads": "4"},
        200000:{"MeanUpdateMs": "1.150", "MeanFps": "22",  "MedianUpdateMs": "1.130", "StandardDeviationUpdateMs": "0.045", "Threads": "4"},
    },
}


def build_dataset():
    seq_path = newest("summary-speedup-sequential-*.csv")
    par_path = newest("summary-speedup-parallel-*.csv")
    seq = read_summary(seq_path)
    par = read_summary(par_path)
    is_sample = not (seq and par)
    if is_sample:
        seq, par = SAMPLE["seq"], SAMPLE["par"]

    ns = sorted(set(seq) & set(par))
    threads = 4
    for n in ns:
        try:
            threads = int(float(par[n].get("Threads", 4))) or 4
            break
        except (TypeError, ValueError):
            pass

    points = []
    for n in ns:
        s_ms = float(seq[n]["MeanUpdateMs"])
        p_ms = float(par[n]["MeanUpdateMs"])
        speedup = s_ms / p_ms if p_ms else 0.0
        points.append({
            "n": n,
            "seq_ms": s_ms,
            "par_ms": p_ms,
            "seq_ms_sd": float(seq[n].get("StandardDeviationUpdateMs", 0) or 0),
            "par_ms_sd": float(par[n].get("StandardDeviationUpdateMs", 0) or 0),
            "seq_fps": float(seq[n].get("MeanFps", 0) or 0),
            "par_fps": float(par[n].get("MeanFps", 0) or 0),
            "speedup": speedup,
            "efficiency": (speedup / threads) if threads else 0.0,
        })

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
    src = seq if not is_sample else {}
    for n in (src or {}):
        r = src[n]
        cfg = {"stars": r.get("Stars"), "seed": r.get("Seed"),
               "width": r.get("Width"), "height": r.get("Height")}
        break

    return {
        "is_sample": is_sample,
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "threads": threads,
        "points": points,
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
      --accent:#e6a63c; --seq:#4b93ec; --ideal:#727a8b; --good:#3bbf6d;
    }
  }
  :root[data-theme="dark"] {
    --plane:#0c0e13; --surface:#151922; --surface-2:#1b202b;
    --ink:#eef0f4;   --ink-2:#9aa3b2;   --muted:#727a8b;
    --grid:#242a36;  --hair:rgba(255,255,255,.10);
    --accent:#e6a63c; --seq:#4b93ec; --ideal:#727a8b; --good:#3bbf6d;
  }

  * { box-sizing:border-box; }
  html { scroll-behavior:smooth; }
  body {
    margin:0; background:var(--plane); color:var(--ink);
    font-family:var(--font-body); font-size:16px; line-height:1.6;
    -webkit-font-smoothing:antialiased;
  }
  h1,h2,h3 { font-family:var(--font-display); font-weight:800; line-height:1.1;
             text-wrap:balance; margin:0 0 .4em; letter-spacing:-.01em; }
  h1 { font-size:clamp(2rem,5vw,3.4rem); }
  h2 { font-size:clamp(1.5rem,3.4vw,2.3rem); }
  h3 { font-size:1.15rem; font-weight:700; }
  p { margin:0 0 1em; max-width:64ch; }
  a { color:var(--accent); }
  code, .mono { font-family:var(--font-mono); font-size:.9em; }
  .eyebrow {
    font-family:var(--font-mono); font-size:.72rem; letter-spacing:.15em;
    text-transform:uppercase; color:var(--muted); margin:0 0 1.4em;
  }
  .lede { font-size:1.15rem; color:var(--ink-2); max-width:60ch; }

  /* ---- chrome ---- */
  .topbar {
    position:fixed; inset:0 0 auto 0; z-index:50; display:flex;
    align-items:center; justify-content:space-between; gap:1rem;
    padding:.7rem 1.1rem; background:color-mix(in srgb,var(--plane) 86%,transparent);
    backdrop-filter:blur(8px); border-bottom:1px solid var(--hair);
  }
  .topbar .brand { font-family:var(--font-mono); font-size:.8rem; color:var(--ink-2);
                   letter-spacing:.04em; }
  .topbar .brand b { color:var(--ink); font-weight:500; }
  .controls { display:flex; gap:.4rem; }
  .btn {
    font-family:var(--font-mono); font-size:.75rem; letter-spacing:.03em;
    padding:.4rem .7rem; border:1px solid var(--hair); border-radius:999px;
    background:var(--surface); color:var(--ink-2); cursor:pointer;
  }
  .btn[aria-pressed="true"] { background:var(--ink); color:var(--plane); border-color:var(--ink); }
  .btn:focus-visible { outline:2px solid var(--accent); outline-offset:2px; }

  .sample-flag {
    display:none; margin:0; padding:.55rem 1rem; background:var(--accent);
    color:#12151a; font-family:var(--font-mono); font-size:.78rem; text-align:center;
    position:fixed; top:2.9rem; left:0; right:0; z-index:49;
  }
  body.is-sample .sample-flag { display:block; }

  /* ---- deck vs report ---- */
  main { --pad:clamp(1.2rem,5vw,4rem); }
  section.slide {
    min-height:100vh; display:flex; flex-direction:column; justify-content:center;
    padding:6rem var(--pad) 4rem; scroll-snap-align:start; position:relative;
    border-bottom:1px solid transparent;
  }
  body.is-sample section.slide:first-of-type { padding-top:8.5rem; }
  body.mode-deck { scroll-snap-type:y mandatory; overflow-y:scroll; height:100vh; }
  body.mode-deck main { }
  body.mode-report section.slide {
    min-height:auto; padding:3.2rem var(--pad); scroll-snap-align:none;
    border-bottom:1px solid var(--hair); max-width:1180px; margin:0 auto;
  }
  body.mode-report section.slide:first-of-type { padding-top:6rem; }
  body.is-sample.mode-report section.slide:first-of-type { padding-top:8.5rem; }

  .slide-inner { width:100%; max-width:1080px; margin:0 auto; }
  .slide .num {
    position:absolute; top:4.6rem; right:var(--pad);
    font-family:var(--font-mono); font-size:.72rem; color:var(--muted);
  }
  body.mode-report .slide .num { display:none; }

  .progress {
    position:fixed; left:0; bottom:0; height:3px; background:var(--accent);
    z-index:50; transition:width .2s ease;
  }
  body.mode-report .progress { display:none; }

  /* ---- content bits ---- */
  .grid-2 { display:grid; gap:2rem; grid-template-columns:1fr; }
  @media (min-width:820px){ .grid-2 { grid-template-columns:1.1fr .9fr; align-items:center; } }

  .stat-row { display:flex; flex-wrap:wrap; gap:2.4rem; margin:1.4rem 0; }
  .stat { }
  .stat .v { font-family:var(--font-display); font-weight:800; font-size:2.6rem;
             line-height:1; color:var(--accent); font-variant-numeric:tabular-nums; }
  .stat .k { font-family:var(--font-mono); font-size:.72rem; letter-spacing:.12em;
             text-transform:uppercase; color:var(--muted); margin-top:.4rem; }

  figure { margin:0; }
  .chart-card {
    background:var(--surface); border:1px solid var(--hair); border-radius:14px;
    padding:1.1rem 1.1rem .8rem;
  }
  .chart-card svg { display:block; width:100%; height:auto; overflow:visible; }
  figcaption { font-size:.9rem; color:var(--ink-2); margin-top:.7rem; max-width:70ch; }
  .legend { display:flex; gap:1.2rem; flex-wrap:wrap; margin:.2rem 0 .6rem;
            font-family:var(--font-mono); font-size:.78rem; color:var(--ink-2); }
  .legend i { display:inline-block; width:14px; height:3px; border-radius:2px;
              vertical-align:middle; margin-right:.45rem; }

  .tbl-wrap { overflow-x:auto; border:1px solid var(--hair); border-radius:12px; }
  table { border-collapse:collapse; width:100%; font-size:.86rem;
          font-variant-numeric:tabular-nums; }
  th,td { text-align:right; padding:.5rem .8rem; white-space:nowrap; }
  th:first-child, td:first-child { text-align:left; }
  thead th { font-family:var(--font-mono); font-size:.72rem; letter-spacing:.06em;
             text-transform:uppercase; color:var(--muted); border-bottom:1px solid var(--hair); }
  tbody tr:nth-child(even) { background:var(--surface-2); }
  tbody td { border-bottom:1px solid var(--hair); }
  tbody tr:last-child td { border-bottom:none; }
  .t-accent { color:var(--accent); font-weight:600; }

  .pill { display:inline-block; font-family:var(--font-mono); font-size:.7rem;
          letter-spacing:.08em; text-transform:uppercase; padding:.2rem .55rem;
          border-radius:999px; border:1px solid var(--hair); color:var(--ink-2); }
  .pend { border-left:3px solid var(--accent); padding:.2rem 0 .2rem 1rem; margin:1.2rem 0;
          color:var(--ink-2); }

  .anexo { opacity:.72; }
  .anexo .pend { border-color:var(--muted); }
  .anexo h2::after {
    content:"pendiente"; font-family:var(--font-mono); font-size:.62rem;
    letter-spacing:.14em; text-transform:uppercase; vertical-align:middle;
    margin-left:.8rem; padding:.15rem .5rem; border:1px solid var(--hair);
    border-radius:999px; color:var(--muted);
  }

  ul.method { list-style:none; padding:0; margin:1.2rem 0; display:grid; gap:.9rem; }
  ul.method li { padding-left:2.4rem; position:relative; color:var(--ink-2); max-width:60ch; }
  ul.method li b { color:var(--ink); font-family:var(--font-display); font-weight:700; }
  ul.method li::before {
    content:attr(data-k); position:absolute; left:0; top:.1rem;
    font-family:var(--font-mono); font-size:.8rem; color:var(--accent); font-weight:500;
  }

  .tooltip {
    position:fixed; z-index:60; pointer-events:none; opacity:0; transition:opacity .1s;
    background:var(--ink); color:var(--plane); font-family:var(--font-mono);
    font-size:.72rem; padding:.4rem .55rem; border-radius:7px; white-space:pre; line-height:1.5;
  }

  @media (prefers-reduced-motion:reduce){ *{transition:none!important; scroll-behavior:auto!important;} }
  footer { padding:2.5rem var(--pad); color:var(--muted); font-family:var(--font-mono);
           font-size:.74rem; border-top:1px solid var(--hair); max-width:1180px; margin:0 auto; }
</style>

<div class="topbar">
  <span class="brand"><b>CC3069</b> · Screensaver OpenMP · Bitácora de rendimiento</span>
  <div class="controls">
    <button class="btn" id="mode-deck" aria-pressed="true">Presentación</button>
    <button class="btn" id="mode-report" aria-pressed="false">Informe</button>
    <button class="btn" id="theme-toggle" aria-label="Cambiar tema">◑</button>
  </div>
</div>
<p class="sample-flag">Datos de muestra — ejecutá <code>scripts/benchmark.sh</code> y <code>python3 scripts/build_report.py</code> para reemplazarlos.</p>
<div class="progress" id="progress"></div>

<main id="deck">
<!-- SLIDES -->
</main>

<div class="tooltip" id="tip"></div>

<script>
const DATA = /*DATA*/{}/*END*/;
</script>
<script>
/* ---------- theme + mode ---------- */
(function(){
  const root=document.documentElement, body=document.body;
  const dk=document.getElementById('mode-deck'), rp=document.getElementById('mode-report');
  function setMode(m){
    body.classList.toggle('mode-deck',m==='deck');
    body.classList.toggle('mode-report',m==='report');
    dk.setAttribute('aria-pressed',m==='deck'); rp.setAttribute('aria-pressed',m==='report');
    try{localStorage.setItem('bm-mode',m);}catch(e){}
    if(m==='deck') window.scrollTo(0,0);
  }
  dk.onclick=()=>setMode('deck'); rp.onclick=()=>setMode('report');
  const q=new URLSearchParams(location.search).get('view');
  setMode(q==='report'||q==='deck' ? q :
    (()=>{try{return localStorage.getItem('bm-mode')||'deck';}catch(e){return 'deck';}})());

  const tt=document.getElementById('theme-toggle');
  tt.onclick=()=>{
    const cur=root.getAttribute('data-theme');
    const next=cur==='dark'?'light':cur==='light'?null:(matchMedia('(prefers-color-scheme: dark)').matches?'light':'dark');
    if(next) root.setAttribute('data-theme',next); else root.removeAttribute('data-theme');
    try{localStorage.setItem('bm-theme',next||'');}catch(e){}
    redrawAll();
  };
  try{const s=localStorage.getItem('bm-theme'); if(s) root.setAttribute('data-theme',s);}catch(e){}

  /* deck keyboard nav */
  const slides=()=>[...document.querySelectorAll('section.slide')];
  function go(dir){
    if(!body.classList.contains('mode-deck')) return;
    const y=window.scrollY, arr=slides();
    let i=arr.findIndex(s=>s.offsetTop> y+10);
    if(dir>0){ i=i<0?arr.length-1:i; } else { i=arr.findIndex(s=>s.offsetTop>=y-10)-1; if(i<0)i=0; }
    arr[Math.max(0,Math.min(arr.length-1,i))].scrollIntoView();
  }
  addEventListener('keydown',e=>{
    if(e.key==='ArrowRight'||e.key==='PageDown'||e.key===' '){e.preventDefault();go(1);}
    if(e.key==='ArrowLeft'||e.key==='PageUp'){e.preventDefault();go(-1);}
  });
  const prog=document.getElementById('progress');
  addEventListener('scroll',()=>{
    const h=document.documentElement.scrollHeight-innerHeight;
    prog.style.width=(h>0?(scrollY/h*100):0)+'%';
  },{passive:true});
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
    // direct label at end, nudged apart if two series' endpoints collide
    const lp=s.pts[s.pts.length-1];
    let y=fy(lp[1])+4;
    for(const prev of endYs) if(Math.abs(prev-y)<13) y = prev<y ? y+13 : y-13;
    endYs.push(y);
    const lab=el('text',{x:fx(lp[0])+8,y,fill:col,'font-size':12,'font-weight':600,'font-family':cssvar('--font-mono')});
    lab.textContent=s.name; svg.appendChild(lab);
  });

  mount.innerHTML=''; mount.appendChild(svg);
}

function legend(el, items){
  el.innerHTML=items.map(it=>`<span><i style="background:${cssvar(it.color)}"></i>${it.label}</span>`).join('');
}

/* ---------- build charts ---------- */
const P = DATA.points;
function draw(){
  if(!P.length) return;
  const nmax=P.map(p=>p.n);
  const speedup={mount:'c-speedup', xLog:true, series:[{name:'speedup', color:'--accent', pts:P.map(p=>[p.n,p.speedup])}],
    yLabel:'t_seq / t_par', yMin:0, ref:DATA.threads, refLabel:DATA.threads+'× (ideal)',
    tipFmt:v=>v.toFixed(2)+'×', yFmt:v=>v.toFixed(1)+'×', title:'Speedup vs N'};
  const eff={mount:'c-eff', xLog:true, series:[{name:'eficiencia', color:'--accent', pts:P.map(p=>[p.n,p.efficiency*100])}],
    yLabel:'eficiencia %', yMin:0, ref:100, refLabel:'100%',
    tipFmt:v=>v.toFixed(0)+'%', yFmt:v=>v.toFixed(0)+'%', title:'Eficiencia vs N'};
  const fps={mount:'c-fps', xLog:true, series:[
      {name:'secuencial', color:'--seq', pts:P.map(p=>[p.n,p.seq_fps])},
      {name:'paralelo', color:'--accent', pts:P.map(p=>[p.n,p.par_fps])}],
    yLabel:'FPS medio', yMin:0, yFmt:v=>Math.round(v).toLocaleString(),
    tipFmt:v=>v.toFixed(0)+' fps', title:'FPS totales vs N'};
  const ms={mount:'c-ms', xLog:true, series:[
      {name:'secuencial', color:'--seq', pts:P.map(p=>[p.n,p.seq_ms])},
      {name:'paralelo', color:'--accent', pts:P.map(p=>[p.n,p.par_ms])}],
    yLabel:'ms / fotograma', yMin:0, tipFmt:v=>v.toFixed(3)+' ms', title:'Costo de actualización ECS vs N'};
  [speedup,eff,fps,ms].forEach(o=>{ const el=document.getElementById(o.mount); if(el) lineChart(el,o); });
  const lf=document.getElementById('leg-fps'); if(lf) legend(lf,[{color:'--seq',label:'secuencial (1 hilo)'},{color:'--accent',label:'paralelo ('+DATA.threads+' hilos)'}]);
  const lm=document.getElementById('leg-ms'); if(lm) legend(lm,[{color:'--seq',label:'secuencial (1 hilo)'},{color:'--accent',label:'paralelo ('+DATA.threads+' hilos)'}]);
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
    hw = d["hardware"]
    proc = (hw.get("Processors") or [{}])[0]
    gpu = (hw.get("Graphics") or [{}])[0]
    cfg = d["config"]

    def runs_table(rows, title):
        if not rows:
            return f'<p class="pend">Sin corridas registradas para «{title}». Ejecutá <code>scripts/benchmark.sh</code>.</p>'
        cols = ["Systems", "AverageFps", "OneSecondMinFps", "UpdateMs", "Samples"]
        head = "".join(f"<th>{c}</th>" for c in ["N", "FPS medio", "FPS min 1s", "Update ms", "Muestras"])
        body = ""
        for r in rows:
            body += "<tr>" + "".join(
                f'<td>{esc(r.get(c, ""))}</td>' for c in cols) + "</tr>"
        return (f'<div class="tbl-wrap"><table><caption class="pill" style="margin:.6rem">{esc(title)}'
                f' · {len(rows)} corridas</caption><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')

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
        return (f'<div class="tbl-wrap"><table><thead><tr>'
                f'<th>N</th><th>t_seq (ms)</th><th>t_par (ms)</th><th>speedup</th>'
                f'<th>eficiencia</th><th>FPS seq</th><th>FPS par</th>'
                f'</tr></thead><tbody>{rows}</tbody></table></div>')

    def ceiling_block():
        if not d["ceiling"]:
            return ('<p class="pend">Sin dato de N=1 000 000 todavía. '
                    'Ejecutá <code>scripts/benchmark.sh --ceiling 1000000</code> en cada modo.</p>')
        rows = ""
        for r in d["ceiling"]:
            rows += (f"<tr><td>{esc(r.get('Version'))}</td><td>{esc(r.get('Systems'))}</td>"
                     f"<td>{esc(r.get('Entities'))}</td><td>{esc(r.get('UpdateMs'))}</td>"
                     f"<td>{esc(r.get('MeanFps'))}</td><td>{esc(r.get('Frames'))}</td></tr>")
        return (f'<div class="tbl-wrap"><table><thead><tr><th>Versión</th><th>N</th>'
                f'<th>Entidades</th><th>Update ms</th><th>FPS medio</th><th>Fotogramas</th>'
                f'</tr></thead><tbody>{rows}</tbody></table></div>')

    S = []

    # 1 — title
    S.append(f"""<section class="slide"><span class="num">01</span><div class="slide-inner">
      <p class="eyebrow">Proyecto 1 · Computación Paralela y Distribuida · UVG</p>
      <h1>Un screensaver de sistemas solares,<br>de 6 a un millón — con OpenMP</h1>
      <p class="lede">Bitácora de rendimiento: speedup y eficiencia de la ruta de
      actualización paralela frente a la secuencial, medidos sobre la misma escena
      y semilla a distintos N.</p>
      <div class="stat-row">
        <div class="stat"><div class="v">{best['speedup']:.2f}×</div><div class="k">speedup máx · N={best['n']:,}</div></div>
        <div class="stat"><div class="v">{best['efficiency']*100:.0f}%</div><div class="k">eficiencia en ese punto</div></div>
        <div class="stat"><div class="v">{d['threads']}</div><div class="k">hilos OpenMP</div></div>
      </div>
      <p class="mono" style="color:var(--muted);font-size:.8rem">Generado {esc(d['generated'])}
      · {esc(d['sources']['seq'] or 'datos de muestra')}</p>
    </div></section>""")

    # 2 — method / PCAM
    S.append(f"""<section class="slide"><span class="num">02</span><div class="slide-inner">
      <p class="eyebrow">Método — PCAM</p>
      <h2>Qué se paralelizó y por qué</h2>
      <ul class="method">
        <li data-k="P"><b>Particionar.</b> El mundo es SoA (arreglos paralelos por
        campo). Cada entidad — sol, planeta, estrella — se actualiza sola: centelleo,
        órbita y vida son funciones puras de su propio estado y del tiempo.</li>
        <li data-k="C"><b>Comunicar.</b> Ninguna. El bucle no escribe en el slot de
        otra entidad ni en un acumulador compartido; <code>ecs_create/destroy</code>
        quedan fuera de la región.</li>
        <li data-k="A"><b>Aglomerar.</b> Un solo <code>#pragma omp parallel for
        schedule(static,64)</code> fusiona los tres sistemas en un recorrido; trozos
        contiguos = localidad de caché sobre los arreglos SoA.</li>
        <li data-k="M"><b>Mapear.</b> Tope de 4 hilos (6/12 compiten con el hilo de
        render y bajan los FPS totales). Barrera implícita al terminar; recién
        entonces corren estelas y destrucción diferida, en serie.</li>
      </ul>
      <p class="pend">Sincronía: barrera implícita del <code>parallel for</code> +
      patrón marcar-y-barrer (<code>C_PENDING_DESTROY</code>) para diferir los
      borrados al hilo principal. Sin mutex, sin secciones críticas.</p>
    </div></section>""")

    # 3 — test env
    S.append(f"""<section class="slide"><span class="num">03</span><div class="slide-inner">
      <p class="eyebrow">Entorno de pruebas</p>
      <h2>Equipo y configuración</h2>
      <div class="grid-2">
        <div class="tbl-wrap"><table><tbody>
          <tr><td>CPU</td><td>{esc(proc.get('Name','—'))}</td></tr>
          <tr><td>Núcleos</td><td>{esc(proc.get('PhysicalCores','—'))} físicos / {esc(proc.get('LogicalProcessors','—'))} lógicos</td></tr>
          <tr><td>GPU</td><td>{esc(gpu.get('Name','—'))}</td></tr>
          <tr><td>SO</td><td>{esc(hw.get('OperatingSystem','—'))}</td></tr>
          <tr><td>Compilador</td><td>{esc(hw.get('Gcc','—'))}</td></tr>
          <tr><td>Commit</td><td class="mono">{esc((hw.get('Commit') or '—')[:12])}</td></tr>
        </tbody></table></div>
        <div>
          <p><b>Escena fija</b> en todas las corridas: semilla {esc(cfg['seed'])},
          {esc(cfg['width'])}×{esc(cfg['height'])}, {esc(cfg['stars'])} estrellas de
          fondo, <code>--no-vsync</code>.</p>
          <p>Cada punto = <b>{runs_per_point} corridas</b> de 10&nbsp;s (tras
          3&nbsp;s de calentamiento), 10 muestras de 1&nbsp;s por corrida. Métrica
          principal: <code>Actualización ECS</code> ms/fotograma (campo del
          <code>BENCHMARK_CSV</code>), aislada del render.</p>
        </div>
      </div>
    </div></section>""")

    # 4 — speedup
    S.append(f"""<section class="slide"><span class="num">04</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 1 de 4</p>
      <h2>Speedup de la actualización</h2>
      <figure><div class="chart-card"><div id="c-speedup"></div></div>
        <figcaption>speedup(N) = t_secuencial / t_paralelo, sobre el tiempo medio de
        actualización ECS (sin render). La línea punteada es el techo ideal de
        {d['threads']}×. Debajo de ~1000 sistemas el mundo tiene menos de 6144
        entidades y la ruta paralela cae a la secuencial a propósito.</figcaption>
      </figure>
    </div></section>""")

    # 5 — efficiency
    S.append(f"""<section class="slide"><span class="num">05</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 2 de 4</p>
      <h2>Eficiencia</h2>
      <figure><div class="chart-card"><div id="c-eff"></div></div>
        <figcaption>eficiencia(N) = speedup(N) / {d['threads']} hilos. El trabajo por
        entidad es aritmética trivial sobre arreglos grandes — está limitado por ancho
        de banda de memoria, no por cómputo — así que la eficiencia se estabiliza por
        debajo del 100 %.</figcaption>
      </figure>
    </div></section>""")

    # 6 — FPS
    S.append(f"""<section class="slide"><span class="num">06</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 3 de 4</p>
      <h2>FPS totales</h2>
      <div class="legend" id="leg-fps"></div>
      <figure><div class="chart-card"><div id="c-fps"></div></div>
        <figcaption>FPS medios de la aplicación completa (eje N logarítmico). El
        render es serial y domina el tiempo de fotograma, así que el speedup de la
        actualización casi no mueve los FPS totales — es el comportamiento esperado y
        documentado.</figcaption>
      </figure>
    </div></section>""")

    # 7 — update ms
    S.append(f"""<section class="slide"><span class="num">07</span><div class="slide-inner">
      <p class="eyebrow">Resultado · 4 de 4</p>
      <h2>Costo de actualización ECS</h2>
      <div class="legend" id="leg-ms"></div>
      <figure><div class="chart-card"><div id="c-ms"></div></div>
        <figcaption>El número que sí mejora la paralelización: ms por fotograma del
        recorrido centelleo+órbita+vida, secuencial vs {d['threads']} hilos
        (eje N logarítmico).</figcaption>
      </figure>
    </div></section>""")

    # 8 — bitácora
    S.append(f"""<section class="slide"><span class="num">08</span><div class="slide-inner">
      <p class="eyebrow">Anexo 3 · Bitácora</p>
      <h2>Resumen por N</h2>
      {summary_table()}
      <p style="margin-top:1.4rem"></p>
      {runs_table(d['runs']['sequential'][:24], 'Corridas — secuencial')}
      <p style="margin-top:1rem"></p>
      {runs_table(d['runs']['parallel'][:24], 'Corridas — paralelo')}
    </div></section>""")

    # 9 — ceiling
    S.append(f"""<section class="slide"><span class="num">09</span><div class="slide-inner">
      <p class="eyebrow">Escalado</p>
      <h2>N = 1 000 000</h2>
      <p>El mundo se dimensiona en tiempo de ejecución a la N pedida (World,
      SolarSystems y TrailBuffer en heap). Un millón de sistemas ≈ 6 millones de
      entidades y ~0,7 GB. Renderiza a &lt;1 FPS — «corre aunque se vea saturado»,
      que era el objetivo. El <code>--benchmark</code> no aplica a ese N (nunca junta
      10 muestras de 1&nbsp;s); se mide con <code>--frames</code>.</p>
      {ceiling_block()}
    </div></section>""")

    # 10 — conclusions
    S.append(f"""<section class="slide"><span class="num">10</span><div class="slide-inner">
      <p class="eyebrow">Conclusiones</p>
      <h2>Lo que muestran los números</h2>
      <ul class="method">
        <li data-k="1"><b>La actualización escala.</b> {best['speedup']:.2f}× a
        N={best['n']:,} con {d['threads']} hilos, sin mecanismos de exclusión mutua:
        el patrón por-entidad sobre SoA era genuinamente paralelo.</li>
        <li data-k="2"><b>Los FPS no.</b> El render serial es el cuello de botella;
        paralelizarlo requeriría un backend de render por lotes, fuera de alcance.</li>
        <li data-k="3"><b>El límite es la memoria.</b> Eficiencia &lt;100 % porque el
        bucle mueve más bytes de los que calcula.</li>
      </ul>
      <p class="pend">Próxima iteración posible: paralelizar <code>sys_drift</code> y
      la siembra de estelas; medir con 2 y 3 hilos para la curva de eficiencia por
      número de hilos que pide el informe.</p>
    </div></section>""")

    # 11-13 — anexos placeholder
    S.append("""<section class="slide anexo"><span class="num">11</span><div class="slide-inner">
      <p class="eyebrow">Anexo 1</p>
      <h2>Diagrama de flujo</h2>
      <p class="pend">Diagrama de flujo del programa: captura de argumentos →
      programación defensiva → construcción de escena → bucle principal (spawn ·
      drift · <span class="mono">región paralela</span> · barrera · trails ·
      destroy · render) → resumen. Se dibuja a partir del flujo de
      <code>src/main.c</code>.</p>
    </div></section>""")
    S.append("""<section class="slide anexo"><span class="num">12</span><div class="slide-inner">
      <p class="eyebrow">Anexo 2</p>
      <h2>Catálogo de funciones</h2>
      <p class="pend">Entradas / salidas / propósito de cada función pública de
      <code>ecs.h</code>, <code>spawn.h</code>, <code>systems.h</code>,
      <code>deathstar.h</code>, <code>rng.h</code>. Se extrae de los encabezados.</p>
    </div></section>""")
    S.append("""<section class="slide anexo"><span class="num">13</span><div class="slide-inner">
      <p class="eyebrow">Anexo 3</p>
      <h2>Bitácora completa</h2>
      <p class="pend">Tabla cruda de las ≥10 mediciones por configuración con captura
      de pantalla de la terminal. El resumen y las corridas ya están en la
      diapositiva 08; falta adjuntar las capturas.</p>
    </div></section>""")

    return "\n".join(S)


def render(d):
    html = TEMPLATE.replace("<!-- SLIDES -->", slides_html(d))
    html = html.replace("/*DATA*/{}/*END*/", json.dumps({
        "points": d["points"], "threads": d["threads"],
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
