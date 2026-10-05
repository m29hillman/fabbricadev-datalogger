#include "web_app.h"

#include "config.h"
#include "inverter_data.h"
#include "inverter_model.h"
#include "logger.h"
#include "mqtt_pub.h"
#include "net_wifi.h"
#include "settings.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

extern const uint8_t kLogoClaroStart[] asm("_binary_src_img_logo_fundo_claro_png_start");
extern const uint8_t kLogoClaroEnd[] asm("_binary_src_img_logo_fundo_claro_png_end");
extern const uint8_t kLogoEscuroStart[] asm("_binary_src_img_logo_fundo_escuro_png_start");
extern const uint8_t kLogoEscuroEnd[] asm("_binary_src_img_logo_fundo_escuro_png_end");

namespace {

DNSServer g_dns;
bool g_dns_on = false;
WebServer g_server(WEB_PORT);
char g_token[33];
constexpr uint8_t kLoginMaxFails = 5;
constexpr uint32_t kLoginLockMs = 5UL * 60UL * 1000UL;
uint8_t g_login_fails = 0;
uint32_t g_login_lock_until = 0;

const char PAGE[] PROGMEM = R"PAGE(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fabbrica Dev Datalogger</title>
<style>
:root { color-scheme: dark; --bg:#12110f; --ink:#f4efe6; --card:#1e1c19; --line:#3c362f; --field:#2a2622; --green:#2f9d6e; --green-2:#17352a; --warn:#e7c39a; --warn-bg:#3a2c1c; --muted:#b7ada2; --sec:#322e29; --err:#ff8d98; }
html[data-tema="claro"] { color-scheme: light; --bg:#f3efe6; --ink:#1c1915; --card:#fffdf8; --line:#e2d9cc; --field:#fff; --green:#0c6b4d; --green-2:#e5f4ee; --warn:#8a4b12; --warn-bg:#fff1e0; --muted:#6d645b; --sec:#efeae2; --err:#9b2330; }
* { box-sizing: border-box; }
body { margin:0; font:16px/1.45 "Segoe UI", system-ui, sans-serif; background:var(--bg); color:var(--ink); }
main { max-width:760px; margin:0 auto; padding:20px 16px 48px; }
h1 { font-size:1.35rem; margin:0 0 4px; }
.logos { line-height:0; margin:0 0 10px; }
.logos img { height:58px; width:auto; }
.logos .claro { display:none; }
html[data-tema="claro"] .logos .escuro { display:none; }
html[data-tema="claro"] .logos .claro { display:block; }
.sub { color:var(--muted); margin:0 0 16px; }
.card { background:var(--card); border:1px solid var(--line); border-radius:14px; padding:16px; margin:0 0 14px; }
label { display:block; font-size:.92rem; margin:12px 0 4px; }
input, select, button { font:inherit; }
input[type=text], input[type=password], input:not([type]), select { width:100%; padding:10px 12px; border:1px solid var(--line); border-radius:10px; background:var(--field); color:var(--ink); }
input::placeholder { color:var(--muted); }
.row { display:flex; gap:8px; align-items:center; }
.row input { flex:1; }
button { border:0; border-radius:10px; padding:11px 14px; background:var(--green); color:#fff; cursor:pointer; }
button:disabled, input:disabled { opacity:.55; cursor:default; }
button.sec { background:var(--sec); color:var(--ink); }
button.perigo { background:#8f2d2d; color:#fff; }
nav { display:flex; gap:6px; flex-wrap:wrap; margin:0 0 14px; }
nav button { background:transparent; color:var(--ink); border:1px solid transparent; }
nav button.on { background:var(--card); border-color:var(--line); }
.hero { font-size:2.5rem; font-weight:650; letter-spacing:-.03em; }
.pill { display:inline-block; padding:2px 10px; border-radius:999px; background:var(--green-2); color:var(--green); font-weight:600; }
.grid { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
.grid b { display:block; font-size:1.25rem; margin-top:2px; }
.muted { color:var(--muted); font-size:.92rem; }
.warn { background:var(--warn-bg); color:var(--warn); padding:10px 12px; border-radius:10px; }
.err { color:var(--err); min-height:1.2em; }
.checks { display:flex; flex-direction:column; gap:8px; margin:12px 0; }
.fixo { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
@media (max-width:560px) { .grid, .fixo { grid-template-columns:1fr; } }
</style>
</head>
<body>
<main>
<section id="login" class="card">
<div class="row" style="justify-content:space-between;align-items:flex-start">
<div>
<div class="logos">
<img class="escuro" src="/logo-escuro.png" alt="Fabbrica">
<img class="claro" src="/logo-claro.png" alt="Fabbrica">
</div>
<h1>Fabbrica Dev Datalogger</h1>
</div>
<button class="sec" type="button" data-tema-btn>Claro</button>
</div>
<p class="sub">Entre para configurar a placa. No primeiro acesso, usuário e senha são admin.</p>
<form id="flogin">
<label for="luser">Usuário</label>
<input id="luser" name="user" type="text" autocomplete="username" value="admin">
<label for="lsenha">Senha</label>
<input id="lsenha" name="senha" type="password" autocomplete="current-password">
<p class="err" id="lerr"></p>
<button type="submit">Entrar</button>
</form>
</section>
<div id="app" hidden>
<header class="card">
<div class="row" style="justify-content:space-between">
<div>
<div class="logos">
<img class="escuro" src="/logo-escuro.png" alt="Fabbrica">
<img class="claro" src="/logo-claro.png" alt="Fabbrica">
</div>
<h1>Fabbrica Dev Datalogger</h1>
<p class="sub" id="topo" style="margin:0">Fabbrica Dev Datalogger · http://192.168.4.1</p>
</div>
<div class="row">
<button class="sec" type="button" data-tema-btn>Claro</button>
<button class="sec" id="sair" type="button">Sair</button>
</div>
</div>
<p class="warn" id="aviso" hidden>A senha ainda é admin. Troque na aba Usuário.</p>
</header>
<nav>
<button type="button" data-aba="estatistica" class="on">Estatística</button>
<button type="button" data-aba="inversor">Inversor</button>
<button type="button" data-aba="rede">Rede</button>
<button type="button" data-aba="mqtt">MQTT</button>
<button type="button" data-aba="usuario">Usuário</button>
</nav>
<section id="estatistica">
<div class="card">
<div class="muted" id="estado">Aguardando o inversor</div>
<div class="hero" id="potencia">—</div>
<p class="muted" id="idade"></p>
<p class="muted" id="serie"></p>
<p class="muted" id="modelo"></p>
</div>
<div class="grid" id="grades"></div>
</section>
<section id="inversor" hidden>
<form id="fmodelo" class="card">
<label for="modelo_sel">Modelo</label>
<select id="modelo_sel"></select>
<p class="muted">A leitura RS485 e o JSON usam o mapa deste modelo.</p>
<p class="err" id="ierr"></p>
<button type="submit">Salvar modelo</button>
</form>
</section>
<section id="rede" hidden>
<form id="frede" class="card">
<label for="ssid">SSID</label>
<div class="row">
<input id="ssid" maxlength="32" autocomplete="off">
<button class="sec" id="buscar" type="button">Buscar</button>
</div>
<select id="lista" hidden></select>
<label for="wpass">Senha do Wi-Fi</label>
<input id="wpass" type="password" autocomplete="new-password" placeholder="Em branco mantém a senha atual">
<div class="checks">
<label><input type="radio" name="ipmodo" value="dhcp" checked> IP dinâmico</label>
<label><input type="radio" name="ipmodo" value="fixo"> IP fixo</label>
</div>
<div class="fixo" id="fixo" style="display:none">
<input id="ip" placeholder="IP">
<input id="mask" placeholder="Máscara">
<input id="gw" placeholder="Gateway">
<input id="dns" placeholder="DNS">
</div>
<p class="muted" id="redeinfo"></p>
<p class="err" id="rerr"></p>
<button type="submit">Salvar rede</button>
</form>
</section>
<section id="mqtt" hidden>
<form id="fmqtt" class="card">
<label><input type="checkbox" id="mqtt_on"> Habilitar envio</label>
<label for="mqtt_url">URL do broker</label>
<input id="mqtt_url" type="text" placeholder="mqtt://usuario:senha@192.168.1.10:1883/esp32-casa-1/inversor">
<p class="muted">O caminho depois da porta é o tópico. Sem caminho, o tópico é esp32-casa-1/inversor. A cada 10 s a placa publica o JSON do inversor.</p>
<p class="muted" id="mqttinfo"></p>
<p class="err" id="merr"></p>
<button type="submit">Salvar MQTT</button>
</form>
</section>
<section id="usuario" hidden>
<form id="fuser" class="card">
<p>Usuário: <b id="quem">admin</b></p>
<label for="atual">Senha atual</label>
<input id="atual" type="password" autocomplete="current-password">
<label for="nova">Nova senha</label>
<input id="nova" type="password" autocomplete="new-password">
<label for="nova2">Confirmar nova senha</label>
<input id="nova2" type="password" autocomplete="new-password">
<p class="err" id="uerr"></p>
<button type="submit">Trocar senha</button>
</form>
<div class="card">
<p>Restaura o usuário e a senha para admin, apaga a rede e o MQTT, e volta o modelo para Sungrow SG6K-D.</p>
<button class="perigo" type="button" id="resetabrir">Restaurar padrão</button>
<form id="freset" hidden>
<label for="resetsenha">Senha atual</label>
<input id="resetsenha" type="password" autocomplete="current-password">
<p class="muted">Digite a senha deste usuário para confirmar.</p>
<p class="err" id="rseterr"></p>
<p class="muted" id="resetok" hidden></p>
<div class="row">
<button class="perigo" type="submit">Confirmar reset</button>
<button class="sec" type="button" id="resetcancelar">Cancelar</button>
</div>
</form>
</div>
</section>
</div>
</main>
<script>
function aplicarTema(claro) {
  document.documentElement.dataset.tema = claro ? 'claro' : 'escuro';
  document.querySelectorAll('[data-tema-btn]').forEach(b => { b.textContent = claro ? 'Escuro' : 'Claro'; });
}
aplicarTema(localStorage.getItem('tema') === 'claro');
document.querySelectorAll('[data-tema-btn]').forEach(b => b.onclick = () => {
  const claro = document.documentElement.dataset.tema !== 'claro';
  localStorage.setItem('tema', claro ? 'claro' : 'escuro');
  aplicarTema(claro);
});
const CAMPOS = [
  ['tensao_v','Tensão','V'],
  ['corrente_a','Corrente','A'],
  ['frequencia_hz','Frequência','Hz'],
  ['fator_potencia','Fator de potência',''],
  ['energia_dia_kwh','Energia do dia','kWh'],
  ['energia_total_kwh','Energia total','kWh'],
  ['temperatura_c','Temperatura','°C'],
  ['potencia_cc_w','Potência CC','W'],
  ['mppt1_tensao_v','MPPT 1 tensão','V'],
  ['mppt1_corrente_a','MPPT 1 corrente','A'],
  ['mppt2_tensao_v','MPPT 2 tensão','V'],
  ['mppt2_corrente_a','MPPT 2 corrente','A'],
  ['potencia_nominal_kw','Potência nominal','kW'],
  ['tipo','Código do tipo','']
];
function aba(id) {
  document.querySelectorAll('nav button').forEach(b => b.classList.toggle('on', b.dataset.aba === id));
  ['estatistica','inversor','rede','mqtt','usuario'].forEach(n => {
    document.getElementById(n).hidden = n !== id;
  });
  if (id === 'inversor') carregarModelo();
  if (id === 'rede') carregarRede();
  if (id === 'mqtt') carregarMqtt();
}
document.querySelectorAll('nav button').forEach(b => b.onclick = () => aba(b.dataset.aba));
async function post(url, dados) {
  const r = await fetch(url, {method:'POST', body:new URLSearchParams(dados)});
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.erro || 'falha');
  return j;
}
function txt(v, suf) {
  if (v === null || v === undefined) return '—';
  return suf ? v + ' ' + suf : String(v);
}
async function carregarStats() {
  const r = await fetch('/api/stats');
  if (r.status === 401) { location.reload(); return; }
  const j = await r.json();
  const m = j.medicao || {};
  document.getElementById('aviso').hidden = !j.senha_padrao;
  document.getElementById('estado').textContent = m.estado || 'Aguardando o inversor';
  document.getElementById('estado').className = m.estado ? 'pill' : 'muted';
  document.getElementById('potencia').textContent = txt(m.potencia_w, 'W');
  document.getElementById('serie').textContent = m.serie ? 'Série ' + m.serie : '';
  document.getElementById('modelo').textContent = m.modelo || '';
  document.getElementById('idade').textContent = j.tem_dados ? 'Atualizado há ' + j.idade_s + ' s' : '';
  const rede = j.rede && j.rede.conectado ? 'Casa ' + j.rede.ip : 'Casa desconectada';
  const mqtt = j.mqtt_conectado ? 'MQTT conectado' : 'MQTT parado';
  document.getElementById('topo').textContent = rede + ' · ' + mqtt + (j.ap ? ' · config em 192.168.4.1' : '');
  document.getElementById('grades').innerHTML = CAMPOS.map(c =>
    '<div class="card"><span class="muted">' + c[1] + '</span><b>' + txt(m[c[0]], c[2]) + '</b></div>'
  ).join('');
}
function escHtml(s) {
  return String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
}
async function carregarModelo() {
  const r = await fetch('/api/modelo');
  const j = await r.json();
  const sel = document.getElementById('modelo_sel');
  sel.innerHTML = (j.modelos || []).map(m => '<option value="' + escHtml(m.id) + '">' + escHtml(m.nome) + '</option>').join('');
  if (j.atual) sel.value = j.atual;
}
async function carregarRede() {
  const r = await fetch('/api/rede');
  const j = await r.json();
  document.getElementById('ssid').value = j.ssid || '';
  document.getElementById('ip').value = j.ip || '';
  document.getElementById('mask').value = j.mask || '';
  document.getElementById('gw').value = j.gw || '';
  document.getElementById('dns').value = j.dns || '';
  document.querySelector('input[name=ipmodo][value=' + (j.dhcp ? 'dhcp' : 'fixo') + ']').checked = true;
  document.getElementById('fixo').style.display = j.dhcp ? 'none' : 'grid';
  document.getElementById('redeinfo').textContent = j.conectado ? 'Conectado em ' + j.ip_sta + ' (' + j.rssi + ' dBm)' : 'Ainda não entrou na rede da casa';
}
async function carregarMqtt() {
  const r = await fetch('/api/mqtt');
  const j = await r.json();
  document.getElementById('mqtt_on').checked = !!j.habilitado;
  document.getElementById('mqtt_url').value = j.url || '';
  document.getElementById('mqttinfo').textContent = (j.conectado ? 'Conectado' : 'Desconectado') + ' · topico ' + (j.topico || '');
}
document.querySelectorAll('input[name=ipmodo]').forEach(el => {
  el.onchange = () => { document.getElementById('fixo').style.display = el.value === 'fixo' && el.checked ? 'grid' : 'none'; };
});
let bloqueio = 0;
function travarLogin(seg) {
  const form = document.getElementById('flogin');
  const fim = Date.now() + seg * 1000;
  const tick = () => {
    const resta = Math.max(0, Math.ceil((fim - Date.now()) / 1000));
    const campos = form.querySelectorAll('input, button');
    if (resta <= 0) {
      clearInterval(bloqueio);
      bloqueio = 0;
      campos.forEach(el => { el.disabled = false; });
      document.getElementById('lerr').textContent = '';
      return;
    }
    campos.forEach(el => { el.disabled = true; });
    const m = Math.floor(resta / 60);
    const s = String(resta % 60).padStart(2, '0');
    document.getElementById('lerr').textContent = 'Acesso bloqueado. Tente novamente em ' + m + ':' + s;
  };
  if (bloqueio) clearInterval(bloqueio);
  tick();
  bloqueio = setInterval(tick, 1000);
}
document.getElementById('flogin').onsubmit = async (ev) => {
  ev.preventDefault();
  if (bloqueio) return;
  document.getElementById('lerr').textContent = '';
  try {
    const r = await fetch('/api/login', {method:'POST', body:new URLSearchParams({
      user:document.getElementById('luser').value,
      senha:document.getElementById('lsenha').value
    })});
    const j = await r.json().catch(() => ({}));
    if (!r.ok) {
      if (j.espera) travarLogin(j.espera);
      else document.getElementById('lerr').textContent = j.erro || 'Usuário ou senha incorretos';
      return;
    }
    document.getElementById('login').hidden = true;
    document.getElementById('app').hidden = false;
    iniciar();
  } catch (e) {
    document.getElementById('lerr').textContent = 'Usuário ou senha incorretos';
  }
};
document.getElementById('frede').onsubmit = async (ev) => {
  ev.preventDefault();
  document.getElementById('rerr').textContent = '';
  const modo = document.querySelector('input[name=ipmodo]:checked').value;
  try {
    await post('/api/rede', {
      ssid:document.getElementById('ssid').value,
      wpass:document.getElementById('wpass').value,
      dhcp: modo === 'dhcp' ? '1' : '0',
      ip:document.getElementById('ip').value,
      mask:document.getElementById('mask').value,
      gw:document.getElementById('gw').value,
      dns:document.getElementById('dns').value
    });
    document.getElementById('wpass').value = '';
    document.getElementById('rerr').textContent = 'Rede salva. A placa está entrando nela.';
    setTimeout(carregarRede, 4000);
  } catch (e) { document.getElementById('rerr').textContent = e.message; }
};
document.getElementById('fmqtt').onsubmit = async (ev) => {
  ev.preventDefault();
  document.getElementById('merr').textContent = '';
  try {
    await post('/api/mqtt', {
      habilitado: document.getElementById('mqtt_on').checked ? '1' : '0',
      url: document.getElementById('mqtt_url').value
    });
    document.getElementById('merr').textContent = 'MQTT salvo.';
    setTimeout(carregarMqtt, 2000);
  } catch (e) { document.getElementById('merr').textContent = e.message; }
};
document.getElementById('fmodelo').onsubmit = async (ev) => {
  ev.preventDefault();
  document.getElementById('ierr').textContent = '';
  try {
    await post('/api/modelo', {modelo: document.getElementById('modelo_sel').value});
    document.getElementById('ierr').textContent = 'Modelo salvo.';
    carregarStats();
  } catch (e) { document.getElementById('ierr').textContent = e.message; }
};
document.getElementById('fuser').onsubmit = async (ev) => {
  ev.preventDefault();
  document.getElementById('uerr').textContent = '';
  const nova = document.getElementById('nova').value;
  if (nova !== document.getElementById('nova2').value) {
    document.getElementById('uerr').textContent = 'A confirmação não confere';
    return;
  }
  try {
    await post('/api/senha', {atual:document.getElementById('atual').value, nova});
    document.getElementById('atual').value = '';
    document.getElementById('nova').value = '';
    document.getElementById('nova2').value = '';
    document.getElementById('uerr').textContent = 'Senha alterada.';
    document.getElementById('aviso').hidden = true;
  } catch (e) { document.getElementById('uerr').textContent = e.message; }
};
document.getElementById('resetabrir').onclick = () => {
  document.getElementById('resetabrir').hidden = true;
  document.getElementById('freset').hidden = false;
  document.getElementById('resetsenha').focus();
};
document.getElementById('resetcancelar').onclick = () => {
  document.getElementById('freset').hidden = true;
  document.getElementById('resetabrir').hidden = false;
  document.getElementById('resetsenha').value = '';
  document.getElementById('rseterr').textContent = '';
};
document.getElementById('freset').onsubmit = async (ev) => {
  ev.preventDefault();
  document.getElementById('rseterr').textContent = '';
  document.getElementById('resetok').hidden = true;
  try {
    await post('/api/reset', {senha: document.getElementById('resetsenha').value});
    if (pulso) { clearInterval(pulso); pulso = 0; }
    document.getElementById('resetsenha').value = '';
    document.getElementById('freset').querySelectorAll('input, button').forEach(el => { el.disabled = true; });
    const ok = document.getElementById('resetok');
    ok.hidden = false;
    ok.textContent = 'Configuração restaurada. Usuário e senha: admin. A placa abre de novo a rede fabbricadev-datalogger.';
  } catch (e) { document.getElementById('rseterr').textContent = e.message; }
};
document.getElementById('buscar').onclick = async () => {
  const lista = document.getElementById('lista');
  lista.hidden = false;
  lista.innerHTML = '<option>Buscando...</option>';
  for (let i = 0; i < 12; i++) {
    const r = await fetch('/api/scan');
    const j = await r.json();
    if (j.redes) {
      lista.innerHTML = '<option value="">Escolha uma rede</option>' + j.redes.map(n => '<option>' + n.replace(/[&<>"]/g,'') + '</option>').join('');
      lista.onchange = () => { if (lista.value) document.getElementById('ssid').value = lista.value; };
      return;
    }
    await new Promise(ok => setTimeout(ok, 1000));
  }
  lista.innerHTML = '<option>Nenhuma rede encontrada</option>';
};
document.getElementById('sair').onclick = async () => { await fetch('/api/sair', {method:'POST'}); location.reload(); };
let pulso = 0;
function iniciar() {
  document.getElementById('login').hidden = true;
  document.getElementById('app').hidden = false;
  fetch('/api/sessao').then(r => r.json()).then(j => { document.getElementById('quem').textContent = j.usuario || 'admin'; });
  carregarStats();
  if (!pulso) pulso = setInterval(carregarStats, 3000);
}
fetch('/api/sessao').then(r => { if (r.ok) iniciar(); });
fetch('/api/login').then(r => r.json()).then(j => { if (j.espera) travarLogin(j.espera); }).catch(() => {});
</script>
</body>
</html>)PAGE";

bool logged_in() {
  if (g_token[0] == '\0' || !g_server.hasHeader("Cookie")) {
    return false;
  }
  String cookie = g_server.header("Cookie");
  String needle = String("casa=") + g_token;
  return cookie.indexOf(needle) >= 0;
}

void send_json(int code, const char *body) {
  g_server.send(code, "application/json; charset=utf-8", body);
}

void json_escape(const char *src, char *dst, size_t n) {
  size_t j = 0;
  if (src == nullptr) {
    src = "";
  }
  for (size_t i = 0; src[i] != '\0' && j + 2 < n; ++i) {
    if (src[i] == '"' || src[i] == '\\') {
      dst[j++] = '\\';
    }
    dst[j++] = src[i];
  }
  dst[j] = '\0';
}

bool require_auth() {
  if (logged_in()) {
    return true;
  }
  send_json(401, "{\"erro\":\"login\"}");
  return false;
}

String field(const char *name) {
  String value = g_server.arg(name);
  value.trim();
  return value;
}

void handle_root() { g_server.send_P(200, "text/html; charset=utf-8", PAGE); }

void send_png(const uint8_t *start, const uint8_t *end) {
  g_server.sendHeader("Cache-Control", "public, max-age=86400");
  g_server.send_P(200, "image/png", reinterpret_cast<PGM_P>(start), static_cast<size_t>(end - start));
}

void handle_logo_claro() { send_png(kLogoClaroStart, kLogoClaroEnd); }

void handle_logo_escuro() { send_png(kLogoEscuroStart, kLogoEscuroEnd); }

uint32_t login_wait_s() {
  if (g_login_lock_until == 0) {
    return 0;
  }
  int32_t left = static_cast<int32_t>(g_login_lock_until - millis());
  if (left <= 0) {
    g_login_lock_until = 0;
    g_login_fails = 0;
    return 0;
  }
  return (static_cast<uint32_t>(left) + 999U) / 1000U;
}

void send_login_wait(uint32_t wait_s) {
  char body[64];
  snprintf(body, sizeof(body), "{\"erro\":\"Acesso bloqueado\",\"espera\":%lu}",
           static_cast<unsigned long>(wait_s));
  send_json(429, body);
}

void deny_login() {
  g_login_fails++;
  if (g_login_fails >= kLoginMaxFails) {
    g_login_fails = 0;
    g_login_lock_until = millis() + kLoginLockMs;
    logger_printf("login bloqueado por 5 minutos\r\n");
    send_login_wait(kLoginLockMs / 1000U);
    return;
  }
  unsigned restantes = kLoginMaxFails - g_login_fails;
  logger_printf("login recusado, restam %u tentativas\r\n", restantes);
  char body[160];
  snprintf(body, sizeof(body), "{\"erro\":\"Usuário ou senha incorretos. %s %u %s\",\"restam\":%u}",
           restantes == 1 ? "Resta" : "Restam", restantes, restantes == 1 ? "tentativa" : "tentativas",
           restantes);
  send_json(401, body);
}

void handle_login_status() {
  char body[40];
  snprintf(body, sizeof(body), "{\"espera\":%lu}", static_cast<unsigned long>(login_wait_s()));
  send_json(200, body);
}

void handle_login() {
  uint32_t wait = login_wait_s();
  if (wait > 0) {
    send_login_wait(wait);
    return;
  }
  String user = field("user");
  String pass = g_server.arg("senha");
  if (!settings_check_login(user.c_str(), pass.c_str())) {
    deny_login();
    return;
  }
  g_login_fails = 0;
  snprintf(g_token, sizeof(g_token), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  String cookie = String("casa=") + g_token + "; Path=/; HttpOnly";
  g_server.sendHeader("Set-Cookie", cookie);
  send_json(200, "{\"ok\":true}");
}

void handle_logout() {
  g_token[0] = '\0';
  g_server.sendHeader("Set-Cookie", "casa=; Path=/; Max-Age=0");
  send_json(200, "{\"ok\":true}");
}

void handle_session() {
  if (!require_auth()) {
    return;
  }
  const Settings &cfg = settings_get();
  char user[64];
  json_escape(cfg.user, user, sizeof(user));
  char body[128];
  snprintf(body, sizeof(body), "{\"usuario\":\"%s\",\"senha_padrao\":%s}", user,
           settings_is_default_password() ? "true" : "false");
  send_json(200, body);
}

void handle_stats() {
  if (!require_auth()) {
    return;
  }
  static char med[1100];
  static char body[1500];
  if (inverter_format_json(med, sizeof(med)) == 0) {
    strcpy(med, "null");
  }
  uint32_t age = inverter_data_age_ms();
  char age_text[16];
  if (age == UINT32_MAX) {
    strcpy(age_text, "null");
  } else {
    snprintf(age_text, sizeof(age_text), "%lu", static_cast<unsigned long>(age / 1000));
  }
  char ip[48];
  json_escape(net_sta_ip().c_str(), ip, sizeof(ip));
  snprintf(body, sizeof(body),
           "{\"medicao\":%s,\"idade_s\":%s,\"tem_dados\":%s,\"senha_padrao\":%s,"
           "\"mqtt_conectado\":%s,\"ap\":%s,\"rede\":{\"conectado\":%s,\"ip\":\"%s\",\"rssi\":%d}}",
           med, age_text, inverter_data_ready() ? "true" : "false",
           settings_is_default_password() ? "true" : "false", mqtt_connected() ? "true" : "false",
           net_ap_up() ? "true" : "false", net_sta_connected() ? "true" : "false", ip, net_rssi());
  send_json(200, body);
}

void handle_net_get() {
  if (!require_auth()) {
    return;
  }
  const Settings &cfg = settings_get();
  char ssid[80];
  char ip[24];
  char mask[24];
  char gw[24];
  char dns[24];
  char sta[48];
  json_escape(cfg.ssid, ssid, sizeof(ssid));
  json_escape(cfg.ip, ip, sizeof(ip));
  json_escape(cfg.mask, mask, sizeof(mask));
  json_escape(cfg.gw, gw, sizeof(gw));
  json_escape(cfg.dns, dns, sizeof(dns));
  json_escape(net_sta_ip().c_str(), sta, sizeof(sta));
  char body[420];
  snprintf(body, sizeof(body),
           "{\"ssid\":\"%s\",\"dhcp\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\",\"dns\":\"%s\","
           "\"conectado\":%s,\"ip_sta\":\"%s\",\"rssi\":%d,\"ap\":\"%s\"}",
           ssid, cfg.dhcp ? "true" : "false", ip, mask, gw, dns,
           net_sta_connected() ? "true" : "false", sta, net_rssi(), WIFI_AP_IP);
  send_json(200, body);
}

void handle_net_post() {
  if (!require_auth()) {
    return;
  }
  const char *err = settings_save_network(field("ssid").c_str(), g_server.arg("wpass").c_str(),
                                          field("dhcp") != "0", field("ip").c_str(), field("mask").c_str(),
                                          field("gw").c_str(), field("dns").c_str());
  if (err != nullptr) {
    char escaped[80];
    char body[120];
    json_escape(err, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"erro\":\"%s\"}", escaped);
    send_json(400, body);
    return;
  }
  logger_printf("Rede salva ssid %s %s\r\n", settings_get().ssid,
                settings_get().dhcp ? "dinamico" : "fixo");
  send_json(200, "{\"ok\":true}");
  net_request_apply();
}

void handle_mqtt_get() {
  if (!require_auth()) {
    return;
  }
  const Settings &cfg = settings_get();
  char url[360];
  char topic[120];
  json_escape(cfg.mqtt_url, url, sizeof(url));
  json_escape(mqtt_topic(), topic, sizeof(topic));
  char body[560];
  snprintf(body, sizeof(body), "{\"habilitado\":%s,\"url\":\"%s\",\"conectado\":%s,\"topico\":\"%s\"}",
           cfg.mqtt_on ? "true" : "false", url, mqtt_connected() ? "true" : "false", topic);
  send_json(200, body);
}

void handle_mqtt_post() {
  if (!require_auth()) {
    return;
  }
  bool enabled = field("habilitado") == "1";
  String url = field("url");
  if (url.length() > 0 && !mqtt_url_valid(url.c_str())) {
    send_json(400, "{\"erro\":\"URL invalida. Use mqtt://host:1883/topico\"}");
    return;
  }
  const char *err = settings_save_mqtt(enabled, url.c_str());
  if (err != nullptr) {
    char escaped[80];
    char body[120];
    json_escape(err, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"erro\":\"%s\"}", escaped);
    send_json(400, body);
    return;
  }
  logger_printf("MQTT %s\r\n", enabled ? "habilitado" : "desabilitado");
  mqtt_reconfigure();
  send_json(200, "{\"ok\":true}");
}

void handle_model_get() {
  if (!require_auth()) {
    return;
  }
  size_t count = 0;
  const InverterModel *list = inverter_model_list(&count);
  char body[480];
  size_t used = 0;
  int n = snprintf(body, sizeof(body), "{\"atual\":\"%s\",\"modelos\":[", settings_get().modelo);
  if (n < 0) {
    send_json(500, "{\"erro\":\"falha\"}");
    return;
  }
  used = static_cast<size_t>(n);
  for (size_t i = 0; i < count && used + 80 < sizeof(body); ++i) {
    char id[48];
    char nome[64];
    json_escape(list[i].id, id, sizeof(id));
    json_escape(list[i].nome, nome, sizeof(nome));
    n = snprintf(body + used, sizeof(body) - used, "%s{\"id\":\"%s\",\"nome\":\"%s\"}", i == 0 ? "" : ",", id,
                 nome);
    if (n < 0) {
      break;
    }
    used += static_cast<size_t>(n);
  }
  snprintf(body + used, sizeof(body) - used, "]}");
  send_json(200, body);
}

void handle_model_post() {
  if (!require_auth()) {
    return;
  }
  String id = field("modelo");
  bool changed = strcmp(settings_get().modelo, id.c_str()) != 0;
  const char *err = settings_save_model(id.c_str());
  if (err != nullptr) {
    char escaped[80];
    char body[120];
    json_escape(err, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"erro\":\"%s\"}", escaped);
    send_json(400, body);
    return;
  }
  if (changed) {
    inverter_data_clear();
    logger_printf("modelo %s\r\n", inverter_model_name());
  }
  send_json(200, "{\"ok\":true}");
}

void handle_password() {
  if (!require_auth()) {
    return;
  }
  const char *err = settings_change_password(g_server.arg("atual").c_str(), field("nova").c_str());
  if (err != nullptr) {
    char escaped[96];
    char body[140];
    json_escape(err, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"erro\":\"%s\"}", escaped);
    send_json(400, body);
    return;
  }
  logger_printf("Senha da pagina alterada\r\n");
  send_json(200, "{\"ok\":true}");
}

void handle_factory_reset() {
  if (!require_auth()) {
    return;
  }
  const char *err = settings_factory_reset(g_server.arg("senha").c_str());
  if (err != nullptr) {
    char escaped[80];
    char body[120];
    json_escape(err, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"erro\":\"%s\"}", escaped);
    send_json(400, body);
    return;
  }
  g_token[0] = '\0';
  g_login_fails = 0;
  g_login_lock_until = 0;
  g_server.sendHeader("Set-Cookie", "casa=; Path=/; Max-Age=0");
  inverter_data_clear();
  mqtt_reconfigure();
  logger_printf("Configuracao restaurada. Login admin / admin\r\n");
  send_json(200, "{\"ok\":true}");
  net_request_apply();
}

void handle_scan() {
  if (!require_auth()) {
    return;
  }
  int state = WiFi.scanComplete();
  if (state == WIFI_SCAN_RUNNING) {
    send_json(200, "{\"pendente\":true}");
    return;
  }
  if (state < 0) {
    WiFi.scanNetworks(true);
    send_json(200, "{\"pendente\":true}");
    return;
  }
  String body = "{\"redes\":[";
  bool first = true;
  for (int i = 0; i < state && i < 16; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid == "Piemont Office") {
      continue;
    }
    bool dup = false;
    for (int j = 0; j < i; ++j) {
      if (WiFi.SSID(j) == ssid) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    char escaped[80];
    json_escape(ssid.c_str(), escaped, sizeof(escaped));
    if (!first) {
      body += ",";
    }
    first = false;
    body += "\"";
    body += escaped;
    body += "\"";
  }
  body += "]}";
  WiFi.scanDelete();
  send_json(200, body.c_str());
}

bool captive_probe() {
  String uri = g_server.uri();
  return uri == "/generate_204" || uri == "/gen_204" || uri == "/hotspot-detect.html" ||
         uri == "/connecttest.txt" || uri == "/ncsi.txt" || uri == "/fwlink" || uri == "/canonical.html";
}

void handle_not_found() {
  if (captive_probe()) {
    g_server.sendHeader("Location", String("http://") + WIFI_AP_IP + "/");
    g_server.send(302, "text/plain", "");
    return;
  }
  g_server.send(404, "text/plain", "nao encontrado");
}

}  // namespace

void web_begin() {
  const char *headers[] = {"Cookie"};
  g_server.collectHeaders(headers, 1);
  g_server.on("/", HTTP_GET, handle_root);
  g_server.on("/logo-claro.png", HTTP_GET, handle_logo_claro);
  g_server.on("/logo-escuro.png", HTTP_GET, handle_logo_escuro);
  g_server.on("/api/login", HTTP_GET, handle_login_status);
  g_server.on("/api/login", HTTP_POST, handle_login);
  g_server.on("/api/sair", HTTP_POST, handle_logout);
  g_server.on("/api/sessao", HTTP_GET, handle_session);
  g_server.on("/api/stats", HTTP_GET, handle_stats);
  g_server.on("/api/rede", HTTP_GET, handle_net_get);
  g_server.on("/api/rede", HTTP_POST, handle_net_post);
  g_server.on("/api/modelo", HTTP_GET, handle_model_get);
  g_server.on("/api/modelo", HTTP_POST, handle_model_post);
  g_server.on("/api/mqtt", HTTP_GET, handle_mqtt_get);
  g_server.on("/api/mqtt", HTTP_POST, handle_mqtt_post);
  g_server.on("/api/senha", HTTP_POST, handle_password);
  g_server.on("/api/reset", HTTP_POST, handle_factory_reset);
  g_server.on("/api/scan", HTTP_GET, handle_scan);
  g_server.on("/favicon.ico", HTTP_GET, []() { g_server.send(204, "text/plain", ""); });
  g_server.onNotFound(handle_not_found);
  g_server.begin();
  logger_printf("Pagina web na porta %d\r\n", WEB_PORT);
}

void web_service() {
  if (net_ap_up()) {
    if (!g_dns_on) {
      IPAddress ap_ip = WiFi.softAPIP();
      if (ap_ip == IPAddress(0, 0, 0, 0)) {
        ap_ip = IPAddress(192, 168, 4, 1);
      }
      g_dns.start(53, "*", ap_ip);
      g_dns_on = true;
    }
    g_dns.processNextRequest();
  } else if (g_dns_on) {
    g_dns.stop();
    g_dns_on = false;
  }
  g_server.handleClient();
}
