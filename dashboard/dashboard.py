#!/usr/bin/env python3
"""JamShield - live control dashboard (runs on the laptop, talks over USB).

Reads the node's STAT line on COM6 (active channel, jam state, sensor value,
delivered/lost, ...), controls the node mode over COM6 (h/n/e) and triggers the
jam over COM6 (j/c) + the RF jammer over COM7 (s/x), and serves a single-page
web UI. Stdlib + pyserial only.

  python dashboard.py [--node COM6] [--jammer COM7] [--port 8080]
"""
from __future__ import annotations

import argparse
import json
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial  # pyserial (installed with esptool)
except ImportError:
    raise SystemExit("pyserial missing: python -m pip install pyserial")

TIMELINE_MAX = 140
FEED_MAX = 16

state = {
    "node_open": False, "jammer_open": False, "jammer_on": False, "rf_on": False,
    "mode": "?", "ch": "?", "jam": "?",
    "seq": 0, "rssi": 0, "loss": 0, "wifi": 0, "deliv": 0, "ldr": 0,
    "counts": {"WIFI": 0, "BLE": 0, "ESPNOW": 0, "DROP": 0},
    "timeline": [], "feed": [], "last_seq": -1, "last_update": 0.0,
}
state_lock = threading.Lock()
node_ser = None
jammer_ser = None


# ----------------------------- serial -----------------------------
def node_reader(port: str, baud: int):
    global node_ser
    while True:
        try:
            node_ser = serial.Serial(port, baud, timeout=1)
            with state_lock:
                state["node_open"] = True
            buf = b""
            while True:
                data = node_ser.read(256)
                if not data:
                    continue
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    _handle(line.decode("utf-8", "replace").strip())
        except Exception as exc:
            with state_lock:
                state["node_open"] = False
            print(f"[node] {port}: {exc}; retry 2s")
            try:
                node_ser and node_ser.close()
            except Exception:
                pass
            node_ser = None
            time.sleep(2)


def _handle(line: str):
    i = line.find("STAT ")
    if i < 0:
        return
    try:
        d = json.loads(line[i + 5:])
    except Exception:
        return
    with state_lock:
        ch, deliv, seq = d.get("ch", "?"), int(d.get("deliv", 0)), int(d.get("seq", 0))
        state.update({
            "mode": d.get("mode", "?"), "ch": ch, "jam": d.get("jam", "?"),
            "seq": seq, "rssi": int(d.get("rssi", 0)), "loss": int(d.get("loss", 0)),
            "wifi": int(d.get("wifi", 0)), "deliv": deliv, "ldr": int(d.get("ldr", 0)),
            "last_update": time.time(),
        })
        if seq != state["last_seq"]:
            state["last_seq"] = seq
            key = ch if (deliv and ch in state["counts"]) else "DROP"
            state["counts"][key] = state["counts"].get(key, 0) + 1
            state["timeline"].append({"ch": key, "jam": d.get("jam", "?")})
            if len(state["timeline"]) > TIMELINE_MAX:
                state["timeline"] = state["timeline"][-TIMELINE_MAX:]
            state["feed"].append({"seq": seq, "val": int(d.get("ldr", 0)),
                                  "ch": ch, "deliv": deliv})
            if len(state["feed"]) > FEED_MAX:
                state["feed"] = state["feed"][-FEED_MAX:]


def open_jammer(port: str, baud: int):
    global jammer_ser
    try:
        jammer_ser = serial.Serial(port, baud, timeout=1)
        with state_lock:
            state["jammer_open"] = True
        print(f"[jammer] {port} open")
    except Exception as exc:
        print(f"[jammer] {port} unavailable: {exc}")


def _w(ser, cmd):
    try:
        ser and ser.write(cmd.encode())
    except Exception as exc:
        print(f"[serial] write {cmd!r}: {exc}")


# ----------------------------- HTTP -----------------------------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            return self._send(200, PAGE, "text/html; charset=utf-8")
        if self.path == "/api/state":
            with state_lock:
                return self._send(200, json.dumps(state))
        if self.path.startswith("/api/cmd"):
            from urllib.parse import urlparse, parse_qs
            a = parse_qs(urlparse(self.path).query).get("a", [""])[0]
            if a == "hop":
                _w(node_ser, "h")
            elif a == "nohop":
                _w(node_ser, "n")
            elif a == "noble":
                _w(node_ser, "e")
            elif a == "jam_on":
                # Control-link jam: node runs its real FSM, WiFi stays up ->
                # instant failover AND instant recovery.
                _w(node_ser, "j")
                with state_lock:
                    state["jammer_on"] = True
            elif a == "jam_off":
                _w(node_ser, "c")
                with state_lock:
                    state["jammer_on"] = False
            elif a == "rf_on":
                # Real over-the-air deauth from ESP32 #2 (recovery is slower:
                # WiFi must fully re-establish afterwards).
                _w(jammer_ser, "s")
                with state_lock:
                    state["rf_on"] = True
            elif a == "rf_off":
                _w(jammer_ser, "x")
                with state_lock:
                    state["rf_on"] = False
            elif a == "reset":
                with state_lock:
                    state["counts"] = {"WIFI": 0, "BLE": 0, "ESPNOW": 0, "DROP": 0}
                    state["feed"] = []
                    state["timeline"] = []
            return self._send(200, '{"ok":true}')
        self._send(404, "{}")


# ----------------------------- UI -----------------------------
PAGE = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>JamShield Live</title>
<style>
:root{
  --bg:#0a0d15;--txt:#eef2fb;--mut:#8893ac;--line:rgba(255,255,255,.09);
  --glass:rgba(255,255,255,.045);--glass2:rgba(255,255,255,.07);
  --accent:#4c8dff;--wifi:#34d39a;--ble:#5b9dff;--espnow:#f0a45a;--drop:#ff5d6e;--susp:#f5c451;
  --mono:ui-monospace,"Cascadia Mono","JetBrains Mono",Consolas,monospace;
  --sans:ui-sans-serif,system-ui,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{background:var(--bg);color:var(--txt);font-family:var(--sans);
  letter-spacing:.1px;-webkit-font-smoothing:antialiased;padding:24px;position:relative;overflow-x:hidden}
/* soft mesh so the frosted glass actually refracts (no neon) */
body::before{content:"";position:fixed;inset:0;z-index:-1;pointer-events:none;
  background:
   radial-gradient(540px 380px at 12% -8%,rgba(76,141,255,.16),transparent 60%),
   radial-gradient(520px 420px at 92% 4%,rgba(52,211,154,.12),transparent 60%),
   radial-gradient(640px 520px at 78% 108%,rgba(240,164,90,.08),transparent 60%);}
.wrap{max-width:1280px;margin:0 auto}
.bar{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:12px}
.mark{width:34px;height:34px;border-radius:10px;display:grid;place-items:center;
  background:linear-gradient(160deg,rgba(76,141,255,.9),rgba(52,211,154,.85));
  box-shadow:inset 0 1px 0 rgba(255,255,255,.25)}
.mark svg{width:19px;height:19px;fill:#0a0d15}
.brand h1{font-size:18px;font-weight:650;letter-spacing:-.2px}
.brand .s{font-size:12px;color:var(--mut)}
.chips{display:flex;gap:8px;align-items:center;font-size:12px;color:var(--mut)}
.chip{display:flex;align-items:center;gap:7px;padding:7px 11px;border:1px solid var(--line);
  border-radius:999px;background:var(--glass);backdrop-filter:blur(12px)}
.dot{width:7px;height:7px;border-radius:50%}.up{background:var(--wifi)}.dn{background:#566}
.glass{background:var(--glass);backdrop-filter:blur(20px) saturate(120%);
  -webkit-backdrop-filter:blur(20px) saturate(120%);border:1px solid var(--line);
  border-radius:14px;box-shadow:inset 0 1px 0 rgba(255,255,255,.07),0 22px 48px -26px rgba(0,0,0,.7)}
.k{font-size:10.5px;font-weight:700;letter-spacing:1.6px;text-transform:uppercase;color:var(--mut)}
.grid{display:grid;gap:16px;margin-top:18px}
.g1{grid-template-columns:1.35fr 1fr}
.g2{grid-template-columns:1.1fr .9fr}
@media(max-width:860px){.g1,.g2{grid-template-columns:1fr}}
.pad{padding:20px}
/* hero */
.chanrow{display:flex;align-items:flex-end;gap:18px;margin-top:8px}
.chan{font-size:58px;font-weight:760;letter-spacing:-1.5px;line-height:.92;font-family:var(--mono)}
.chan.WIFI{color:var(--wifi)}.chan.BLE{color:var(--ble)}.chan.ESPNOW{color:var(--espnow)}
.deliv{margin-left:auto;text-align:right}
.pill{display:inline-flex;align-items:center;gap:8px;padding:8px 15px;border-radius:999px;
  font-weight:650;font-size:14px;border:1px solid}
.pill .d{width:8px;height:8px;border-radius:50%}
.ok{color:var(--wifi);border-color:rgba(52,211,154,.45);background:rgba(52,211,154,.10)}
.ok .d{background:var(--wifi)}
.bad{color:var(--drop);border-color:rgba(255,93,110,.45);background:rgba(255,93,110,.10)}
.bad .d{background:var(--drop)}
.jam{margin-top:16px;border-radius:11px;padding:12px 15px;font-weight:650;font-size:13.5px;
  border:1px solid var(--line);display:flex;align-items:center;gap:10px}
.jam .d{width:9px;height:9px;border-radius:50%}
.j-CLEAR{color:var(--wifi)}.j-CLEAR .d{background:var(--wifi)}
.j-SUSPECTED{color:var(--susp);border-color:rgba(245,196,81,.4)}.j-SUSPECTED .d{background:var(--susp)}
.j-CONFIRMED{color:var(--drop);border-color:rgba(255,93,110,.5);background:rgba(255,93,110,.07)}.j-CONFIRMED .d{background:var(--drop);animation:blink 1s steps(2,end) infinite}
.j-RECOVERING{color:var(--ble);border-color:rgba(91,157,255,.4)}.j-RECOVERING .d{background:var(--ble)}
@keyframes blink{50%{opacity:.25}}
/* mode buttons */
.modes{display:flex;flex-direction:column;gap:9px;margin-top:10px}
.mbtn{cursor:pointer;text-align:left;border:1px solid var(--line);border-radius:11px;padding:12px 14px;
  background:var(--glass);color:var(--txt);transition:transform .12s,border-color .12s,background .12s;
  -webkit-backdrop-filter:blur(8px);backdrop-filter:blur(8px)}
.mbtn:hover{border-color:rgba(255,255,255,.2)}
.mbtn:active{transform:translateY(1px)}
.mbtn b{font-size:13.5px;font-weight:650}.mbtn span{display:block;color:var(--mut);font-size:11px;margin-top:2px}
.mbtn.on{border-color:rgba(76,141,255,.7);background:rgba(76,141,255,.13);box-shadow:inset 0 0 0 1px rgba(76,141,255,.35)}
.jbtn{cursor:pointer;width:100%;margin-top:14px;border:1px solid var(--line);border-radius:12px;padding:16px;
  font-size:15px;font-weight:700;letter-spacing:.3px;background:var(--glass2);color:var(--txt);
  transition:transform .12s,background .12s}
.jbtn:hover{background:rgba(255,255,255,.1)}.jbtn:active{transform:translateY(1px)}
.jbtn.on{background:rgba(255,93,110,.16);border-color:rgba(255,93,110,.55);color:#ffd9de}
.jbtn.on .d{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--drop);margin-right:8px;animation:blink 1s steps(2,end) infinite;vertical-align:middle}
.rfbtn{cursor:pointer;width:100%;margin-top:8px;border:1px solid var(--line);border-radius:10px;padding:10px;
  font-size:12px;font-weight:600;background:transparent;color:var(--mut);transition:transform .12s,color .12s,border-color .12s}
.rfbtn:hover{color:var(--txt);border-color:rgba(255,255,255,.2)}.rfbtn:active{transform:translateY(1px)}
.rfbtn.on{color:#ffd9de;border-color:rgba(255,93,110,.55);background:rgba(255,93,110,.12)}
/* integrity */
.integ{display:grid;grid-template-columns:repeat(4,1fr);gap:0;margin-top:8px}
.integ .cell{padding:12px 6px;border-left:1px solid var(--line)}
.integ .cell:first-child{border-left:0;padding-left:0}
.integ .n{font-family:var(--mono);font-size:30px;font-weight:600;line-height:1;margin-top:8px}
.bars{margin-top:6px}
.brow{margin:11px 0}.brow .h{display:flex;justify-content:space-between;font-size:12.5px}
.brow .h .nm{font-weight:650}.brow .h .v{font-family:var(--mono);color:var(--mut)}
.track{height:7px;border-radius:5px;background:rgba(255,255,255,.06);margin-top:6px;overflow:hidden}
.fill{height:100%;border-radius:5px;transition:width .45s cubic-bezier(.16,1,.3,1)}
/* feed */
.feed{margin-top:8px;display:flex;flex-direction:column;gap:6px;max-height:340px;overflow:auto}
.pk{display:flex;align-items:center;gap:10px;padding:9px 12px;border-radius:10px;border:1px solid var(--line);
  background:rgba(255,255,255,.025);font-size:13px}
.pk.lost{border-color:rgba(255,93,110,.35);background:rgba(255,93,110,.06)}
.pk .seq{font-family:var(--mono);color:var(--mut);min-width:62px}
.pk .val{font-family:var(--mono)}
.pk .tag{margin-left:auto;font-size:11px;font-weight:700;letter-spacing:.4px;padding:3px 9px;border-radius:999px}
.t-WIFI{color:var(--wifi);background:rgba(52,211,154,.12)}
.t-BLE{color:var(--ble);background:rgba(91,157,255,.12)}
.t-ESPNOW{color:var(--espnow);background:rgba(240,164,90,.12)}
.t-LOST{color:var(--drop);background:rgba(255,93,110,.14)}
.tl{display:flex;gap:2px;flex-wrap:wrap;margin-top:8px}
.tcell{width:10px;height:22px;border-radius:2px}
.c-WIFI{background:var(--wifi)}.c-BLE{background:var(--ble)}.c-ESPNOW{background:var(--espnow)}.c-DROP{background:var(--drop)}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:0;margin-top:8px}
.stats .cell{padding:10px 6px;border-left:1px solid var(--line)}
.stats .cell:first-child{border-left:0;padding-left:0}
.stats .n{font-family:var(--mono);font-size:21px;font-weight:600;margin-top:6px}
.foot{color:var(--mut);font-size:11.5px;margin-top:10px}
.linkbtn{cursor:pointer;background:none;border:0;color:var(--mut);font-size:11.5px;text-decoration:underline}
</style></head><body><div class=wrap>

<div class=bar>
  <div class=brand>
    <div class=mark><svg viewBox="0 0 24 24"><path d="M12 2 4 5v6c0 5 3.4 8.4 8 11 4.6-2.6 8-6 8-11V5l-8-3Zm0 4.2 4 1.5v3.3c0 3.3-2 5.6-4 7-2-1.4-4-3.7-4-7V7.7l4-1.5Z"/></svg></div>
    <div><h1>JamShield <span style=color:var(--mut);font-weight:400>/ Live Control</span></h1>
      <div class=s>jamming resilience &mdash; WiFi to BLE to ESP-NOW</div></div>
  </div>
  <div class=chips>
    <span class=chip><span id=ndot class="dot dn"></span>node</span>
    <span class=chip><span id=jdot class="dot dn"></span>jammer</span>
    <span class=chip id=live>idle</span>
  </div>
</div>

<div class="grid g1">
  <div class="glass pad">
    <div class=k>Active channel</div>
    <div class=chanrow>
      <div class="chan" id=chan>--</div>
      <div class=deliv><div class=k>Live data</div>
        <div id=deliv class="pill ok" style=margin-top:8px><span class=d></span><span id=delivt>--</span></div></div>
    </div>
    <div id=jam class="jam j-CLEAR"><span class=d></span><span id=jamt>--</span></div>
  </div>

  <div class="glass pad">
    <div class=k>Protection mode</div>
    <div class=modes>
      <button class=mbtn id=m_hop onclick="cmd('hop')"><b>HOP &mdash; full protection</b>
        <span>WiFi to BLE to ESP-NOW automatic failover</span></button>
      <button class=mbtn id=m_nohop onclick="cmd('nohop')"><b>NO-HOP &mdash; unprotected</b>
        <span>Stay on WiFi; a jam means data is lost</span></button>
      <button class=mbtn id=m_noble onclick="cmd('noble')"><b>NO-BLE</b>
        <span>Skip BLE; WiFi to ESP-NOW</span></button>
    </div>
    <button class=jbtn id=jbtn onclick=tj()><span class=d></span><span id=jbtnt>Start jam</span></button>
    <button class=rfbtn id=rfbtn onclick=trf()>RF jammer (real OTA &mdash; slower recovery)</button>
  </div>
</div>

<div class="grid g2">
  <div class="glass pad">
    <div class=k>Data integrity <button class=linkbtn onclick="cmd('reset')">reset</button></div>
    <div class=integ>
      <div class=cell><div class=k>Sent</div><div class=n id=i_sent>0</div></div>
      <div class=cell><div class=k>Delivered</div><div class=n style=color:var(--wifi) id=i_dlv>0</div></div>
      <div class=cell><div class=k>Lost</div><div class=n style=color:var(--drop) id=i_lost>0</div></div>
      <div class=cell><div class=k>Delivery</div><div class=n id=i_rate>--</div></div>
    </div>
    <div class=stats>
      <div class=cell><div class=k>RSSI</div><div class=n id=rssi>--</div></div>
      <div class=cell><div class=k>Loss %</div><div class=n id=loss>--</div></div>
      <div class=cell><div class=k>WiFi</div><div class=n id=wifi>--</div></div>
      <div class=cell><div class=k>Sensor</div><div class=n id=ldr>--</div></div>
    </div>
    <div class=bars id=bars></div>
  </div>

  <div class="glass pad">
    <div class=k>Data packets <span style=color:var(--mut);font-weight:400;text-transform:none;letter-spacing:0> &mdash; newest first</span></div>
    <div class=feed id=feed></div>
    <div class=tl id=tl></div>
    <div class=foot>Each packet carries a sensor reading. Watch deliveries continue on BLE during a jam (HOP) or stop dead (NO-HOP).</div>
  </div>
</div>
</div>

<script>
const C={WIFI:'#34d39a',BLE:'#5b9dff',ESPNOW:'#f0a45a',DROP:'#ff5d6e'};
let JAMON=false,RFON=false;
function cmd(a){fetch('/api/cmd?a='+a);}
function tj(){fetch('/api/cmd?a='+(JAMON?'jam_off':'jam_on'));}
function trf(){fetch('/api/cmd?a='+(RFON?'rf_off':'rf_on'));}
function $(id){return document.getElementById(id);}
async function tick(){
  let s;try{s=await(await fetch('/api/state')).json();}catch(e){return;}
  JAMON=s.jammer_on;
  $('ndot').className='dot '+(s.node_open?'up':'dn');
  $('jdot').className='dot '+(s.jammer_open?'up':'dn');
  $('live').textContent=(s.last_update&&(Date.now()/1000-s.last_update)<3)?'live':'idle';
  const ch=s.ch||'--';
  $('chan').textContent=ch;$('chan').className='chan '+ch;
  $('delivt').textContent=s.deliv?'DELIVERING':'DATA LOST';
  $('deliv').className='pill '+(s.deliv?'ok':'bad');
  $('jamt').textContent=(s.jam=='CONFIRMED'?'JAMMING DETECTED - ':'')+'jam state: '+s.jam;
  $('jam').className='jam j-'+s.jam;
  $('rssi').textContent=s.rssi;$('loss').textContent=s.loss;
  $('wifi').innerHTML=s.wifi?'<span style=color:var(--wifi)>up</span>':'<span style=color:var(--drop)>down</span>';
  $('ldr').textContent=s.ldr;
  for(const m of['hop','nohop','noble'])
    $('m_'+m).classList.toggle('on',s.mode==(m=='hop'?'HOP':m=='nohop'?'NOHOP':'NOBLE'));
  $('jbtn').className='jbtn'+(JAMON?' on':'');$('jbtnt').textContent=JAMON?'Stop jam':'Start jam';
  RFON=s.rf_on;$('rfbtn').className='rfbtn'+(RFON?' on':'');
  $('rfbtn').textContent=RFON?'RF jammer ON - click to stop':'RF jammer (real OTA - slower recovery)';
  const dlv=s.counts.WIFI+s.counts.BLE+s.counts.ESPNOW, lost=s.counts.DROP, sent=dlv+lost;
  $('i_sent').textContent=sent;$('i_dlv').textContent=dlv;$('i_lost').textContent=lost;
  $('i_rate').textContent=sent?((100*dlv/sent).toFixed(1)+'%'):'--';
  let mx=Math.max(1,...Object.values(s.counts)),h='';
  for(const k of['WIFI','BLE','ESPNOW','DROP'])
    h+=`<div class=brow><div class=h><span class=nm style=color:${C[k]}>${k}</span><span class=v>${s.counts[k]}</span></div>
        <div class=track><div class=fill style="width:${100*s.counts[k]/mx}%;background:${C[k]}"></div></div></div>`;
  $('bars').innerHTML=h;
  $('feed').innerHTML=s.feed.slice().reverse().map(p=>{
    const lost=!p.deliv,tag=lost?'LOST':p.ch;
    return `<div class="pk ${lost?'lost':''}"><span class=seq>#${p.seq}</span>
      <span class=val>sensor ${p.val}</span>
      <span class="tag t-${tag}">${lost?'LOST':'via '+p.ch}</span></div>`;}).join('');
  $('tl').innerHTML=s.timeline.map(x=>`<div class="tcell c-${x.ch}" title="${x.ch} / ${x.jam}"></div>`).join('');
}
setInterval(tick,400);tick();
</script></body></html>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--node", default="")
    ap.add_argument("--jammer", default="")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--no-open", action="store_true")
    args = ap.parse_args()

    node, jammer = args.node, args.jammer
    # Auto-detect which board is which if not given (robust to USB port swaps).
    if not node or (not jammer and (jammer or "").upper() != "COM_NONE"):
        import os
        import subprocess
        try:
            det = os.path.join(os.path.dirname(__file__), "..", "scripts", "detect_ports.py")
            d = json.loads(subprocess.check_output(["python", det], text=True, timeout=40))
            node = node or d.get("node") or "COM6"
            if not jammer:
                jammer = d.get("jammer") or ""
            print(f"[dashboard] auto-detected node={node} jammer={jammer}")
        except Exception as exc:
            print(f"[dashboard] port auto-detect failed ({exc}); using node={node or 'COM6'}")
            node = node or "COM6"

    threading.Thread(target=node_reader, args=(node, args.baud), daemon=True).start()
    if jammer and jammer.upper() != "COM_NONE":
        open_jammer(jammer, args.baud)

    url = f"http://127.0.0.1:{args.port}/"
    print(f"[dashboard] node={node} jammer={jammer}  ->  {url}")
    if not args.no_open:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
