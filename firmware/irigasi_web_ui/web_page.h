#pragma once
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ProTerra · ESP32 Precision Irrigation</title>
<style>
  :root{
    --bg:#090e17;
    --bg-grad:radial-gradient(1100px 420px at 12% -10%, rgba(56,189,248,.10), transparent 60%),
              radial-gradient(900px 400px at 88% -8%, rgba(34,197,94,.09), transparent 55%);
    --surface:#111827;
    --surface-2:#172133;
    --surface-glass:rgba(17,24,39,.78);
    --border:#243247;
    --border-strong:#334663;
    --ink:#f1f5f9;
    --muted:#94a3b8;
    --soil1:#38bdf8;
    --soil2:#f59e0b;
    --flow:#22c55e;
    --danger:#ef4444;
    --danger-ink:#fecaca;
    --ok-ink:#86efac;
    --warn-ink:#fde68a;
    --grid:rgba(148,163,184,.13);
    --radius:14px;
    --radius-sm:9px;
    --shadow:0 8px 28px rgba(0,0,0,.28);
    --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
    --sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Inter,sans-serif;
  }
  [data-theme="light"]{
    --bg:#f1f5f9;
    --bg-grad:radial-gradient(1000px 380px at 10% -10%, rgba(2,132,199,.08), transparent 60%),
              radial-gradient(900px 380px at 90% -10%, rgba(22,163,74,.08), transparent 55%);
    --surface:#ffffff;
    --surface-2:#f8fafc;
    --surface-glass:rgba(255,255,255,.86);
    --border:#dbe3ee;
    --border-strong:#bfd0e3;
    --ink:#0f172a;
    --muted:#475569;
    --soil1:#0284c7;
    --soil2:#d97706;
    --flow:#16a34a;
    --danger:#dc2626;
    --danger-ink:#991b1b;
    --ok-ink:#166534;
    --warn-ink:#92400e;
    --grid:rgba(15,23,42,.08);
    --shadow:0 4px 18px rgba(15,23,42,.06);
  }
  *{box-sizing:border-box}
  html,body{margin:0;padding:0;background:var(--bg);background-image:var(--bg-grad);background-attachment:fixed;
    color:var(--ink);font-family:var(--sans);font-size:14px;line-height:1.45;-webkit-font-smoothing:antialiased}
  .app{max-width:1120px;margin:0 auto;padding:16px;display:flex;flex-direction:column;gap:14px}

  /* Header */
  .topbar{
    position:sticky;top:10px;z-index:20;
    display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px;
    background:var(--surface-glass);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);
    border:1px solid var(--border);border-radius:var(--radius);padding:12px 16px;box-shadow:var(--shadow);
  }
  .brand{display:flex;align-items:center;gap:11px;min-width:0}
  .logo{
    width:36px;height:36px;border-radius:10px;flex:0 0 auto;
    background:linear-gradient(135deg, rgba(34,197,94,.22), rgba(56,189,248,.22));
    border:1px solid var(--border-strong);display:grid;place-items:center;color:var(--flow);
  }
  .brand-txt h1{margin:0;font-size:15.5px;font-weight:700;letter-spacing:-.015em;white-space:nowrap}
  .brand-meta{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-top:2px;font-family:var(--mono);font-size:11px;color:var(--muted)}
  .live-dot{display:inline-flex;align-items:center;gap:5px;font-weight:700;color:var(--flow)}
  .live-dot i{width:7px;height:7px;border-radius:50%;background:var(--flow);display:inline-block;
    box-shadow:0 0 0 3px rgba(34,197,94,.22);animation:pulse 1.8s infinite}
  .live-dot.stale{color:var(--danger)}
  .live-dot.stale i{background:var(--danger);box-shadow:0 0 0 3px rgba(239,68,68,.24);animation:none}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
  @media(prefers-reduced-motion:reduce){.live-dot i{animation:none} *{transition:none!important}}

  .top-actions{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
  .state-pill{
    font-family:var(--mono);font-size:11px;font-weight:700;letter-spacing:.03em;
    padding:6px 11px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border-strong);color:var(--ink)
  }
  .state-pill.ok{border-color:rgba(34,197,94,.5);color:var(--flow)}
  .state-pill.run{border-color:rgba(56,189,248,.6);color:var(--soil1)}
  .state-pill.warn{border-color:rgba(245,158,11,.6);color:var(--soil2)}
  .state-pill.err{background:rgba(239,68,68,.16);border-color:var(--danger);color:var(--danger-ink)}

  /* Buttons */
  .btn{
    display:inline-flex;align-items:center;justify-content:center;gap:6px;
    min-height:36px;padding:7px 13px;border-radius:var(--radius-sm);
    border:1px solid var(--border-strong);background:var(--surface-2);color:var(--ink);
    font-family:var(--sans);font-size:12.5px;font-weight:600;cursor:pointer;
    transition:background .15s,border-color .15s,opacity .15s;
  }
  .btn:hover{border-color:var(--soil1)}
  .btn:focus-visible{outline:2px solid var(--soil1);outline-offset:2px}
  .btn-sm{min-height:30px;padding:4px 10px;font-size:11.5px}
  .btn-icon{width:36px;padding:0}
  .btn-primary{background:var(--flow);border-color:var(--flow);color:#042412}
  .btn-primary:hover{opacity:.92;border-color:var(--flow)}
  .btn-blue{background:var(--soil1);border-color:var(--soil1);color:#062033}
  .btn-blue:hover{opacity:.92;border-color:var(--soil1)}
  .btn-warn{background:var(--soil2);border-color:var(--soil2);color:#261300}
  .btn-danger{background:var(--danger);border-color:var(--danger);color:#fff}
  .btn-danger:hover{opacity:.92;border-color:var(--danger)}
  .btn.active{background:var(--soil1);border-color:var(--soil1);color:#062033}

  /* Fault Banner */
  .fault-banner{
    display:none;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;
    background:rgba(239,68,68,.14);border:1px solid var(--danger);border-radius:var(--radius);padding:12px 16px
  }
  .fault-banner.show{display:flex}

  /* 4-Column KPI Strip */
  .kpi-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}
  @media(max-width:860px){.kpi-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
  @media(max-width:460px){.kpi-grid{grid-template-columns:1fr}}
  .kpi{
    background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);
    padding:13px 15px;box-shadow:var(--shadow);display:flex;flex-direction:column;gap:8px;position:relative;overflow:hidden
  }
  .kpi::before{content:"";position:absolute;top:0;left:0;right:0;height:2.5px;background:var(--accent-bar,var(--border))}
  .kpi-top{display:flex;align-items:center;justify-content:space-between;gap:8px}
  .kpi-title{display:flex;align-items:center;gap:6px;font-size:12px;font-weight:600;color:var(--muted);min-width:0}
  .kpi-title span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .kpi-val-row{display:flex;align-items:baseline;justify-content:space-between;gap:8px}
  .kpi-val{font-family:var(--mono);font-size:26px;font-weight:700;letter-spacing:-.025em;font-variant-numeric:tabular-nums;line-height:1.1}
  .kpi-unit{font-size:12.5px;font-weight:600;color:var(--muted);margin-left:3px}
  .kpi-sub{font-family:var(--mono);font-size:11px;color:var(--muted);display:flex;justify-content:space-between;gap:6px}
  .mini-bar{position:relative;height:6px;border-radius:999px;background:var(--surface-2);overflow:hidden;border:1px solid var(--border)}
  .mini-bar .fill{height:100%;width:0;border-radius:999px;transition:width .25s ease}
  .mini-bar .mark{position:absolute;top:0;bottom:0;width:2px;background:var(--ink);opacity:.85}
  .chip{font-family:var(--mono);font-size:10px;font-weight:700;padding:2px 7px;border-radius:999px;white-space:nowrap}
  .chip-dry{background:rgba(239,68,68,.18);color:var(--danger-ink);border:1px solid rgba(239,68,68,.45)}
  .chip-ok{background:rgba(34,197,94,.16);color:var(--ok-ink);border:1px solid rgba(34,197,94,.4)}
  .chip-off{background:var(--surface-2);color:var(--muted);border:1px solid var(--border)}

  /* Charts Row */
  .charts-grid{display:grid;grid-template-columns:1.35fr 1fr;gap:14px}
  @media(max-width:880px){.charts-grid{grid-template-columns:1fr}}
  .card{
    background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);
    padding:15px 16px;box-shadow:var(--shadow);display:flex;flex-direction:column;gap:12px
  }
  .card-head{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px}
  .card-title{display:flex;align-items:center;gap:8px}
  .card-title h2{margin:0;font-size:14px;font-weight:700;letter-spacing:-.01em}
  .pin-tag{font-family:var(--mono);font-size:10.5px;color:var(--muted);background:var(--surface-2);
    border:1px solid var(--border);padding:2px 7px;border-radius:6px}
  .chart-tools{display:flex;align-items:center;gap:5px;flex-wrap:wrap}
  .legend{display:flex;flex-wrap:wrap;align-items:center;gap:14px;font-size:11.5px;color:var(--muted)}
  .leg-item{display:inline-flex;align-items:center;gap:6px;font-family:var(--mono);font-size:11px}
  .leg-line{width:16px;height:0;border-top:2.5px solid currentColor;display:inline-block}
  .leg-line.dashed{border-top-style:dashed}
  .chart-wrap{position:relative;width:100%;height:245px;border-radius:var(--radius-sm);
    background:var(--surface-2);border:1px solid var(--border);overflow:hidden}
  canvas.chart-cv{width:100%;height:100%;display:block;cursor:crosshair}
  .chart-tip{
    position:absolute;top:8px;right:10px;pointer-events:none;
    font-family:var(--mono);font-size:11px;color:var(--ink);
    background:var(--surface-glass);backdrop-filter:blur(6px);
    border:1px solid var(--border-strong);border-radius:7px;padding:4px 9px;
    opacity:0;transition:opacity .12s
  }
  .chart-tip.show{opacity:1}
  .chart-foot{display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px;font-family:var(--mono);font-size:11px;color:var(--muted)}

  /* Bottom Control Deck */
  .controls-grid{display:grid;grid-template-columns:1.25fr 1fr;gap:14px}
  @media(max-width:860px){.controls-grid{grid-template-columns:1fr}}
  .cfg-table{display:flex;flex-direction:column;gap:9px}
  .cfg-row{
    display:grid;grid-template-columns:auto 1.4fr .8fr .8fr;gap:8px;align-items:end;
    background:var(--surface-2);border:1px solid var(--border);border-radius:var(--radius-sm);padding:10px 11px
  }
  @media(max-width:520px){.cfg-row{grid-template-columns:1fr 1fr}}
  .field{display:flex;flex-direction:column;gap:4px;min-width:0}
  .field label{font-family:var(--mono);font-size:10.5px;color:var(--muted)}
  .field input[type=text],.field input[type=number]{
    width:100%;height:34px;padding:0 9px;border-radius:7px;border:1px solid var(--border-strong);
    background:var(--surface);color:var(--ink);font-family:var(--mono);font-size:12px
  }
  .field input:focus{outline:2px solid var(--soil1);outline-offset:1px;border-color:var(--soil1)}
  .chk{display:inline-flex;align-items:center;gap:7px;font-size:12.5px;cursor:pointer;user-select:none;min-height:32px}
  .chk input{width:16px;height:16px;accent-color:var(--flow);cursor:pointer}
  .row{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
  .slider-box{background:var(--surface-2);border:1px solid var(--border);border-radius:var(--radius-sm);padding:11px 12px;display:flex;flex-direction:column;gap:9px}
  input[type=range]{width:100%;height:6px;accent-color:var(--soil1);cursor:pointer}
  .sys-foot{font-family:var(--mono);font-size:11px;color:var(--muted);background:var(--surface-2);
    border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px 11px;display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px}
  svg.ico{width:16px;height:16px;flex:0 0 auto;stroke:currentColor;fill:none;stroke-width:1.9;stroke-linecap:round;stroke-linejoin:round}
</style>
</head>
<body>
<div class="app">

  <!-- STICKY COMMAND HEADER -->
  <header class="topbar">
    <div class="brand">
      <div class="logo" aria-hidden="true">
        <svg class="ico" style="width:20px;height:20px" viewBox="0 0 24 24"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/></svg>
      </div>
      <div class="brand-txt">
        <h1>ProTerra · Precision Irrigation Telemetry</h1>
        <div class="brand-meta">
          <span class="live-dot" id="live-ind"><i></i><span id="live-txt">LIVE</span></span>
          <span id="net-info">Connecting...</span>
        </div>
      </div>
    </div>
    <div class="top-actions">
      <span class="state-pill" id="state-pill">---</span>
      <button type="button" class="btn btn-primary" onclick="post('/api/action?cmd=start_auto')">Mulai Auto</button>
      <button type="button" class="btn" onclick="post('/api/action?cmd=standby')">Standby</button>
      <button type="button" class="btn btn-danger" onclick="post('/api/action?cmd=stop')">STOP ALL</button>
      <button type="button" class="btn btn-icon" id="theme-btn" aria-label="Toggle light or dark theme" onclick="toggleTheme()">
        <svg class="ico" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M4.93 4.93l1.41 1.41m11.32 11.32 1.41 1.41M2 12h2m16 0h2M4.93 19.07l1.41-1.41m11.32-11.32 1.41-1.41"/></svg>
      </button>
    </div>
  </header>

  <!-- FAULT ALERT BANNER -->
  <div class="fault-banner" id="fault-box" role="alert">
    <div>
      <strong style="color:var(--danger-ink)">ALARM GANGGUAN:</strong>
      <span id="fault-text" style="font-family:var(--mono);margin-left:6px">---</span>
    </div>
    <button type="button" class="btn btn-warn" onclick="post('/api/action?cmd=reset_alarm')">Reset Alarm</button>
  </div>

  <!-- 4-COLUMN LIVE KPI STRIP -->
  <section class="kpi-grid" aria-label="Live sensor metrics">
    <div class="kpi" style="--accent-bar:var(--soil1)">
      <div class="kpi-top">
        <div class="kpi-title">
          <svg class="ico" style="color:var(--soil1)" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/></svg>
          <span id="k-s1-name">Soil 1</span>
        </div>
        <span class="chip chip-off" id="k-s1-chip">--</span>
      </div>
      <div class="kpi-val-row">
        <div class="kpi-val" style="color:var(--soil1)"><span id="k-s1-pct">0.0</span><span class="kpi-unit">%</span></div>
        <span class="pin-tag">D34</span>
      </div>
      <div class="mini-bar"><div class="fill" id="k-s1-bar" style="background:var(--soil1)"></div><div class="mark" id="k-s1-mark"></div></div>
      <div class="kpi-sub"><span id="k-s1-raw">raw ----</span><span id="k-s1-rng">target --</span></div>
    </div>

    <div class="kpi" style="--accent-bar:var(--soil2)">
      <div class="kpi-top">
        <div class="kpi-title">
          <svg class="ico" style="color:var(--soil2)" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/></svg>
          <span id="k-s2-name">Soil 2</span>
        </div>
        <span class="chip chip-off" id="k-s2-chip">--</span>
      </div>
      <div class="kpi-val-row">
        <div class="kpi-val" style="color:var(--soil2)"><span id="k-s2-pct">0.0</span><span class="kpi-unit">%</span></div>
        <span class="pin-tag">D35</span>
      </div>
      <div class="mini-bar"><div class="fill" id="k-s2-bar" style="background:var(--soil2)"></div><div class="mark" id="k-s2-mark"></div></div>
      <div class="kpi-sub"><span id="k-s2-raw">raw ----</span><span id="k-s2-rng">target --</span></div>
    </div>

    <div class="kpi" style="--accent-bar:var(--flow)">
      <div class="kpi-top">
        <div class="kpi-title">
          <svg class="ico" style="color:var(--flow)" viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12c2-3 4-3 6 0s4 3 6 0 4-3 6 0"/><path d="M2 17c2-3 4-3 6 0s4 3 6 0 4-3 6 0"/></svg>
          <span>Water Flow · YF-S201</span>
        </div>
        <span class="pin-tag">D27</span>
      </div>
      <div class="kpi-val-row">
        <div class="kpi-val" style="color:var(--flow)"><span id="k-flow">0.00</span><span class="kpi-unit">L/m</span></div>
      </div>
      <div class="kpi-sub"><span>Vol: <strong id="k-vol">0.000</strong> L</span><span>Pulsa: <strong id="k-pls">0</strong></span></div>
    </div>

    <div class="kpi" style="--accent-bar:var(--border-strong)">
      <div class="kpi-top">
        <div class="kpi-title">
          <svg class="ico" viewBox="0 0 24 24" aria-hidden="true"><path d="M13 2 3 14h9l-1 8 10-12h-9l1-8z"/></svg>
          <span>Pump &amp; Buzzer</span>
        </div>
        <span class="chip chip-off" id="k-act-chip">IDLE</span>
      </div>
      <div class="kpi-val-row">
        <div class="kpi-val"><span id="k-pwm">0</span><span class="kpi-unit">% PWM</span></div>
        <span class="pin-tag">D14/D32</span>
      </div>
      <div class="mini-bar"><div class="fill" id="k-pwm-bar" style="background:var(--soil1)"></div></div>
      <div class="kpi-sub"><span>Pump(D14): <strong id="k-pump">OFF</strong></span><span>Bz(D32): <strong id="k-bz">OFF</strong></span></div>
    </div>
  </section>

  <!-- LIVE STREAMING GRAPHS -->
  <section class="charts-grid" aria-label="Real-time telemetry graphs">
    <!-- SOIL MOISTURE GRAPH -->
    <div class="card">
      <div class="card-head">
        <div class="card-title">
          <h2>Soil Moisture Trend (0–100%)</h2>
          <span class="pin-tag">D34 &amp; D35</span>
        </div>
        <div class="chart-tools">
          <button type="button" class="btn btn-sm active" data-win="60" onclick="setWindow(60,this)">30s</button>
          <button type="button" class="btn btn-sm" data-win="120" onclick="setWindow(120,this)">60s</button>
          <button type="button" class="btn btn-sm" data-win="240" onclick="setWindow(240,this)">2m</button>
          <button type="button" class="btn btn-sm" id="btn-pause" onclick="togglePause()">Pause</button>
          <button type="button" class="btn btn-sm" onclick="clearGraphs()">Clear</button>
        </div>
      </div>
      <div class="legend">
        <span class="leg-item" style="color:var(--soil1)"><i class="leg-line"></i><span id="leg-s1">Soil 1 (D34)</span></span>
        <span class="leg-item" style="color:var(--soil2)"><i class="leg-line dashed"></i><span id="leg-s2">Soil 2 (D35)</span></span>
        <span class="leg-item" style="color:var(--muted)"><i class="leg-line dashed" style="border-top-width:1.5px"></i>Min threshold</span>
      </div>
      <div class="chart-wrap">
        <canvas id="soil-cv" class="chart-cv" aria-label="Real-time dual soil moisture line chart"></canvas>
        <div class="chart-tip" id="soil-tip"></div>
      </div>
      <div class="chart-foot">
        <span id="soil-stats">Buffer: 0 pts · 2 Hz</span>
        <span>Calibrated: S1(2545→1011) · S2(2445→1050)</span>
      </div>
    </div>

    <!-- WATER FLOW GRAPH -->
    <div class="card">
      <div class="card-head">
        <div class="card-title">
          <h2>Water Flow Rate (L/min)</h2>
          <span class="pin-tag">GPIO27 · YF-S201</span>
        </div>
        <div class="chart-tools">
          <button type="button" class="btn btn-sm" onclick="post('/api/action?cmd=reset_vol')">Reset Vol</button>
        </div>
      </div>
      <div class="legend">
        <span class="leg-item" style="color:var(--flow)"><i class="leg-line"></i>Debit (L/menit)</span>
        <span class="leg-item" style="color:var(--muted)" id="flow-peak">Peak: 0.00 L/m</span>
      </div>
      <div class="chart-wrap">
        <canvas id="flow-cv" class="chart-cv" aria-label="Real-time water flow rate area chart"></canvas>
        <div class="chart-tip" id="flow-tip"></div>
      </div>
      <div class="chart-foot">
        <span id="flow-stats">450 pulses/L · 2 ms glitch filter</span>
        <span id="flow-vol-foot">Total: 0.000 L</span>
      </div>
    </div>
  </section>

  <!-- BOTTOM CONTROL DECK -->
  <section class="controls-grid" aria-label="Thresholds and actuator bench controls">
    <!-- CUSTOM INPUT THRESHOLDS (NO HARDCODED PRESET BUTTONS) -->
    <div class="card">
      <div class="card-head">
        <div class="card-title">
          <h2>Threshold &amp; Label Sensor</h2>
          <span class="pin-tag" id="save-msg">NVS Flash</span>
        </div>
        <button type="button" class="btn btn-sm btn-blue" onclick="saveConfig()">Simpan &amp; Terapkan</button>
      </div>
      <div class="cfg-table">
        <div class="cfg-row">
          <label class="chk" title="Aktifkan Soil 1"><input type="checkbox" id="in-s1-en" checked> <span class="pin-tag">D34</span></label>
          <div class="field"><label for="in-s1-name">Label</label><input type="text" id="in-s1-name" maxlength="20" placeholder="Nama tanaman / zona"></div>
          <div class="field"><label for="in-s1-min">Min %</label><input type="number" id="in-s1-min" min="0" max="100" value="40"></div>
          <div class="field"><label for="in-s1-max">Max %</label><input type="number" id="in-s1-max" min="0" max="100" value="60"></div>
        </div>
        <div class="cfg-row">
          <label class="chk" title="Aktifkan Soil 2"><input type="checkbox" id="in-s2-en" checked> <span class="pin-tag">D35</span></label>
          <div class="field"><label for="in-s2-name">Label</label><input type="text" id="in-s2-name" maxlength="20" placeholder="Nama tanaman / zona"></div>
          <div class="field"><label for="in-s2-min">Min %</label><input type="number" id="in-s2-min" min="0" max="100" value="60"></div>
          <div class="field"><label for="in-s2-max">Max %</label><input type="number" id="in-s2-max" min="0" max="100" value="78"></div>
        </div>
      </div>
      <div class="row">
        <label class="chk"><input type="checkbox" id="chk-bz" onchange="saveOptions()"> Buzzer nyala saat siram</label>
        <label class="chk"><input type="checkbox" id="chk-flow" onchange="saveOptions()"> Proteksi No-Flow YF-S201</label>
      </div>
    </div>

    <!-- BENCH ACTUATOR CONTROLS -->
    <div class="card">
      <div class="card-head">
        <div class="card-title">
          <h2>Kontrol Manual Aktuator</h2>
          <span class="pin-tag">D14 &amp; D32</span>
        </div>
      </div>
      <div class="slider-box">
        <div class="kpi-top">
          <span style="font-size:12.5px;font-weight:600">Pompa MOSFET PWM (GPIO14)</span>
          <strong style="font-family:var(--mono)" id="slider-val">0%</strong>
        </div>
        <input type="range" id="pwm-slider" min="0" max="100" step="5" value="0" aria-label="Manual pump PWM duty cycle"
               oninput="document.getElementById('slider-val').textContent=this.value+'%'"
               onchange="setManualPump(this.value)">
        <div class="row">
          <button type="button" class="btn btn-sm" onclick="setManualPump(0)">OFF</button>
          <button type="button" class="btn btn-sm btn-blue" onclick="setManualPump(45)">45%</button>
          <button type="button" class="btn btn-sm btn-blue" onclick="setManualPump(70)">70%</button>
          <button type="button" class="btn btn-sm btn-warn" onclick="setManualPump(100)">100%</button>
        </div>
      </div>
      <div class="row">
        <button type="button" class="btn btn-sm btn-blue" onclick="post('/api/buzzer?mode=beep')">Buzzer Beep 0.5s</button>
        <button type="button" class="btn btn-sm btn-warn" onclick="post('/api/buzzer?mode=on')">Buzzer ON</button>
        <button type="button" class="btn btn-sm" onclick="post('/api/buzzer?mode=off')">Buzzer OFF</button>
      </div>
      <div class="sys-foot">
        <span id="diag-lcd">LCD(0x27): --</span>
        <span id="diag-up">Uptime: 0s</span>
      </div>
    </div>
  </section>
</div>

<script>
const MAX_BUF = 240; // 2 min @ 2 Hz
let winPts = 60;     // 30s default
let paused = false;
let formSynced = false;
let lastOkMs = Date.now();
let peakFlow = 0;

const hist = { t: [], s1: [], s2: [], flow: [] };
let lastState = null;

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function toggleTheme() {
  const cur = document.documentElement.getAttribute('data-theme') || 'dark';
  document.documentElement.setAttribute('data-theme', cur === 'dark' ? 'light' : 'dark');
  drawAllCharts();
}

function setWindow(pts, btn) {
  winPts = pts;
  document.querySelectorAll('[data-win]').forEach(b => b.classList.remove('active'));
  if (btn) btn.classList.add('active');
  drawAllCharts();
}

function togglePause() {
  paused = !paused;
  const b = document.getElementById('btn-pause');
  b.textContent = paused ? 'Resume' : 'Pause';
  b.classList.toggle('active', paused);
}

function clearGraphs() {
  hist.t.length = 0; hist.s1.length = 0; hist.s2.length = 0; hist.flow.length = 0;
  peakFlow = 0;
  drawAllCharts();
}

async function post(url) {
  try {
    const r = await fetch(url, { method: 'POST' });
    const d = await r.json();
    if (d && d.status) applyState(d.status, false);
  } catch (e) {}
}

async function saveConfig() {
  const p = new URLSearchParams({
    s1n: document.getElementById('in-s1-name').value.trim() || 'Soil 1',
    s1min: document.getElementById('in-s1-min').value,
    s1max: document.getElementById('in-s1-max').value,
    s1en: document.getElementById('in-s1-en').checked ? '1' : '0',
    s2n: document.getElementById('in-s2-name').value.trim() || 'Soil 2',
    s2min: document.getElementById('in-s2-min').value,
    s2max: document.getElementById('in-s2-max').value,
    s2en: document.getElementById('in-s2-en').checked ? '1' : '0'
  });
  document.getElementById('save-msg').textContent = 'Saving...';
  await post('/api/config?' + p.toString());
  document.getElementById('save-msg').textContent = 'Saved NVS';
  setTimeout(() => { document.getElementById('save-msg').textContent = 'NVS Flash'; }, 2000);
}

async function saveOptions() {
  const p = new URLSearchParams({
    bzOnPump: document.getElementById('chk-bz').checked ? '1' : '0',
    flowProt: document.getElementById('chk-flow').checked ? '1' : '0'
  });
  await post('/api/options?' + p.toString());
}

function setManualPump(duty) {
  document.getElementById('pwm-slider').value = duty;
  document.getElementById('slider-val').textContent = duty + '%';
  post('/api/pump?duty=' + duty);
}

function updateSoilCard(ch, pct, raw, name, mn, mx, en) {
  document.getElementById(`k-s${ch}-name`).textContent = name;
  document.getElementById(`leg-s${ch}`).textContent = `${name} (D3${ch === 1 ? '4' : '5'})`;
  document.getElementById(`k-s${ch}-pct`).textContent = pct.toFixed(1);
  document.getElementById(`k-s${ch}-raw`).textContent = `raw ${raw}`;
  document.getElementById(`k-s${ch}-rng`).textContent = `${mn}-${mx}%`;
  document.getElementById(`k-s${ch}-bar`).style.width = Math.min(100, Math.max(0, pct)) + '%';
  document.getElementById(`k-s${ch}-mark`).style.left = Math.min(100, Math.max(0, mn)) + '%';
  const c = document.getElementById(`k-s${ch}-chip`);
  if (!en) { c.className = 'chip chip-off'; c.textContent = 'DISABLED'; }
  else if (pct < mn) { c.className = 'chip chip-dry'; c.textContent = '< MIN'; }
  else { c.className = 'chip chip-ok'; c.textContent = 'OK'; }
}

function applyState(s, pushSample = true) {
  lastState = s;
  lastOkMs = Date.now();
  const ind = document.getElementById('live-ind');
  ind.classList.remove('stale');
  document.getElementById('live-txt').textContent = paused ? 'PAUSED' : 'LIVE';
  document.getElementById('net-info').textContent = `IP ${s.ip} · ${s.rssi} dBm`;

  const sp = document.getElementById('state-pill');
  sp.textContent = s.manual ? 'MANUAL' : s.state;
  sp.className = 'state-pill ' + (
    s.state === 'GANGGUAN' ? 'err' :
    s.manual ? 'warn' :
    s.state === 'PENYIRAMAN' ? 'run' :
    s.state === 'PEMANTAUAN' || s.state === 'PERESAPAN' ? 'ok' : ''
  );

  const fb = document.getElementById('fault-box');
  fb.classList.toggle('show', s.state === 'GANGGUAN');
  if (s.state === 'GANGGUAN') document.getElementById('fault-text').textContent = s.fault;

  updateSoilCard(1, s.s1Pct, s.s1Raw, s.s1Name, s.s1Min, s.s1Max, s.s1En);
  updateSoilCard(2, s.s2Pct, s.s2Raw, s.s2Name, s.s2Min, s.s2Max, s.s2En);

  document.getElementById('k-flow').textContent = s.flowLpm.toFixed(2);
  document.getElementById('k-vol').textContent  = s.volL.toFixed(3);
  document.getElementById('k-pls').textContent  = s.pulses;
  document.getElementById('flow-vol-foot').textContent = `Total: ${s.volL.toFixed(3)} L`;

  document.getElementById('k-pwm').textContent = Math.round(s.dutyPct);
  document.getElementById('k-pwm-bar').style.width = Math.round(s.dutyPct) + '%';
  document.getElementById('k-pump').textContent = s.pumpOn ? 'ON' : 'OFF';
  document.getElementById('k-bz').textContent   = s.buzzerOn ? 'ON' : 'OFF';
  const ac = document.getElementById('k-act-chip');
  ac.textContent = s.pumpOn ? 'ACTIVE' : 'IDLE';
  ac.className = 'chip ' + (s.pumpOn ? 'chip-ok' : 'chip-off');

  if (!formSynced) {
    formSynced = true;
    document.getElementById('in-s1-name').value = s.s1Name;
    document.getElementById('in-s1-min').value  = s.s1Min;
    document.getElementById('in-s1-max').value  = s.s1Max;
    document.getElementById('in-s1-en').checked = s.s1En;
    document.getElementById('in-s2-name').value = s.s2Name;
    document.getElementById('in-s2-min').value  = s.s2Min;
    document.getElementById('in-s2-max').value  = s.s2Max;
    document.getElementById('in-s2-en').checked = s.s2En;
  }
  document.getElementById('chk-bz').checked   = s.bzOnPump;
  document.getElementById('chk-flow').checked = s.flowProt;

  if (document.activeElement.id !== 'pwm-slider') {
    document.getElementById('pwm-slider').value = Math.round(s.dutyPct);
    document.getElementById('slider-val').textContent = Math.round(s.dutyPct) + '%';
  }

  document.getElementById('diag-lcd').textContent = s.lcdOk ? `LCD(${s.lcdAddr}): OK` : 'LCD(D21/D22): Offline';
  document.getElementById('diag-up').textContent  = `Uptime: ${s.uptimeS}s`;

  if (pushSample && !paused) {
    const now = new Date();
    const ts = now.toTimeString().slice(0, 8);
    hist.t.push(ts);
    hist.s1.push(s.s1Pct);
    hist.s2.push(s.s2Pct);
    hist.flow.push(s.flowLpm);
    if (s.flowLpm > peakFlow) peakFlow = s.flowLpm;
    if (hist.t.length > MAX_BUF) {
      hist.t.shift(); hist.s1.shift(); hist.s2.shift(); hist.flow.shift();
    }
    document.getElementById('soil-stats').textContent = `Buffer: ${hist.t.length} pts · 2 Hz`;
    document.getElementById('flow-peak').textContent  = `Peak: ${peakFlow.toFixed(2)} L/m`;
    drawAllCharts();
  }
}

// High-DPI Canvas Renderer
function setupCanvas(cv) {
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const w = cv.clientWidth || 500;
  const h = cv.clientHeight || 245;
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  const ctx = cv.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h };
}

let soilHoverIdx = -1;
let flowHoverIdx = -1;

function drawSoilChart() {
  const cv = document.getElementById('soil-cv');
  const { ctx, w, h } = setupCanvas(cv);
  ctx.clearRect(0, 0, w, h);

  const pad = { l: 42, r: 14, t: 14, b: 26 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  const cGrid = cssVar('--grid');
  const cMuted = cssVar('--muted');
  const cS1 = cssVar('--soil1');
  const cS2 = cssVar('--soil2');

  // Grid & Y labels (0..100%)
  ctx.font = '10.5px ' + cssVar('--mono');
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let v = 0; v <= 100; v += 25) {
    const y = pad.t + ph * (1 - v / 100);
    ctx.strokeStyle = cGrid;
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
    ctx.fillStyle = cMuted;
    ctx.fillText(v + '%', pad.l - 6, y);
  }

  // Min threshold lines
  if (lastState) {
    const drawThresh = (val, col, enabled) => {
      if (!enabled) return;
      const y = pad.t + ph * (1 - Math.min(100, Math.max(0, val)) / 100);
      ctx.save();
      ctx.strokeStyle = col;
      ctx.globalAlpha = 0.45;
      ctx.setLineDash([4, 4]);
      ctx.lineWidth = 1.2;
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.restore();
    };
    drawThresh(lastState.s1Min, cS1, lastState.s1En);
    drawThresh(lastState.s2Min, cS2, lastState.s2En);
  }

  const total = hist.t.length;
  const count = Math.min(total, winPts);
  const start = total - count;
  if (count < 2) {
    ctx.fillStyle = cMuted;
    ctx.textAlign = 'center';
    ctx.fillText('Waiting for live samples...', pad.l + pw / 2, pad.t + ph / 2);
    return;
  }

  const xAt = i => pad.l + (i / Math.max(1, winPts - 1)) * pw;
  const yAt = v => pad.t + ph * (1 - Math.min(100, Math.max(0, v)) / 100);
  const xOff = winPts - count;

  function drawSeries(arr, color, dashed) {
    ctx.save();
    // Area fill
    ctx.beginPath();
    for (let i = 0; i < count; i++) {
      const x = xAt(xOff + i), y = yAt(arr[start + i]);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.lineTo(xAt(xOff + count - 1), pad.t + ph);
    ctx.lineTo(xAt(xOff), pad.t + ph);
    ctx.closePath();
    const grad = ctx.createLinearGradient(0, pad.t, 0, pad.t + ph);
    grad.addColorStop(0, color);
    grad.addColorStop(1, 'transparent');
    ctx.globalAlpha = 0.14;
    ctx.fillStyle = grad;
    ctx.fill();

    // Stroke line
    ctx.globalAlpha = 1;
    ctx.strokeStyle = color;
    ctx.lineWidth = 2.2;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    if (dashed) ctx.setLineDash([6, 4]);
    ctx.beginPath();
    for (let i = 0; i < count; i++) {
      const x = xAt(xOff + i), y = yAt(arr[start + i]);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();

    // Latest live dot
    ctx.setLineDash([]);
    const lx = xAt(xOff + count - 1), ly = yAt(arr[start + count - 1]);
    ctx.fillStyle = color;
    ctx.beginPath(); ctx.arc(lx, ly, 4, 0, Math.PI * 2); ctx.fill();
    ctx.restore();
  }

  drawSeries(hist.s2, cS2, true);
  drawSeries(hist.s1, cS1, false);

  // X time labels
  ctx.fillStyle = cMuted;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  ctx.fillText(hist.t[start], xAt(xOff), h - 18);
  ctx.fillText(hist.t[total - 1], xAt(winPts - 1) - 18, h - 18);

  // Hover crosshair
  if (soilHoverIdx >= 0 && soilHoverIdx < count) {
    const idx = start + soilHoverIdx;
    const hx = xAt(xOff + soilHoverIdx);
    ctx.strokeStyle = cMuted;
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(hx, pad.t); ctx.lineTo(hx, pad.t + ph); ctx.stroke();
    ctx.setLineDash([]);
    const tip = document.getElementById('soil-tip');
    tip.textContent = `${hist.t[idx]} · S1:${hist.s1[idx].toFixed(1)}% · S2:${hist.s2[idx].toFixed(1)}%`;
    tip.classList.add('show');
  }
}

function drawFlowChart() {
  const cv = document.getElementById('flow-cv');
  const { ctx, w, h } = setupCanvas(cv);
  ctx.clearRect(0, 0, w, h);

  const pad = { l: 46, r: 14, t: 14, b: 26 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  const cGrid = cssVar('--grid');
  const cMuted = cssVar('--muted');
  const cFlow = cssVar('--flow');

  const total = hist.t.length;
  const count = Math.min(total, winPts);
  const start = total - count;

  let yMax = 2.0;
  for (let i = start; i < total; i++) if (hist.flow[i] > yMax) yMax = hist.flow[i];
  yMax = Math.ceil(yMax * 1.2 * 2) / 2;

  ctx.font = '10.5px ' + cssVar('--mono');
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let k = 0; k <= 4; k++) {
    const v = (yMax * k) / 4;
    const y = pad.t + ph * (1 - k / 4);
    ctx.strokeStyle = cGrid;
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
    ctx.fillStyle = cMuted;
    ctx.fillText(v.toFixed(1), pad.l - 6, y);
  }

  if (count < 2) {
    ctx.fillStyle = cMuted;
    ctx.textAlign = 'center';
    ctx.fillText('Waiting for flow telemetry...', pad.l + pw / 2, pad.t + ph / 2);
    return;
  }

  const xAt = i => pad.l + (i / Math.max(1, winPts - 1)) * pw;
  const yAt = v => pad.t + ph * (1 - Math.min(yMax, Math.max(0, v)) / yMax);
  const xOff = winPts - count;

  ctx.save();
  ctx.beginPath();
  for (let i = 0; i < count; i++) {
    const x = xAt(xOff + i), y = yAt(hist.flow[start + i]);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.lineTo(xAt(xOff + count - 1), pad.t + ph);
  ctx.lineTo(xAt(xOff), pad.t + ph);
  ctx.closePath();
  const grad = ctx.createLinearGradient(0, pad.t, 0, pad.t + ph);
  grad.addColorStop(0, cFlow);
  grad.addColorStop(1, 'transparent');
  ctx.globalAlpha = 0.22;
  ctx.fillStyle = grad;
  ctx.fill();

  ctx.globalAlpha = 1;
  ctx.strokeStyle = cFlow;
  ctx.lineWidth = 2.2;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  for (let i = 0; i < count; i++) {
    const x = xAt(xOff + i), y = yAt(hist.flow[start + i]);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();

  const lx = xAt(xOff + count - 1), ly = yAt(hist.flow[start + count - 1]);
  ctx.fillStyle = cFlow;
  ctx.beginPath(); ctx.arc(lx, ly, 4, 0, Math.PI * 2); ctx.fill();
  ctx.restore();

  ctx.fillStyle = cMuted;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  ctx.fillText(hist.t[start], xAt(xOff), h - 18);
  ctx.fillText(hist.t[total - 1], xAt(winPts - 1) - 18, h - 18);

  if (flowHoverIdx >= 0 && flowHoverIdx < count) {
    const idx = start + flowHoverIdx;
    const hx = xAt(xOff + flowHoverIdx);
    ctx.strokeStyle = cMuted;
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(hx, pad.t); ctx.lineTo(hx, pad.t + ph); ctx.stroke();
    ctx.setLineDash([]);
    const tip = document.getElementById('flow-tip');
    tip.textContent = `${hist.t[idx]} · ${hist.flow[idx].toFixed(2)} L/min`;
    tip.classList.add('show');
  }
}

function drawAllCharts() {
  drawSoilChart();
  drawFlowChart();
}

function bindHover(canvasId, tipId, padL, padR, setter) {
  const cv = document.getElementById(canvasId);
  cv.addEventListener('pointermove', e => {
    const rect = cv.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const pw = rect.width - padL - padR;
    const count = Math.min(hist.t.length, winPts);
    if (count < 2) return;
    const xOff = winPts - count;
    const slot = Math.round(((x - padL) / pw) * (winPts - 1)) - xOff;
    setter(Math.max(0, Math.min(count - 1, slot)));
    drawAllCharts();
  });
  cv.addEventListener('pointerleave', () => {
    setter(-1);
    document.getElementById(tipId).classList.remove('show');
    drawAllCharts();
  });
}
bindHover('soil-cv', 'soil-tip', 42, 14, v => { soilHoverIdx = v; });
bindHover('flow-cv', 'flow-tip', 46, 14, v => { flowHoverIdx = v; });

window.addEventListener('resize', drawAllCharts);

async function poll() {
  if (document.hidden) return; // Save CPU when tab is hidden
  try {
    const r = await fetch('/api/status', { cache: 'no-store' });
    const s = await r.json();
    applyState(s, true);
  } catch (e) {
    if (Date.now() - lastOkMs > 2500) {
      document.getElementById('live-ind').classList.add('stale');
      document.getElementById('live-txt').textContent = 'OFFLINE';
    }
  }
}

poll();
setInterval(poll, 500);
</script>
</body>
</html>
)rawliteral";
