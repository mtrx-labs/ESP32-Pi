// Dashboard page for ESP32-Pi. Kept in a header so the Arduino build
// does not scan the JavaScript for C++ function prototypes.
#pragma once
#include <Arduino.h>

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<title>ESP32-Pi</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iNDEiIGhlaWdodD0iNDIiIHZpZXdCb3g9IjAgMCA0MSA0MiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPGcgZmlsdGVyPSJ1cmwoI2ZpbHRlcjBfZF8zMV8xNCkiPgo8bGluZSB4MT0iNy43Nzc3OCIgeTE9IjE2IiB4Mj0iMjYuNjY2NyIgeTI9IjE2IiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZT0iIzBFOUUxOCIvPgo8bGluZSB4MT0iOC4zMzI0OCIgeTE9IjE2LjE2OCIgeDI9IjMwLjk5OTEiIHkyPSIzMS4yNzkxIiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZT0iIzBFOUUxOCIvPgo8bGluZSB4MT0iMzMuMzMxMSIgeTE9IjMuODQ0MzEiIHgyPSIzMS40NDIyIiB5Mj0iMzIuMTc3NiIgc3Ryb2tlLXdpZHRoPSIyIiBzdHJva2U9IiMwRTlFMTgiLz4KPGxpbmUgeDE9IjcuMzAzNjgiIHkxPSIxNi4xMTk1IiB4Mj0iMzEuODU5MiIgeTI9IjIuODk3MzEiIHN0cm9rZS13aWR0aD0iMiIgc3Ryb2tlPSIjMEU5RTE4Ii8+CjxsaW5lIHgxPSIyNy42MzY4IiB5MT0iMTYuNzU3NSIgeDI9IjMxLjQxNDYiIHkyPSIzMS44Njg2IiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZT0iIzBFOUUxOCIvPgo8bGluZSB4MT0iMjcuNjE5MiIgeTE9IjE2LjY0ODkiIHgyPSIzMy4yODU5IiB5Mj0iMS41Mzc3NyIgc3Ryb2tlLXdpZHRoPSIyIiBzdHJva2U9IiMwRTlFMTgiLz4KPGNpcmNsZSBjeD0iMzMuMjc3OCIgY3k9IjIuODMzMzMiIHI9IjIuODMzMzMiIGZpbGw9IiMwNTQzMDkiLz4KPGNpcmNsZSBjeD0iMzEuMzg4OSIgY3k9IjMxLjE2NjciIHI9IjIuODMzMzMiIGZpbGw9IiMwNTQzMDkiLz4KPGNpcmNsZSBjeD0iNi44MzMzMyIgY3k9IjE2LjA1NTYiIHI9IjIuODMzMzMiIGZpbGw9IiMwNTQzMDkiLz4KPGNpcmNsZSBjeD0iMjcuNjExMSIgY3k9IjE2LjA1NTYiIHI9IjIuODMzMzMiIGZpbGw9IiMwNTQzMDkiLz4KPC9nPgo8ZGVmcz4KPGZpbHRlciBpZD0iZmlsdGVyMF9kXzMxXzE0IiB4PSIwIiB5PSIwIiB3aWR0aD0iNDAuMTExMSIgaGVpZ2h0PSI0MiIgZmlsdGVyVW5pdHM9InVzZXJTcGFjZU9uVXNlIiBjb2xvci1pbnRlcnBvbGF0aW9uLWZpbHRlcnM9InNSR0IiPgo8ZmVGbG9vZCBmbG9vZC1vcGFjaXR5PSIwIiByZXN1bHQ9IkJhY2tncm91bmRJbWFnZUZpeCIvPgo8ZmVDb2xvck1hdHJpeCBpbj0iU291cmNlQWxwaGEiIHR5cGU9Im1hdHJpeCIgdmFsdWVzPSIwIDAgMCAwIDAgMCAwIDAgMCAwIDAgMCAwIDAgMCAwIDAgMCAxMjcgMCIgcmVzdWx0PSJoYXJkQWxwaGEiLz4KPGZlT2Zmc2V0IGR5PSI0Ii8+CjxmZUdhdXNzaWFuQmx1ciBzdGREZXZpYXRpb249IjIiLz4KPGZlQ29tcG9zaXRlIGluMj0iaGFyZEFscGhhIiBvcGVyYXRvcj0ib3V0Ii8+CjxmZUNvbG9yTWF0cml4IHR5cGU9Im1hdHJpeCIgdmFsdWVzPSIwIDAgMCAwIDAgMCAwIDAgMCAwIDAgMCAwIDAgMCAwIDAgMCAwIDAuMjUgMCIvPgo8ZmVCbGVuZCBtb2RlPSJub3JtYWwiIGluMj0iQmFja2dyb3VuZEltYWdlRml4IiByZXN1bHQ9ImVmZmVjdDFfZHJvcFNoYWRvd18zMV8xNCIvPgo8ZmVCbGVuZCBtb2RlPSJub3JtYWwiIGluPSJTb3VyY2VHcmFwaGljIiBpbjI9ImVmZmVjdDFfZHJvcFNoYWRvd18zMV8xNCIgcmVzdWx0PSJzaGFwZSIvPgo8L2ZpbHRlcj4KPC9kZWZzPgo8L3N2Zz4=">
<style>
:root{color-scheme:light;--bg:#eaf0f4;--pn:#fff;--p2:#f1f5f8;--ln:#d3dde5;--tx:#10202c;--mu:#5d7080;--ok:#0b8f86;--on:#fff;--bk:#d9442b;--bl:#3b7dd8;--wa:#c58a12}
@media(prefers-color-scheme:dark){:root{color-scheme:dark;--bg:#0c1822;--pn:#132431;--p2:#193040;--ln:#22394a;--tx:#e6eef4;--mu:#8aa0b0;--ok:#34c2b5;--on:#06201d;--bk:#ff7a5f;--bl:#6aa5f0;--wa:#e3b34f}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",sans-serif;font-variant-numeric:tabular-nums}
.wrap{max-width:1020px;margin:0 auto;padding:20px 16px 72px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
.brand{display:flex;align-items:center;gap:12px}
.brand svg{width:41px;height:42px;flex:none}
h1{margin:0;font-size:21px;font-weight:750;letter-spacing:-.02em}
h2{margin:0 0 12px;font-size:16px;font-weight:700}
h3{margin:18px 0 6px;font-size:14px}
p{margin:6px 0}
.mu{color:var(--mu);font-size:13px}
.pill{display:flex;align-items:center;gap:8px;padding:6px 12px;border:1px solid var(--ln);border-radius:99px;background:var(--pn);font-size:13px;font-weight:600;white-space:nowrap}
.pill i{width:9px;height:9px;border-radius:50%;background:var(--mu)}
.pill.on i{background:var(--ok)}.pill.ap i{background:var(--bl)}
nav{display:flex;gap:2px;border-bottom:1px solid var(--ln);margin-bottom:16px;overflow-x:auto}
nav button{margin:0 0 -1px;padding:11px 16px;background:none;color:var(--mu);border:0;border-bottom:3px solid transparent;border-radius:0;font-weight:650}
nav button.on{color:var(--tx);border-bottom-color:var(--ok)}
.tab{display:none}.tab.on{display:block}
.cols{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(0,1fr);gap:14px;align-items:start}
.cols.eq{grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr))}
@media(max-width:780px){.cols{grid-template-columns:1fr}}
.col>.pn+.pn{margin-top:14px}
.pn{background:var(--pn);border:1px solid var(--ln);border-radius:14px;padding:18px}
.big{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}
.big b{font-size:44px;line-height:1.05;font-weight:780;letter-spacing:-.03em}
.seg{display:flex;height:16px;border-radius:8px;overflow:hidden;background:var(--p2);margin:16px 0 10px}
.seg i{height:100%;width:0;transition:width .6s}
.c0{background:var(--bk)}.c1{background:var(--ok)}.c2{background:var(--bl)}.c3{background:var(--wa)}.c4{background:var(--mu);opacity:.55}
.lg{display:grid;grid-template-columns:14px 1fr auto 58px;gap:10px;align-items:center;padding:8px 0;border-top:1px solid var(--ln)}
.lg i{width:12px;height:12px;border-radius:3px}
.lg b{font-weight:650}
.lg em{font-style:normal;color:var(--mu);text-align:right}
.spark{display:block;width:100%;height:56px;margin-top:8px}
.spark polyline{fill:none;stroke:var(--ok);stroke-width:2;vector-effect:non-scaling-stroke}
.spark polygon{fill:var(--ok);opacity:.14}
.kv>div{display:flex;justify-content:space-between;gap:14px;padding:9px 0;border-top:1px solid var(--ln)}
.kv>div:first-child{border-top:0;padding-top:0}
.kv span{color:var(--mu)}
.kv b{font-weight:600;text-align:right;overflow-wrap:anywhere}
.m{margin:14px 0 0}
.m .t{display:flex;justify-content:space-between;font-size:13px;margin-bottom:6px}
.m .t span{color:var(--mu)}
.bar{height:8px;border-radius:8px;background:var(--p2);overflow:hidden}
.bar i{display:block;height:100%;width:0;border-radius:8px;background:var(--ok);transition:width .6s}
.bar.ind i{width:35%;animation:sl 1.1s infinite ease-in-out}
@keyframes sl{0%{margin-left:-35%}100%{margin-left:100%}}
input,select{width:100%;padding:11px 13px;margin:6px 0;border-radius:10px;border:1px solid var(--ln);background:var(--p2);color:var(--tx);font:inherit}
input:focus,select:focus{outline:2px solid var(--ok);outline-offset:0}
button{font:inherit;font-weight:650;cursor:pointer;border:1px solid transparent;border-radius:10px;padding:10px 16px;margin:4px 6px 4px 0;background:var(--ok);color:var(--on)}
button.s{background:transparent;color:var(--tx);border-color:var(--ln)}
button.d{background:transparent;color:var(--bk);border-color:var(--bk)}
button:disabled{opacity:.5;cursor:wait}
:focus-visible{outline:2px solid var(--ok);outline-offset:2px}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-top:1px solid var(--ln)}
.row:first-child{border-top:0}
.row b{font-weight:600;overflow-wrap:anywhere}
.l{display:flex;align-items:center;min-width:0}
.list{max-height:320px;overflow-y:auto}
.sig{display:inline-flex;gap:2px;align-items:flex-end;height:16px;margin-right:12px;flex:none}
.sig i{width:4px;background:var(--ln);border-radius:1px}
.sig i:nth-child(1){height:5px}.sig i:nth-child(2){height:9px}.sig i:nth-child(3){height:13px}.sig i:nth-child(4){height:16px}
.s1 i:nth-child(-n+1),.s2 i:nth-child(-n+2),.s3 i:nth-child(-n+3),.s4 i:nth-child(-n+4){background:var(--ok)}
.chip{display:inline-flex;align-items:center;gap:4px;padding:3px 4px 3px 12px;border:1px solid var(--ln);border-radius:99px;background:var(--p2);margin:4px 6px 4px 0;font-size:13px}
.chip button{margin:0;padding:0 9px;background:none;color:var(--mu);font-size:18px;line-height:24px;border:0}
.opt{display:flex;gap:12px;align-items:flex-start;padding:12px;border:1px solid var(--ln);border-radius:10px;margin:8px 0;cursor:pointer}
.opt.sel{border-color:var(--ok);background:var(--p2)}
.opt input{width:auto;margin:4px 0 0}
.opt b{display:block}
.opt span{color:var(--mu);font-size:13px}
.swr{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px 0;border-top:1px solid var(--ln)}
.sw{position:relative;width:46px;height:26px;flex:none}
.sw input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer;z-index:2}
.sw u{position:absolute;inset:0;border-radius:26px;background:var(--ln);transition:background .2s}
.sw u:after{content:"";position:absolute;left:3px;top:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:transform .2s}
.sw input:checked+u{background:var(--ok)}
.sw input:checked+u:after{transform:translateX(20px)}
.sw input:focus-visible+u{outline:2px solid var(--ok);outline-offset:2px}
#toast{position:fixed;left:50%;bottom:22px;transform:translate(-50%,30px);opacity:0;pointer-events:none;max-width:90vw;padding:12px 18px;border-radius:10px;background:var(--tx);color:var(--bg);font-weight:600;transition:.25s;z-index:20}
#toast.show{opacity:1;transform:translate(-50%,0)}
#toast.bad{background:var(--bk);color:#fff}
footer{margin-top:28px;text-align:center}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
</head>
<body>
<div class="wrap">

<header>
  <div class="brand">
    <svg width="41" height="42" role="img" aria-label="ESP32-Pi logo" viewBox="0 0 41 42" fill="none" xmlns="http://www.w3.org/2000/svg">
<g filter="url(#filter0_d_31_14)">
<line x1="7.77778" y1="16" x2="26.6667" y2="16" stroke-width="2" stroke="#0E9E18"/>
<line x1="8.33248" y1="16.168" x2="30.9991" y2="31.2791" stroke-width="2" stroke="#0E9E18"/>
<line x1="33.3311" y1="3.84431" x2="31.4422" y2="32.1776" stroke-width="2" stroke="#0E9E18"/>
<line x1="7.30368" y1="16.1195" x2="31.8592" y2="2.89731" stroke-width="2" stroke="#0E9E18"/>
<line x1="27.6368" y1="16.7575" x2="31.4146" y2="31.8686" stroke-width="2" stroke="#0E9E18"/>
<line x1="27.6192" y1="16.6489" x2="33.2859" y2="1.53777" stroke-width="2" stroke="#0E9E18"/>
<circle cx="33.2778" cy="2.83333" r="2.83333" fill="#054309"/>
<circle cx="31.3889" cy="31.1667" r="2.83333" fill="#054309"/>
<circle cx="6.83333" cy="16.0556" r="2.83333" fill="#054309"/>
<circle cx="27.6111" cy="16.0556" r="2.83333" fill="#054309"/>
</g>
<defs>
<filter id="filter0_d_31_14" x="0" y="0" width="40.1111" height="42" filterUnits="userSpaceOnUse" color-interpolation-filters="sRGB">
<feFlood flood-opacity="0" result="BackgroundImageFix"/>
<feColorMatrix in="SourceAlpha" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0" result="hardAlpha"/>
<feOffset dy="4"/>
<feGaussianBlur stdDeviation="2"/>
<feComposite in2="hardAlpha" operator="out"/>
<feColorMatrix type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0.25 0"/>
<feBlend mode="normal" in2="BackgroundImageFix" result="effect1_dropShadow_31_14"/>
<feBlend mode="normal" in="SourceGraphic" in2="effect1_dropShadow_31_14" result="shape"/>
</filter>
</defs>
</svg>
    <div><h1>ESP32-Pi</h1><div class="mu">DNS filter for <span id="apName">your hotspot</span></div></div>
  </div>
  <div class="pill" id="pill"><i></i><span id="pillT">Connecting</span></div>
</header>

<nav id="nav" role="tablist">
  <button data-t="over" class="on" role="tab">Overview</button>
  <button data-t="wifi" role="tab">Wi-Fi</button>
  <button data-t="filters" role="tab">Filters</button>
  <button data-t="system" role="tab">Settings</button>
</nav>

<!-- ============ OVERVIEW ============ -->
<section class="tab on" id="t-over">
 <div class="cols">
  <div class="col">
   <div class="pn">
     <h2>DNS traffic</h2>
     <div class="big"><b id="total">0</b><span class="mu">queries since startup</span></div>
     <div class="mu" id="rate">Waiting for traffic.</div>
     <div class="seg" id="seg"></div>
     <div id="leg"></div>
     <svg class="spark" viewBox="0 0 240 40" preserveAspectRatio="none" aria-label="Queries over the last two minutes"><polygon id="sparkA" points=""/><polyline id="spark" points=""/></svg>
     <div class="mu">Queries over the last two minutes</div>
   </div>
  </div>
  <div class="col">
   <div class="pn">
     <h2>Device</h2>
     <div class="kv">
       <div><span>Wi-Fi network</span><b id="wifiName">Not connected</b></div>
       <div><span>Address on that network</span><b id="stationIP">none</b></div>
       <div><span>Signal</span><b id="rssi">none</b></div>
       <div><span>Hotspot</span><b id="apIP">192.168.4.1</b></div>
       <div><span>Devices on hotspot</span><b id="clients">0</b></div>
       <div><span>Internet sharing</span><b id="napt">Waiting</b></div>
       <div><span>Upstream DNS</span><b id="upstream">Cloudflare</b></div>
     </div>
   </div>
   <div class="pn">
     <h2>Memory and storage</h2>
     <div class="m"><div class="t"><span>Storage</span><b id="mfs">none</b></div><div class="bar"><i id="bfs"></i></div></div>
     <div class="m"><div class="t"><span>PSRAM</span><b id="mps">none</b></div><div class="bar"><i id="bps"></i></div></div>
     <div class="m"><div class="t"><span>DNS cache</span><b id="mca">none</b></div><div class="bar"><i id="bca"></i></div></div>
     <div class="m"><div class="t"><span>Free heap</span><b id="mhe">none</b></div></div>
   </div>
  </div>
 </div>
</section>

<!-- ============ WI-FI ============ -->
<section class="tab" id="t-wifi">
 <div class="cols eq">
  <div class="pn">
    <h2>Available networks</h2>
    <p class="mu">Scanning pauses traffic on the hotspot for a second or two.</p>
    <button id="scanBtn" onclick="scan()">Scan for networks</button>
    <div id="nets" class="list"></div>
  </div>
  <div class="col">
    <div class="pn">
      <h2>Connect to a network</h2>
      <input id="ssid" placeholder="Network name" maxlength="32" autocomplete="off" autocapitalize="off">
      <input id="pw" type="password" placeholder="Password (leave empty for open networks)" autocomplete="off">
      <button id="connBtn" onclick="connect()">Connect and save</button>
      <p id="wmsg" class="mu" aria-live="polite"></p>
    </div>
    <div class="pn">
      <h2>Saved networks</h2>
      <div id="saved" class="list"></div>
    </div>
  </div>
 </div>
</section>

<!-- ============ FILTERS ============ -->
<section class="tab" id="t-filters">
 <div class="cols eq">
  <div class="pn">
    <h2>HaGeZi Pro blocklist</h2>
    <p class="mu">Downloaded to flash and indexed in PSRAM. New lists are active as soon as indexing finishes; no restart needed.</p>
    <div class="kv" style="margin-top:12px">
      <div><span>Blocked domains</span><b id="hgCount">0</b></div>
      <div><span>Exceptions</span><b id="hgExc">0</b></div>
    </div>
    <div class="bar" id="hgBar" style="margin:14px 0 8px"><i></i></div>
    <p class="mu" id="hgMsg" aria-live="polite">Checking blocklist.</p>
    <button id="hgBtn" onclick="updateHG()">Download or update list</button>
  </div>
  <div class="pn">
    <h2>Your own rules</h2>
    <p class="mu">A rule also covers every subdomain. Your rules win over the blocklist.</p>
    <input id="dom" placeholder="example.com" maxlength="253" autocomplete="off" autocapitalize="off">
    <button onclick="domain('add','block',$('#dom').value.trim())">Block domain</button>
    <button class="s" onclick="domain('add','allow',$('#dom').value.trim())">Allow domain</button>
    <h3>Blocked</h3><div id="bl"></div>
    <h3>Allowed</h3><div id="al"></div>
  </div>
 </div>
</section>

<!-- ============ SETTINGS ============ -->
<section class="tab" id="t-system">
 <div class="cols eq">
  <div class="pn">
    <h2>Upstream DNS</h2>
    <p class="mu">Where allowed lookups are sent. The router option is often the fastest because it sits one hop away.</p>
    <div id="ups">
      <label class="opt"><input type="radio" name="up" value="router"><div><b>Router or ISP</b><span>Uses the DNS server your router hands out</span></div></label>
      <label class="opt"><input type="radio" name="up" value="cloudflare"><div><b>Cloudflare</b><span>1.1.1.1 and 1.0.0.1</span></div></label>
      <label class="opt"><input type="radio" name="up" value="google"><div><b>Google</b><span>8.8.8.8 and 8.8.4.4</span></div></label>
      <label class="opt"><input type="radio" name="up" value="quad9"><div><b>Quad9</b><span>9.9.9.9 and 149.112.112.112</span></div></label>
    </div>
    <p id="upmsg" class="mu" aria-live="polite"></p>
  </div>
  <div class="col">
    <div class="pn">
      <h2>Speed options</h2>
      <div class="swr">
        <div><b>Skip IPv6 lookups</b><div class="mu">Hotspot devices have no IPv6 route, so these lookups only add delay. Turn off if you use IPv6-only services.</div></div>
        <label class="sw"><input type="checkbox" id="aaaa" aria-label="Skip IPv6 lookups"><u></u></label>
      </div>
      <div class="swr">
        <div><b>DNS cache</b><div class="mu">Repeat lookups are answered instantly from memory.</div></div>
        <button class="s" onclick="flush()">Clear cache</button>
      </div>
    </div>
    <div class="pn">
      <h2>About</h2>
      <div class="kv">
        <div><span>Firmware</span><b id="ver">0.4</b></div>
        <div><span>Uptime</span><b id="up">none</b></div>
        <div><span>Hotspot name</span><b id="aboutSsid">none</b></div>
        <div><span>Dashboard</span><b id="aboutIp">192.168.4.1</b></div>
      </div>
      <p class="mu" style="margin-top:12px">To filter your whole home, set your router's DHCP DNS server to this device's address on your Wi-Fi network (shown on the Overview tab).</p>
      <button class="d" onclick="reboot()">Restart device</button>
    </div>
  </div>
 </div>
</section>

<footer class="mu">ESP32-Pi <span id="foot"></span></footer>
</div>
<div id="toast" role="status" aria-live="polite"></div>

<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)],N=n=>Number(n).toLocaleString();
const set=(i,v)=>{const e=document.getElementById(i);if(e)e.textContent=v};
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e}
let tt;function toast(m,bad){const t=$('#toast');t.textContent=m;t.className='show'+(bad?' bad':'');clearTimeout(tt);tt=setTimeout(()=>t.className='',3500)}

async function api(p,d){
  const o={};
  if(d){o.method='POST';o.headers={'Content-Type':'application/x-www-form-urlencoded','X-ESP32Pi':'1'};o.body=new URLSearchParams(d)}
  const r=await fetch(p,o);let j={};
  try{j=await r.json()}catch(e){throw Error(r.ok?'Unexpected reply from device':'HTTP '+r.status)}
  if(!r.ok||j.ok===false)throw Error(j.message||'HTTP '+r.status);
  return j;
}

function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+(d||h?h+'h ':'')+m+'m'}
const mb=b=>(b/1048576).toFixed(1)+' MB';
const bars=r=>r>-55?4:r>-65?3:r>-75?2:1;
const quality=r=>r>-55?'Excellent':r>-65?'Good':r>-75?'Fair':'Weak';

/* ---------- tabs ---------- */
function go(n){
  if(!$('#t-'+n))n='over';
  $$('.tab').forEach(e=>e.classList.toggle('on',e.id==='t-'+n));
  $$('nav button').forEach(b=>b.classList.toggle('on',b.dataset.t===n));
  history.replaceState(null,'','#'+n);
  if(n==='wifi')loadSaved();
  if(n==='filters')loadDomains();
}
$('#nav').addEventListener('click',e=>{const b=e.target.closest('button');if(b)go(b.dataset.t)});

/* ---------- traffic legend ---------- */
const SEG=[['Blocked','blocked'],['Answered from cache','cached'],['Answered by upstream','forwarded'],['Failed','failed'],['IPv6 lookups skipped','aaaa']];
SEG.forEach((x,i)=>{
  $('#seg').append(Object.assign(el('i','c'+i),{id:'sg'+i}));
  const r=el('div','lg');r.append(el('i','c'+i),el('span',null,x[0]),Object.assign(el('b',null,'0'),{id:'n'+i}),Object.assign(el('em',null,'0%'),{id:'p'+i}));
  $('#leg').append(r);
});

/* ---------- status polling ---------- */
let hist=[],prev=null;
function spark(){
  const m=Math.max(1,...hist),n=hist.length;if(n<2)return;
  const pts=hist.map((v,i)=>(i*240/39).toFixed(1)+','+(37-v/m*34).toFixed(1));
  $('#spark').setAttribute('points',pts.join(' '));
  $('#sparkA').setAttribute('points','0,40 '+pts.join(' ')+' '+((n-1)*240/39).toFixed(1)+',40');
}
function meter(id,used,total,txt){
  set('m'+id,txt);const p=total>0?Math.min(100,used*100/total):0,b=$('#b'+id);
  if(b){b.style.width=p+'%';b.style.background=p>90?'var(--bk)':'var(--ok)'}
}
function render(s){
  const on=s.connected;
  $('#pill').className='pill '+(on?'on':'ap');set('pillT',on?'Online':'Hotspot only');
  set('apName',s.ap_ssid);set('aboutSsid',s.ap_ssid);set('aboutIp',s.ap_ip);set('apIP',s.ap_ip);
  set('total',N(s.total));
  const v=SEG.map(x=>s[x[1]]||0),sum=v.reduce((a,b)=>a+b,0)||1;
  v.forEach((n,i)=>{const p=n*100/sum;$('#sg'+i).style.width=p+'%';set('n'+i,N(n));set('p'+i,p.toFixed(1)+'%')});
  if(prev!==null)hist.push(Math.max(0,s.total-prev));if(hist.length>40)hist.shift();prev=s.total;spark();
  const k=Math.min(hist.length,20),qpm=k?Math.round(hist.slice(-k).reduce((a,b)=>a+b,0)*20/k):0;
  set('rate',(qpm?'About '+N(qpm)+' queries a minute. ':'Quiet right now. ')+(s.latency>0?'Upstream answers take '+s.latency+' ms on average.':'No upstream timing yet.'));
  set('wifiName',on?s.ssid:'Not connected');set('stationIP',on?s.ip:'none');
  set('rssi',on?s.rssi+' dBm ('+quality(s.rssi)+')':'none');
  set('clients',s.clients);set('napt',s.napt?'Active':(on?'Starting':'Waiting for Wi-Fi'));
  set('upstream',s.upstream);
  meter('fs',s.fs_used,s.fs_total,s.fs_total>0?mb(s.fs_used)+' of '+mb(s.fs_total):'unavailable');
  meter('ps',s.psram_total-s.psram_free,s.psram_total,s.psram_total>0?mb(s.psram_total-s.psram_free)+' of '+mb(s.psram_total):'unavailable');
  meter('ca',s.cache_used,s.cache_cap,N(s.cache_used)+' of '+N(s.cache_cap)+' entries');
  set('mhe',mb(s.heap_free));
  set('hgCount',N(s.hg_loaded));set('hgExc',N(s.hg_exc));set('hgMsg',s.hg_msg);
  const busy=s.hg_state===1||s.hg_state===4;
  $('#hgBtn').disabled=busy;$('#hgBar').className='bar'+(busy?' ind':'');
  $$('#ups .opt').forEach(o=>{const i=o.querySelector('input');i.checked=i.value===s.upstream_code;o.classList.toggle('sel',i.checked)});
  if(document.activeElement!==$('#aaaa'))$('#aaaa').checked=s.block_aaaa;
  set('ver',s.version);set('foot','v'+s.version);set('up',dur(s.uptime));
}
async function poll(){
  if(document.hidden)return;
  try{render(await api('/api/status'))}
  catch(e){$('#pill').className='pill';set('pillT','Device unreachable')}
}

/* ---------- Wi-Fi ---------- */
async function scan(){
  const b=$('#scanBtn'),o=$('#nets');b.disabled=true;o.textContent='Scanning...';
  try{
    const r=await api('/api/scan');o.replaceChildren();
    if(!r.networks.length)o.textContent='No networks found.';
    r.networks.forEach(n=>{
      const row=el('div','row'),l=el('div','l'),sig=el('span','sig s'+bars(n.rssi)),info=el('div');
      for(let i=0;i<4;i++)sig.append(el('i'));
      info.append(el('b',null,n.ssid),el('div','mu',n.rssi+' dBm, '+(n.secure?'secured':'open')));
      l.append(sig,info);
      const pick=el('button','s','Select');pick.onclick=()=>{$('#ssid').value=n.ssid;$('#pw').focus()};
      row.append(l,pick);o.append(row);
    });
  }catch(e){o.textContent=e.message}
  b.disabled=false;
}
async function connect(){
  const ssid=$('#ssid').value.trim(),pw=$('#pw').value,b=$('#connBtn'),m=$('#wmsg');
  if(!ssid){m.textContent='Enter a network name.';return}
  b.disabled=true;m.textContent='Connecting...';
  try{await api('/api/connect',{ssid,password:pw})}catch(e){m.textContent=e.message;b.disabled=false;return}
  let done=false;
  for(let i=0;i<35&&!done;i++){
    await sleep(1000);
    try{
      const s=await api('/api/status');
      if(s.connect_state==='ok'){m.textContent='Connected. Address: '+s.connect_message;$('#pw').value='';done=true;toast('Connected to '+ssid)}
      else if(s.connect_state==='failed'){m.textContent=s.connect_message||'Connection failed.';done=true;toast('Connection failed',1)}
    }catch(e){m.textContent='Still connecting. The hotspot may drop for a moment.'}
  }
  if(!done)m.textContent='Still waiting. Reconnect to the hotspot and check the Overview tab.';
  b.disabled=false;loadSaved();poll();
}
async function loadSaved(){
  try{
    const r=await api('/api/saved'),o=$('#saved');o.replaceChildren();
    if(!r.networks.length){o.textContent='No saved networks.';return}
    r.networks.forEach(n=>{const row=el('div','row'),f=el('button','d','Forget');f.onclick=()=>forget(n);row.append(el('b',null,n),f);o.append(row)});
  }catch(e){}
}
async function forget(ssid){try{await api('/api/forget',{ssid});loadSaved()}catch(e){toast(e.message,1)}}

/* ---------- filters ---------- */
function chips(id,list,type){
  const o=$(id);o.replaceChildren();
  if(!list.length){o.append(el('span','mu','None'));return}
  list.forEach(d=>{const c=el('span','chip',d),x=el('button',null,'\u00d7');x.title='Remove';x.setAttribute('aria-label','Remove '+d);x.onclick=()=>domain('remove',type,d);c.append(x);o.append(c)});
}
async function loadDomains(){try{const r=await api('/api/domains');chips('#bl',r.blocked,'block');chips('#al',r.allowed,'allow')}catch(e){}}
async function domain(action,type,d){
  if(!d)return;
  try{await api('/api/domain',{action,type,domain:d});if(action==='add')$('#dom').value='';loadDomains()}
  catch(e){toast(e.message,1)}
}
async function updateHG(){
  $('#hgBtn').disabled=true;set('hgMsg','Starting download...');
  try{const r=await api('/api/blocklist/update',{});set('hgMsg',r.message||'Download started.')}
  catch(e){set('hgMsg',e.message);$('#hgBtn').disabled=false}
  poll();
}

/* ---------- settings ---------- */
$('#ups').addEventListener('change',async e=>{
  if(e.target.name!=='up')return;
  try{await api('/api/upstream',{provider:e.target.value});set('upmsg','Saved.');poll()}
  catch(x){set('upmsg',x.message)}
});
$('#aaaa').addEventListener('change',async e=>{
  try{await api('/api/settings',{block_aaaa:e.target.checked?'1':'0'});toast(e.target.checked?'IPv6 lookups skipped':'IPv6 lookups allowed')}
  catch(x){toast(x.message,1);e.target.checked=!e.target.checked}
});
async function flush(){try{await api('/api/cache/flush',{});toast('Cache cleared')}catch(e){toast(e.message,1)}}
async function reboot(){
  if(!confirm('Restart the device? The hotspot will drop for about 10 seconds.'))return;
  try{await api('/api/reboot',{});toast('Restarting...')}catch(e){toast(e.message,1)}
}

/* ---------- start ---------- */
go((location.hash||'#over').slice(1));
poll();
setInterval(poll,3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll()});
</script>
</body>
</html>
)HTML";
