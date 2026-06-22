/* JamShield PWA - attack/defense control room.
 * Live telemetry + control over MQTT/WebSockets. Subscribes to the Pi's unified
 * `jamshield/feed` (WiFi+BLE+ESP-NOW) and publishes control to `jamshield/control`.
 * Turns the raw stream into a story: live RSSI, attack timeline, failover time,
 * packets rescued vs lost, and a protected/unprotected comparison.
 */
"use strict";

var HOST = location.hostname || "127.0.0.1";
var WS_URL = "ws://" + HOST + ":9001";
var RATE = 2;            // node publishes ~2 pkt/s; used to estimate loss on a stall
var STALL_MS = 2200;     // no packet for this long => link considered down

var $ = function (id) { return document.getElementById(id); };
$("broker").textContent = WS_URL;

var client = null, started = false;
var lastRx = 0, lastTick = Date.now(), lastSeq = -1;
var mode = "HOP", jamWanted = false;
var last = { channel: "OFF", jam: "CLEAR", rssi: null, val: "-" };

var delivered = 0, lostGaps = 0, lostFrac = 0, rescued = 0;
var attackOn = false, attackT0 = 0, failoverMs = null, foMarked = false;
var prot = { rescued: 0 }, unprot = { lost: 0 };
var rssiHist = [], feed = [], events = [];

/* ---------- connection ---------- */
function setConn(ok, label) {
  $("cdot").className = "cdot" + (ok ? " on" : "");
  $("cstat").textContent = label || (ok ? "live" : "offline");
}
function connect() {
  client = mqtt.connect(WS_URL, {
    clientId: "jamshield-pwa-" + Math.random().toString(16).slice(2),
    reconnectPeriod: 2000, connectTimeout: 8000, keepalive: 30, clean: true
  });
  client.on("connect", function () { setConn(true, "live"); client.subscribe("jamshield/feed"); });
  client.on("reconnect", function () { setConn(false, "reconnecting"); });
  client.on("close", function () { setConn(false, "offline"); });
  client.on("error", function (e) { console.warn("mqtt", e && e.message); });
  client.on("message", function (t, m) {
    if (t === "jamshield/feed") { try { onPacket(JSON.parse(m.toString())); } catch (e) {} }
  });
}

/* ---------- incoming packet ---------- */
function onPacket(p) {
  var now = Date.now();
  lastRx = now; started = true;
  var ch = p.channel || "WIFI";
  var jam = p.jam_state || "CLEAR";
  var seq = parseInt(p.seq, 10);

  if (!isNaN(seq)) {
    if (lastSeq >= 0 && seq > lastSeq + 1) lostGaps += (seq - lastSeq - 1);
    if (seq !== lastSeq) {
      delivered++; lastSeq = seq;
      feed.unshift({ seq: seq, val: p.val != null ? p.val : "?", ch: ch });
      if (feed.length > 16) feed.pop();
      renderFeed();
    }
  }
  if (p.rssi != null) { rssiHist.push(p.rssi); if (rssiHist.length > 90) rssiHist.shift(); }

  // --- attack lifecycle (driven by what actually arrives) ---
  var underAttack = (jam !== "CLEAR") || (ch !== "WIFI");
  if (underAttack && !attackOn) {            // attack begins
    attackOn = true; attackT0 = now; failoverMs = null; foMarked = false;
    addEvent("attack", "Jamming attack detected", "");
  }
  if (ch !== "WIFI") {                         // a packet survived via a fallback radio
    rescued++; prot.rescued++;
    if (!foMarked && attackOn) {               // first fallback packet = failover complete
      failoverMs = now - attackT0; foMarked = true;
      addEvent("defend", "Failover WiFi to " + ch, failoverMs + " ms");
    }
  }
  if (!underAttack && attackOn) {             // recovered: clean WiFi + CLEAR again
    attackOn = false; jamWanted = false; syncJamBtn();
    addEvent("ok", "Link restored to WiFi", rescued + " rescued, 0 lost");
  }

  last.channel = ch; last.jam = jam;
  if (p.rssi != null) last.rssi = p.rssi;
  if (p.val != null) last.val = p.val;
}

/* ---------- periodic render + stall-based loss ---------- */
function tick() {
  var now = Date.now(), dt = (now - lastTick) / 1000; lastTick = now;
  var live = started && (now - lastRx) < STALL_MS;
  var defended = live && last.channel !== "WIFI";
  // unprotected: an attack is wanted but nothing is getting through
  var losing = jamWanted && !defended && (!live || (last.channel === "WIFI" && last.jam !== "CLEAR"));

  if (losing) {                               // accrue estimated lost packets at the node's rate
    lostFrac += dt * RATE;
    if (mode === "NOHOP") unprot.lost = Math.floor(unprot.lost + dt * RATE);
    if (!attackOn) { attackOn = true; attackT0 = now; addEvent("attack", "Jamming attack detected", "unprotected"); }
  }

  // --- headline status ---
  var cls, st, sd;
  if (!started)        { cls = "secure"; st = "WAITING";          sd = "connecting to sensor node..."; }
  else if (defended)   { cls = "attack"; st = "ATTACK DEFEATED";  sd = "WiFi jammed - data rerouted over " + last.channel; }
  else if (losing)     { cls = "lost";   st = "DATA LOST";        sd = "unprotected - packets dropping while jammed"; }
  else if (live && last.jam === "CLEAR" && last.channel === "WIFI") { cls = "secure"; st = "LINK SECURE"; sd = "telemetry flowing normally"; }
  else                 { cls = "lost";   st = "SIGNAL LOST";      sd = "no data from node"; }

  setStatus(cls, st, sd);
  var liveCh = (live || defended) ? last.channel : "OFF";
  $("chan").textContent = liveCh === "OFF" ? "—" : liveCh;
  $("chan").className = "chip " + liveCh;

  // metrics
  $("rssi").textContent = last.rssi != null ? last.rssi : "—";
  $("m_fo").textContent = failoverMs != null ? failoverMs : "—";
  $("m_resc").textContent = rescued;
  $("m_lost").textContent = lostGaps + Math.floor(lostFrac);

  // comparison
  $("c_prot_resc").textContent = prot.rescued;
  $("c_prot_lost").textContent = 0;
  $("c_prot_pct").textContent = prot.rescued > 0 ? "100%" : "—";
  $("c_unprot_lost").textContent = unprot.lost;
  $("c_unprot_pct").textContent = unprot.lost > 0 ? "0%" : "—";

  drawSpark(cls);
}

/* ---------- UI helpers ---------- */
function setStatus(cls, st, sd) {
  $("hero").className = "glass hero " + cls;
  $("beacon").className = "beacon " + cls;
  $("statetxt").textContent = st; $("statedesc").textContent = sd;
  $("bgwash").style.background = cls === "secure"
    ? "radial-gradient(60% 40% at 30% -8%,rgba(52,211,154,.10),transparent 60%)"
    : "radial-gradient(70% 45% at 50% -8%,rgba(255,93,110,.16),transparent 60%)";
}
function renderFeed() {
  if (!feed.length) { $("feed").innerHTML = '<div class="empty">no packets yet</div>'; return; }
  var h = "";
  for (var i = 0; i < feed.length; i++) {
    var f = feed[i];
    h += '<div class="pk"><span class="seq">#' + f.seq + '</span><span class="val">sensor ' + f.val +
         '</span><span class="tag t-' + f.ch + '">via ' + f.ch + '</span></div>';
  }
  $("feed").innerHTML = h;
}
function addEvent(kind, desc, extra) {
  var d = new Date();
  var t = ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2) + ":" + ("0" + d.getSeconds()).slice(-2);
  events.unshift({ t: t, kind: kind, desc: desc, extra: extra || "" });
  if (events.length > 30) events.pop();
  var h = "";
  for (var i = 0; i < events.length; i++) {
    var e = events[i];
    h += '<div class="ev ' + e.kind + '"><span class="t">' + e.t + '</span><span class="d">' + e.desc +
         '</span><span class="x">' + e.extra + '</span></div>';
  }
  $("tl").innerHTML = h;
}

/* ---------- RSSI sparkline ---------- */
function drawSpark(cls) {
  var c = $("spark"), ctx = c.getContext("2d");
  var W = c.width, H = c.height;
  ctx.clearRect(0, 0, W, H);
  if (rssiHist.length < 2) return;
  var lo = -95, hi = -20, step = W / 89;
  var col = cls === "secure" ? "#34d39a" : (cls === "attack" ? "#5b9dff" : "#ff5d6e");
  var n = rssiHist.length, x0 = W - (n - 1) * step;
  ctx.beginPath();
  for (var i = 0; i < n; i++) {
    var x = W - (n - 1 - i) * step;
    var y = H - ((rssiHist[i] - lo) / (hi - lo)) * (H - 6) - 3;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.lineWidth = 2; ctx.strokeStyle = col; ctx.lineJoin = "round"; ctx.stroke();
  ctx.lineTo(W, H); ctx.lineTo(x0, H); ctx.closePath();
  var g = ctx.createLinearGradient(0, 0, 0, H);
  g.addColorStop(0, col + "44"); g.addColorStop(1, col + "00");
  ctx.fillStyle = g; ctx.fill();
  var ly = H - ((rssiHist[n - 1] - lo) / (hi - lo)) * (H - 6) - 3;
  ctx.beginPath(); ctx.arc(W - 2, ly, 3, 0, 7); ctx.fillStyle = col; ctx.fill();
}

/* ---------- control ---------- */
function pub(topic, msg) { if (client && client.connected) client.publish(topic, msg); }
function cmd(m) { mode = m; pub("jamshield/control", m); highlightMode(); }
function highlightMode() {
  ["HOP", "NOHOP", "NOBLE"].forEach(function (m) { $("m_" + m.toLowerCase()).className = (mode === m) ? "on" : ""; });
  $("modehint").textContent = mode === "HOP" ? "HOP: WiFi to BLE to ESP-NOW failover"
    : mode === "NOHOP" ? "NO-HOP: stay on WiFi - jam = data lost (the problem)"
    : "NO-BLE: WiFi to ESP-NOW (skip BLE)";
}
function toggleJam() {
  jamWanted = !jamWanted;
  pub("jamshield/control", jamWanted ? "JAM" : "CLEAR");
  if (jamWanted && !attackOn) attackT0 = Date.now();
  syncJamBtn();
}
function syncJamBtn() {
  $("jbtn").className = "jbtn" + (jamWanted ? " on" : "");
  $("jbtnt").textContent = jamWanted ? "Stop attack" : "Launch jamming attack";
}
$("resetbtn").onclick = function () {
  delivered = 0; lostGaps = 0; lostFrac = 0; rescued = 0; lastSeq = -1;
  prot = { rescued: 0 }; unprot = { lost: 0 }; failoverMs = null;
  events = []; $("tl").innerHTML = '<div class="empty">no events yet</div>';
};

/* ---------- PWA install ---------- */
var deferred = null;
window.addEventListener("beforeinstallprompt", function (e) { e.preventDefault(); deferred = e; $("toast").classList.add("show"); });
$("installbtn").onclick = function () { $("toast").classList.remove("show"); if (deferred) { deferred.prompt(); deferred = null; } };
if ("serviceWorker" in navigator) navigator.serviceWorker.register("sw.js").catch(function () {});

highlightMode(); syncJamBtn(); renderFeed(); connect();
setInterval(tick, 250);
