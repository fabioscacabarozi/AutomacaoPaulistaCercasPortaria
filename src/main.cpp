/**
 * @file main.cpp
 * @brief Controle de 4 cercas elétricas via ESP32 com interface web em tempo real.
 *
 * Cerca 1: entrada 1 (disparo) + relé 1 (ativar/desativar)
 * Cerca 2: entrada 2 (disparo) + relé 2 (ativar/desativar)
 * Cerca 3: entrada 3 (disparo) + relé 3 (ativar/desativar)
 * Cerca 4: entrada 4 (disparo) + relé 4 (ativar/desativar)
 *
 * Endpoints locais:
 *  GET  /            Interface HTML com status em tempo real
 *  GET  /status/N    JSON com estado da cerca N (1-4)
 *  POST /fence/N     Body: {"enabled":true|false} — ativa ou desativa cerca N
 *
 * Notificação externa (disparo):
 *  POST http://localhost:8080/status/{id}   Body: {"triggered":true|false,"enabled":true|false}
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "IOBoard.h"

// ── Configuração WiFi ─────────────────────────────────────────────────────────
#define WIFI_SSID              "wifi_scacabarozi"
#define WIFI_PASS              "e000000000"
#define WIFI_RETRY_INTERVAL_MS  10000

// ── Segurança ─────────────────────────────────────────────────────────────────
#define API_KEY "senha"

// ── API Java externa (cercas 3 e 4) ──────────────────────────────────────────
#define EXT_STATUS_URL       "http://192.168.0.107:8080/status?id=%d"
#define EXT_TOGGLE_URL       "http://192.168.0.107:8080/ligardesligar/%d"
#define EXT_HTTP_TIMEOUT_MS  3000

// ── Número de cercas ──────────────────────────────────────────────────────────
#define NUM_FENCES 4

IOBoard board;
WebServer server(80);

// ── Estado WiFi ───────────────────────────────────────────────────────────────
static unsigned long _wifiRetryAt     = 0;
static bool          _wifiWasConnected = false;

// ── Estado das cercas ─────────────────────────────────────────────────────────
struct Fence {
    bool enabled;   // relé ligado = cerca ativa
    bool triggered; // disparo detectado (borda de subida na entrada)
};

static Fence fence[NUM_FENCES] = {
    {false, false}, {false, false}, {false, false}, {false, false}
};

static bool _lastInput[NUM_FENCES] = {false, false, false, false};

// ── HTML da interface ─────────────────────────────────────────────────────────
static const char HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cercas Elétricas</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Segoe UI',Arial,sans-serif;background:#0d0d1a;color:#eee;display:flex;flex-direction:column;align-items:center;padding:24px 16px;min-height:100vh}
  h1{margin-bottom:8px;font-size:1.7rem;color:#e94560;letter-spacing:1px;text-transform:uppercase}
  .subtitle{font-size:.75rem;color:#555;margin-bottom:28px;letter-spacing:2px;text-transform:uppercase}
  .topbar{display:flex;align-items:center;gap:12px;margin-bottom:28px;flex-direction:column}
  .mute-btn{display:flex;align-items:center;gap:8px;background:#1e1e3a;border:1px solid #333;color:#aaa;padding:8px 18px;border-radius:24px;font-size:.82rem;cursor:pointer;transition:.2s;user-select:none}
  .mute-btn:hover{background:#2a2a4a;color:#eee}
  .mute-btn.muted{border-color:#e94560;color:#e94560}
  .mute-icon{font-size:1rem}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:18px;width:100%;max-width:720px}
  @media(max-width:480px){.grid{grid-template-columns:1fr}}

  /* Card base */
  .card{background:#12122a;border-radius:14px;padding:20px;display:flex;flex-direction:column;gap:14px;border:2px solid #1e2a50;transition:border-color .3s,box-shadow .3s;position:relative;overflow:hidden}
  .card::before{content:'';position:absolute;inset:0;border-radius:12px;opacity:0;transition:opacity .3s;pointer-events:none}
  .card-header{display:flex;align-items:center;gap:10px}
  .card-icon{font-size:1.4rem}
  .card h2{font-size:1rem;color:#a8dadc;letter-spacing:.5px;font-weight:600}
  .divider{height:1px;background:#1e2a50;margin:0 -4px}
  .row{display:flex;justify-content:space-between;align-items:center}
  .label{font-size:.8rem;color:#666;text-transform:uppercase;letter-spacing:.8px}
  .badge{padding:5px 14px;border-radius:20px;font-size:.78rem;font-weight:700;letter-spacing:.4px;text-transform:uppercase}
  .on      {background:#1b4332;color:#52b788;border:1px solid #2d6a4f}
  .off     {background:#3d0c1a;color:#ff7096;border:1px solid #6b2737}
  .offline {background:#1a1a1a;color:#555;border:1px solid #333}

  /* Triggered badge — sempre visível e animado */
  .trig-yes{background:#7f1d1d;color:#fca5a5;border:1px solid #ef4444;font-size:.85rem;padding:6px 16px;animation:badge-pulse 1s ease-in-out infinite}
  .trig-no {background:#0f1f1f;color:#4a7c7c;border:1px solid #1e3a3a}
  @keyframes badge-pulse{0%,100%{opacity:1}50%{opacity:.6}}

  /* Card em alarme */
  .card.alarme{border-color:#ef4444;animation:card-flash 1s ease-in-out infinite;box-shadow:0 0 0 0 rgba(239,68,68,.6)}
  @keyframes card-flash{
    0%  {box-shadow:0 0 0 0 rgba(239,68,68,.7),inset 0 0 0 0 rgba(239,68,68,0)}
    50% {box-shadow:0 0 24px 6px rgba(239,68,68,.35),inset 0 0 40px 0 rgba(239,68,68,.06)}
    100%{box-shadow:0 0 0 0 rgba(239,68,68,.7),inset 0 0 0 0 rgba(239,68,68,0)}
  }
  .card.alarme .card-icon{animation:icon-shake .4s ease-in-out infinite}
  @keyframes icon-shake{0%,100%{transform:rotate(0)}25%{transform:rotate(-8deg)}75%{transform:rotate(8deg)}}
  .alarme-banner{display:none;background:linear-gradient(90deg,#7f1d1d,#991b1b,#7f1d1d);color:#fca5a5;font-weight:800;font-size:.78rem;letter-spacing:2px;text-align:center;padding:7px 4px;border-radius:7px;text-transform:uppercase;animation:banner-blink .6s step-start infinite}
  @keyframes banner-blink{0%,100%{opacity:1}50%{opacity:0}}
  .card.alarme .alarme-banner{display:block}

  /* Botão */
  .btn{width:100%;padding:11px;border:none;border-radius:9px;font-size:.88rem;font-weight:700;cursor:pointer;transition:.2s;letter-spacing:.3px;margin-top:2px}
  .btn-disable{background:linear-gradient(135deg,#c0392b,#e94560);color:#fff;box-shadow:0 3px 10px rgba(233,69,96,.3)}
  .btn-enable {background:linear-gradient(135deg,#1b4332,#2d6a4f);color:#d8f3dc;box-shadow:0 3px 10px rgba(45,106,79,.3)}
  .btn:disabled{opacity:.35;cursor:not-allowed;box-shadow:none}
  .btn:not(:disabled):hover{filter:brightness(1.15);transform:translateY(-1px)}
  .btn:not(:disabled):active{transform:translateY(0)}

  footer{margin-top:32px;font-size:.7rem;color:#333;letter-spacing:1px;text-transform:uppercase}
  .dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:#2d6a4f;margin-right:6px;animation:dot-pulse 2s ease-in-out infinite}
  @keyframes dot-pulse{0%,100%{opacity:1}50%{opacity:.3}}
</style>
</head>
<body>
<div class="topbar">
  <h1>&#x26A1; Cercas Elétricas</h1>
  <div class="subtitle">Painel de monitoramento</div>
  <button class="mute-btn" id="muteBtn" onclick="toggleMute()">
    <span class="mute-icon" id="muteIcon">&#x1F50A;</span>
    <span id="muteLabel">Silenciar alarme sonoro</span>
  </button>
</div>

<div class="grid" id="grid">
  <div class="card" id="card1">
    <div class="card-header"><span class="card-icon">&#x1F6E1;</span><h2>Cerca 1</h2></div>
    <div class="divider"></div>
    <div class="row"><span class="label">Status</span><span id="s1-enabled" class="badge">--</span></div>
    <div class="row"><span class="label">Disparo</span><span id="s1-trig" class="badge">--</span></div>
    <div class="alarme-banner">&#x26A0; ALARME ATIVO &#x26A0;</div>
    <button id="btn1" class="btn" onclick="toggle(1)">--</button>
  </div>
  <div class="card" id="card2">
    <div class="card-header"><span class="card-icon">&#x1F6E1;</span><h2>Cerca 2</h2></div>
    <div class="divider"></div>
    <div class="row"><span class="label">Status</span><span id="s2-enabled" class="badge">--</span></div>
    <div class="row"><span class="label">Disparo</span><span id="s2-trig" class="badge">--</span></div>
    <div class="alarme-banner">&#x26A0; ALARME ATIVO &#x26A0;</div>
    <button id="btn2" class="btn" onclick="toggle(2)">--</button>
  </div>
</div>

<footer><span class="dot"></span>Atualiza a cada 2 s &bull; API Key obrigatória</footer>

<script>
const KEY        = "senha";
const TIMEOUT_MS = 3000;
const FENCES     = [1, 2]; //, 3, 4];

const lastKnown = {};
FENCES.forEach(n => lastKnown[n] = null);

// ── Áudio ──────────────────────────────────────────────────────────────────
let audioCtx = null;
let muted    = false;
let beepLoop = null;

function getAudioCtx() {
  if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  return audioCtx;
}

function beep() {
  if (muted) return;
  try {
    const ctx  = getAudioCtx();
    const osc  = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.type      = "square";
    osc.frequency.setValueAtTime(880, ctx.currentTime);
    osc.frequency.setValueAtTime(660, ctx.currentTime + 0.12);
    gain.gain.setValueAtTime(0.18, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.28);
    osc.start(ctx.currentTime);
    osc.stop(ctx.currentTime + 0.28);
  } catch(e) {}
}

function anyTriggered() {
  return FENCES.some(n => lastKnown[n] && lastKnown[n].triggered);
}

function startBeepLoop() {
  if (beepLoop) return;
  beep();
  beepLoop = setInterval(() => { if (anyTriggered()) beep(); else stopBeepLoop(); }, 1800);
}

function stopBeepLoop() {
  clearInterval(beepLoop);
  beepLoop = null;
}

function toggleMute() {
  muted = !muted;
  document.getElementById("muteIcon").textContent  = muted ? "🔇" : "🔊";
  document.getElementById("muteLabel").textContent = muted ? "Reativar alarme sonoro" : "Silenciar alarme sonoro";
  document.getElementById("muteBtn").classList.toggle("muted", muted);
}

// ── UI ─────────────────────────────────────────────────────────────────────
function fetchWithTimeout(url, opts) {
  const ctrl = new AbortController();
  const tid  = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  return fetch(url, {...opts, signal: ctrl.signal}).finally(() => clearTimeout(tid));
}

function applyState(n, f) {
  const enEl = document.getElementById("s" + n + "-enabled");
  const trEl = document.getElementById("s" + n + "-trig");
  const btn  = document.getElementById("btn" + n);
  const card = document.getElementById("card" + n);

  btn.disabled = false;

  if (f.enabled) {
    enEl.textContent = "Ligada";
    enEl.className   = "badge on";
    btn.textContent  = "Desativar Cerca " + n;
    btn.className    = "btn btn-disable";
  } else {
    enEl.textContent = "Desligada";
    enEl.className   = "badge off";
    btn.textContent  = "Ativar Cerca " + n;
    btn.className    = "btn btn-enable";
  }

  if (f.triggered) {
    trEl.textContent = "⚠ DISPARADA!";
    trEl.className   = "badge trig-yes";
    card.classList.add("alarme");
    startBeepLoop();
  } else {
    trEl.textContent = "Normal";
    trEl.className   = "badge trig-no";
    card.classList.remove("alarme");
    if (!anyTriggered()) stopBeepLoop();
  }
}

function applyOffline(n) {
  const enEl = document.getElementById("s" + n + "-enabled");
  const trEl = document.getElementById("s" + n + "-trig");
  const btn  = document.getElementById("btn" + n);
  const card = document.getElementById("card" + n);

  card.classList.remove("alarme");
  enEl.textContent = "Offline";
  enEl.className   = "badge offline";
  trEl.textContent = "--";
  trEl.className   = "badge trig-no";
  btn.textContent  = "Indisponível";
  btn.className    = "btn btn-disable";
  btn.disabled     = true;
}

async function pollFence(n) {
  try {
    const r = await fetchWithTimeout("/status/" + n, {headers:{"X-API-Key": KEY}});
    if (!r.ok) { applyOffline(n); return; }
    const f = await r.json();
    lastKnown[n] = f;
    applyState(n, f);
  } catch(e) {
    applyOffline(n);
  }
}

async function poll() {
  await Promise.allSettled(FENCES.map(n => pollFence(n)));
}

async function toggle(n) {
  const cur = lastKnown[n];
  if (!cur) return;
  const btn = document.getElementById("btn" + n);
  btn.disabled = true;
  try {
    await fetchWithTimeout("/fence/" + n, {
      method: "POST",
      headers: {"Content-Type":"application/json","X-API-Key": KEY},
      body: JSON.stringify({enabled: !cur.enabled})
    });
    pollFence(n);
  } catch(e) { applyOffline(n); }
}

poll();
setInterval(poll, 2000);
</script>
</body>
</html>)html";

// ── Helpers ───────────────────────────────────────────────────────────────────
static bool checkApiKey() {
    if (server.header("X-API-Key") != API_KEY) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return false;
    }
    return true;
}

static void applyRelays() {
    board.relay1(fence[0].enabled);
    board.relay2(fence[1].enabled);    
}

// ── Chamadas HTTP síncronas à API Java (cercas 3 e 4) ────────────────────────

// Busca {enabled, triggered} da API Java e atualiza fence[idx].enabled.
// Retorna false se a chamada falhar.
static bool extFetchStatus(int idx) {
    char url[80];
    snprintf(url, sizeof(url), EXT_STATUS_URL, idx + 1);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(EXT_HTTP_TIMEOUT_MS);
    int code = http.GET();

    if (code != 200) {
        http.end();
        Serial.printf("[EXT] /status?id=%d → %d\n", idx + 1, code);
        return false;
    }

    String payload = http.getString();
    http.end();

    // Extrai "enabled" e "triggered" do JSON sem biblioteca extra
    fence[idx].enabled   = payload.indexOf("\"enabled\":true")   >= 0;
    fence[idx].triggered = payload.indexOf("\"triggered\":true") >= 0;
    return true;
}

// Chama /ligardesligar/{id} na API Java (GET, sem body).
// Retorna false se falhar.
static bool extToggle(int idx) {
    char url[64];
    snprintf(url, sizeof(url), EXT_TOGGLE_URL, idx + 1);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(EXT_HTTP_TIMEOUT_MS);
    int code = http.GET();
    http.end();

    Serial.printf("[EXT] /ligardesligar/%d → %d\n", idx + 1, code);
    return code == 200;
}

// ── Rotas HTTP ────────────────────────────────────────────────────────────────
static void handleRoot() {
    server.send_P(200, "text/html", HTML);
}

// Cercas 1 e 2: estado local
static void handleLocalStatus(int idx) {
    if (!checkApiKey()) return;
    String json = "{\"enabled\":";
    json += fence[idx].enabled   ? "true" : "false";
    json += ",\"triggered\":";
    json += fence[idx].triggered ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
}

// Cercas 3 e 4: proxy para API Java; triggered vem do estado local
static void handleExtStatus(int idx) {
    if (!checkApiKey()) return;

    if (WiFi.status() != WL_CONNECTED || !extFetchStatus(idx)) {
        server.send(503, "application/json", "{\"error\":\"api_unavailable\"}");
        return;
    }

    // Atualiza relé para refletir o estado vindo da API Java
    applyRelays();

    String json = "{\"enabled\":";
    json += fence[idx].enabled   ? "true" : "false";
    json += ",\"triggered\":";
    json += fence[idx].triggered ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
}

// Cercas 1 e 2: controle local do relé
static void handleLocalFenceControl(int idx) {
    if (!checkApiKey()) return;
    String body   = server.arg("plain");
    bool   enable = body.indexOf("true") >= 0;
    fence[idx].enabled = enable;
    applyRelays();
    Serial.printf("[CERCA %d] %s\n", idx + 1, enable ? "ATIVADA" : "DESATIVADA");
    server.send(200, "application/json", "{\"ok\":true}");
}

// Cercas 3 e 4: delega para API Java (/ligardesligar/{id}) e ressincroniza relay
static void handleExtFenceControl(int idx) {
    if (!checkApiKey()) return;

    if (WiFi.status() != WL_CONNECTED || !extToggle(idx)) {
        server.send(503, "application/json", "{\"error\":\"api_unavailable\"}");
        return;
    }

    // Ressincroniza estado local com a API Java após o toggle
    extFetchStatus(idx);
    applyRelays();

    server.send(200, "application/json", "{\"ok\":true}");
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
static void wifiBeginConnect() {
    Serial.printf("Conectando ao WiFi \"%s\"...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.config(
        IPAddress(192, 168, 0, 47),
        IPAddress(192, 168, 0, 1),
        IPAddress(255, 255, 255, 0),
        IPAddress(8, 8, 8, 8)
    );
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    _wifiRetryAt = millis() + WIFI_RETRY_INTERVAL_MS;
}

static void handleWiFi() {
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !_wifiWasConnected) {
        Serial.printf("Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
        server.begin();
        Serial.println("Servidor HTTP iniciado na porta 80");
    }
    _wifiWasConnected = connected;
    if (!connected && millis() >= _wifiRetryAt) {
        WiFi.disconnect(true);
        wifiBeginConnect();
    }
}

// ── Leitura de entradas por índice ────────────────────────────────────────────
static bool readInput(int idx) {
    switch (idx) {
        case 0: return board.input1();
        case 1: return board.input2();
        //case 2: return board.input3();
        //case 3: return board.input4();
        default: return false;
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);

    board.begin();
    applyRelays();

    wifiBeginConnect();
    while (WiFi.status() != WL_CONNECTED && millis() < _wifiRetryAt) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi não disponível no boot, tentando em background...");
    }

    const char* headers[] = {"X-API-Key"};
    server.collectHeaders(headers, 1);

    server.on("/",          HTTP_GET,  handleRoot);
    // Cercas 1 e 2: estado gerenciado localmente no ESP32
    server.on("/status/1",  HTTP_GET,  []() { handleLocalStatus(0); });
    server.on("/status/2",  HTTP_GET,  []() { handleLocalStatus(1); });
    server.on("/fence/1",   HTTP_POST, []() { handleLocalFenceControl(0); });
    server.on("/fence/2",   HTTP_POST, []() { handleLocalFenceControl(1); });
    // Cercas 3 e 4: estado gerenciado pela API Java
    server.on("/status/3",  HTTP_GET,  []() { handleExtStatus(2); });
    server.on("/status/4",  HTTP_GET,  []() { handleExtStatus(3); });
    server.on("/fence/3",   HTTP_POST, []() { handleExtFenceControl(2); });
    server.on("/fence/4",   HTTP_POST, []() { handleExtFenceControl(3); });
    //server.on("/status/3",  HTTP_GET,  []() { handleLocalStatus(2); });
    //server.on("/status/4",  HTTP_GET,  []() { handleLocalStatus(3); });
    //server.on("/fence/3",   HTTP_POST, []() { handleLocalFenceControl(2); });
    //server.on("/fence/4",   HTTP_POST, []() { handleLocalFenceControl(3); });
    server.onNotFound([]() {
        server.send(404, "application/json", "{\"error\":\"not found\"}");
    });

    if (WiFi.status() == WL_CONNECTED) {
        server.begin();
        Serial.println("Servidor HTTP iniciado na porta 80");
    }

    board.relay3(false);
    board.relay4(false);

    Serial.println("Pronto! Aguardando eventos das cercas...");
}

// ── Loop principal ────────────────────────────────────────────────────────────
void loop() {
    for (int i = 0; i < NUM_FENCES; i++) {
        bool input = readInput(i);

        if (input && !_lastInput[i]) {
            fence[i].triggered = true;
            Serial.printf("[CERCA %d] DISPARADA!\n", i + 1);
        }

        if (!input && _lastInput[i]) {
            fence[i].triggered = false;
        }

        _lastInput[i] = input;
    }

    handleWiFi();
    server.handleClient();

    delay(50);
}

/*

  <div class="card" id="card3">
    <h2>&#x1F6E1; Cerca 3</h2>
    <div class="row"><span class="label">Status</span><span id="s3-enabled" class="badge">--</span></div>
    <div class="row"><span class="label">Disparo</span><span id="s3-trig" class="badge">--</span></div>
    <button id="btn3" class="btn" onclick="toggle(3)">--</button>
  </div>
  <div class="card" id="card4">
    <h2>&#x1F6E1; Cerca 4</h2>
    <div class="row"><span class="label">Status</span><span id="s4-enabled" class="badge">--</span></div>
    <div class="row"><span class="label">Disparo</span><span id="s4-trig" class="badge">--</span></div>
    <button id="btn4" class="btn" onclick="toggle(4)">--</button>
  </div>

*/
