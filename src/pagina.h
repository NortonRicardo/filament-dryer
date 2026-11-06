// ---------------------------------------------------------------------------
// A pagina servida em "/". Fica em PROGMEM: nao ocupa RAM, so flash.
//
// Mesmo desenho do display: mesma paleta, mesmos cartoes, mesma ordem. As
// fontes sao as do sistema de proposito - o celular pode estar sem internet
// e uma fonte de CDN nao carregaria.
//
// Sem framework, sem CDN e sem arquivo externo: um GET entrega tudo.
//
// Convencoes da pagina:
//   - O corpo so mostra; configurar e nos modais. Tocar num cartao com "›"
//     abre a configuracao dele.
//   - Liga/desliga: o texto ao lado diz o estado; o botao diz a ACAO. Ligado
//     mostra a acao de desligar em vermelho; desligado, a de ligar em verde.
//   - Campos salvam sozinhos ao sair deles (evento change). Cada campo manda
//     so a propria chave, nunca o formulario inteiro.
//   - Campos sao texto com teclado numerico, nao type=number: so assim da
//     para por o cursor no fim do valor ao tocar.
//
// Feito para o celular (testado como iPhone 11, 414 px de largura):
//   - campos com fonte de 16 px: abaixo disso o iOS da zoom ao focar;
//   - alvos de toque com 44 px ou mais, a medida minima da Apple;
//   - margens do notch e da barra inferior (env(safe-area-inset-*));
//   - pagina travada atras do modal aberto, para nao rolar junto.
// ---------------------------------------------------------------------------

#pragma once

#include <pgmspace.h>

static const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#080b10">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Estufa">
<title>Estufa de filamento</title>
<style>
:root{
  --bg:#080b10; --card:#111720; --borda:#202b38; --texto:#f3f6fa;
  --mudo:#8290a1; --ciano:#42d7f5; --azul:#6197ff; --laranja:#ff9b52;
  --verde:#55e0a0; --roxo:#a78bfa; --vermelho:#ff6b6b; --rodape:#586575;
  --turquesa:#5eead4;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{-webkit-text-size-adjust:100%}
html.travado,html.travado body{overflow:hidden}
body{margin:0 auto;max-width:440px;background:var(--bg);color:var(--texto);
  font:15px/1.5 -apple-system,system-ui,"Segoe UI",sans-serif;
  padding:calc(12px + env(safe-area-inset-top)) calc(12px + env(safe-area-inset-right))
          calc(24px + env(safe-area-inset-bottom)) calc(12px + env(safe-area-inset-left))}
button,.clicavel,input{touch-action:manipulation}

.grade{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
.cartao{border:1px solid var(--borda);border-radius:16px;background:var(--card);
  padding:16px 10px 14px;text-align:center;min-height:124px;
  display:flex;flex-direction:column;justify-content:center;align-items:center}
.clicavel{cursor:pointer;position:relative;user-select:none;-webkit-user-select:none;
  transition:transform .12s,border-color .12s}
.clicavel:active{transform:scale(.98);border-color:#2d3b4d}
.clicavel::after{content:"›";position:absolute;right:12px;top:50%;
  transform:translateY(-50%);font:300 26px -apple-system,system-ui;color:var(--rodape)}
.rotulo{color:var(--mudo);font:800 9.5px var(--mono);letter-spacing:1.5px;
  text-transform:uppercase;margin-bottom:12px}
.valor{font-size:36px;font-weight:750;line-height:1;letter-spacing:-1.2px;
  font-variant-numeric:tabular-nums;color:var(--mudo)}
.valor u{font-size:15px;font-weight:600;color:var(--mudo);margin-left:3px;
  letter-spacing:0;text-decoration:none}
.detalhe{color:var(--mudo);font:650 9.5px var(--mono);letter-spacing:1px;margin-top:11px}
.detalhe b{color:var(--texto);font-weight:800}
.detalhe.cfg,.detalhe.cfg b{color:var(--verde)}
.vivo .valor{color:var(--cor)}
#c-t{--cor:var(--laranja)} #c-u{--cor:var(--ciano)} #c-ua{--cor:var(--turquesa)}
#c-f1{--cor:var(--azul)} #c-f2{--cor:var(--roxo)} #c-h{--cor:var(--laranja)}
/* Selo de estado: so informa, nao e botao. Verde ligado, vermelho desligado. */
.selo{display:inline-block;margin-top:11px;padding:3px 11px;border:1px solid;
  border-radius:999px;font:800 9.5px var(--mono);letter-spacing:1.3px;
  color:var(--vermelho);border-color:var(--vermelho)}
.selo.on{color:var(--verde);border-color:var(--verde)}

.fig{border:1px solid var(--borda);border-radius:16px;background:var(--card);
  padding:12px 10px 9px}
svg{display:block;width:100%;height:auto}
.eixo,.leg{display:flex;justify-content:space-between;padding:0 3px;
  font:650 9.5px var(--mono);letter-spacing:1px;color:var(--mudo)}
.eixo{margin-top:3px}
.leg{margin-top:8px;justify-content:flex-start;gap:15px}
.leg span{display:flex;align-items:center;gap:5px}
.leg s{width:12px;height:3px;border-radius:2px;text-decoration:none}

.bloco{margin-top:10px;border:1px solid var(--borda);border-radius:16px;
  background:var(--card);padding:14px}
.bloco > b,.topo b{display:block;color:var(--mudo);font:800 9.5px var(--mono);
  letter-spacing:1.5px;text-transform:uppercase;margin-bottom:11px}
.linha{display:flex;align-items:center;gap:10px;margin-bottom:10px}
.linha label{flex:1;font-size:13.5px;line-height:1.3;color:var(--mudo)}
.linha label small{display:block;font-size:11px;color:var(--rodape)}
/* 16 px: com menos o iOS da zoom na pagina ao focar o campo */
.linha input{width:96px;min-height:44px;background:var(--bg);color:var(--texto);
  border:1px solid var(--borda);border-radius:10px;padding:8px 11px;
  font:700 16px var(--mono);text-align:right;-webkit-appearance:none;appearance:none}
.linha input:focus{outline:none;border-color:var(--ciano)}
.chave{display:flex;align-items:center;gap:12px;justify-content:space-between;
  margin-bottom:14px}
.chave button{flex:none}
.estado{font:800 11.5px var(--mono);letter-spacing:1.4px;color:var(--mudo)}
.estado.on{color:var(--verde)} .estado.quente{color:var(--laranja)}
.estado.falha{color:var(--vermelho)}
.chave small,.nota{display:block;color:var(--rodape);font:650 10.5px var(--mono);
  margin-top:3px}
button{-webkit-appearance:none;appearance:none;border:1px solid var(--borda);
  background:var(--bg);color:var(--texto);font:800 11.5px var(--mono);
  letter-spacing:1.2px;min-height:44px;padding:10px 16px;border-radius:11px;
  cursor:pointer;min-width:104px}
button:active{opacity:.7}
button.verde{border-color:var(--verde);color:var(--verde)}
button.vermelho{border-color:var(--vermelho);color:var(--vermelho)}
button:disabled{opacity:.45;cursor:default}

/* Controle de brilho: trilho fino, botao grande para o dedo */
.brilho{display:flex;align-items:center;gap:12px;margin-top:14px;
  padding-top:14px;border-top:1px solid var(--borda)}
.brilho label{font-size:13.5px;color:var(--mudo)}
.brilho output{width:46px;text-align:right;font:700 14px var(--mono)}
input[type=range]{flex:1;height:44px;margin:0;background:transparent;
  -webkit-appearance:none;appearance:none}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;
  background:linear-gradient(90deg,var(--ciano) var(--p,60%),var(--borda) var(--p,60%))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:30px;height:30px;
  margin-top:-12px;border-radius:50%;background:var(--texto);
  box-shadow:0 1px 6px rgba(0,0,0,.5)}
input[type=range]::-moz-range-track{height:6px;border-radius:3px;background:var(--borda)}
input[type=range]::-moz-range-progress{height:6px;border-radius:3px;background:var(--ciano)}
input[type=range]::-moz-range-thumb{width:28px;height:28px;border:0;border-radius:50%;
  background:var(--texto)}

/* Modal: sobe de baixo, como uma folha do iOS */
.fundo{position:fixed;inset:0;z-index:10;background:rgba(0,0,0,.62);
  display:flex;align-items:flex-end;justify-content:center}
.fundo[hidden]{display:none}
.modal{width:100%;max-width:440px;max-height:88vh;max-height:88dvh;overflow:auto;
  -webkit-overflow-scrolling:touch;overscroll-behavior:contain;
  background:var(--card);border:1px solid var(--borda);border-bottom:0;
  border-radius:20px 20px 0 0;animation:sobe .2s ease-out;
  padding:8px calc(16px + env(safe-area-inset-right))
          calc(16px + env(safe-area-inset-bottom)) calc(16px + env(safe-area-inset-left))}
.modal::before{content:"";display:block;width:38px;height:5px;border-radius:3px;
  background:var(--borda);margin:0 auto 10px}
@keyframes sobe{from{transform:translateY(40px);opacity:0}to{transform:none;opacity:1}}
.topo{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
.topo b{margin:0}
.sub{color:var(--mudo);font:800 9.5px var(--mono);letter-spacing:1.4px;
  text-transform:uppercase;margin:16px 0 10px;padding-top:14px;
  border-top:1px solid var(--borda)}
.topo button{min-width:0;padding:8px 14px}

footer{margin-top:16px;text-align:center;color:var(--rodape);
  font:700 10px var(--mono);letter-spacing:1.2px}
footer.ruim{color:var(--laranja)}
</style>

<!-- Mesma ordem do display: as duas umidades, temperatura e aquecedor, ventiladores. -->
<div class="grade">
  <div class="cartao" id="c-u"><div class="rotulo">Umidade relativa</div>
    <div class="valor"><span id="u">--</span><u>%</u></div>
    <div class="detalhe">CAI SÓ DE AQUECER</div></div>

  <div class="cartao" id="c-ua"><div class="rotulo">Umidade absoluta</div>
    <div class="valor"><span id="ua">--</span><u>g/m³</u></div>
    <div class="detalhe">ÁGUA REAL NO AR</div></div>

  <div class="cartao clicavel" id="c-t" data-abre="m-t"><div class="rotulo">Temperatura</div>
    <div class="valor"><span id="t">--</span><u>°C</u></div>
    <div class="detalhe" id="tmaxCard">MÁX <b>--</b></div></div>

  <div class="cartao clicavel" id="c-h" data-abre="m-h"><div class="rotulo">Aquecedor · alvo</div>
    <div class="valor"><span id="alvoCard">--</span><u>°C</u></div>
    <span class="selo" id="seloAq">DESLIGADO</span></div>

  <div class="cartao clicavel" id="c-f1" data-abre="m-f1"><div class="rotulo">Fan 01 · hotend</div>
    <div class="valor"><span id="f1">--</span><u>%</u></div>
    <span class="selo" id="seloF1">DESLIGADO</span></div>

  <div class="cartao clicavel" id="c-f2" data-abre="m-f2"><div class="rotulo">Fan 02 · circulação</div>
    <div class="valor"><span id="f2">--</span><u>%</u></div>
    <span class="selo" id="seloF2">DESLIGADO</span></div>
</div>

<div class="fig">
  <svg viewBox="0 0 320 150" preserveAspectRatio="none" aria-label="Temperatura e umidade dos últimos 5 minutos">
    <line x1="0" y1="37.5" x2="320" y2="37.5" stroke="#202b38"/>
    <line x1="0" y1="75"   x2="320" y2="75"   stroke="#202b38"/>
    <line x1="0" y1="112.5" x2="320" y2="112.5" stroke="#202b38"/>
    <polyline id="lt" fill="none" stroke="#ff9b52" stroke-width="2" vector-effect="non-scaling-stroke" points=""/>
    <polyline id="lu" fill="none" stroke="#42d7f5" stroke-width="2" vector-effect="non-scaling-stroke" points=""/>
  </svg>
  <div class="eixo"><span>5 MIN ATRÁS</span><span>AGORA</span></div>
  <div class="leg">
    <span><s style="background:#ff9b52"></s>0–60 °C</span>
    <span><s style="background:#42d7f5"></s>0–100 %</span>
  </div>
</div>

<div class="bloco">
  <b>Imagem do display</b>
  <div class="chave" style="margin:0">
    <div><span id="estadoTela" class="estado">—</span><small>apagar desliga também a luz de fundo</small></div>
    <button id="btnTela" disabled>—</button>
  </div>
  <div class="brilho">
    <label for="bri">Brilho</label>
    <input type="range" id="bri" min="5" max="100" step="5" value="60">
    <output id="briV">--</output>
  </div>
</div>

<footer id="estado">carregando…</footer>

<!-- ======================= Modais. Abrem ao tocar no cartao. ======================= -->
<div class="fundo" id="m-t" hidden><div class="modal">
  <div class="topo"><b>Temperatura</b><button data-fecha>FECHAR</button></div>
  <div class="linha"><label>Temperatura máxima · °C<small id="tmaxFaixa"></small></label><input id="tmax" inputmode="decimal" placeholder="--"></div>
  <small class="nota">Aparece em verde embaixo da temperatura. Salva ao sair do campo.</small>
</div></div>

<div class="fundo" id="m-h" hidden><div class="modal">
  <div class="topo"><b>Aquecedor</b><button data-fecha>FECHAR</button></div>
  <div class="chave">
    <div><span class="estado" data-estado-aq>—</span><small id="motivo"></small></div>
    <button data-autoriza disabled>—</button>
  </div>
  <div class="linha"><label>Alvo · °C<small>PETG: 55, o máximo da caixa</small></label><input data-k="alvo" inputmode="decimal"></div>
  <div class="linha"><label>Proteção · min<small>corta se ficar no máximo esse tempo sem subir 1 °C (5 a 60)</small></label><input data-k="subir" inputmode="numeric"></div>
  <div class="sub">Ajuste do PID</div>
  <div class="linha"><label>Kp · %/°C<small>potência por grau abaixo do alvo</small></label><input data-k="kp" inputmode="decimal"></div>
  <div class="linha"><label>Ki · %/°C·s<small>corrige o erro que sobra com o tempo</small></label><input data-k="ki" inputmode="decimal"></div>
  <div class="linha"><label>Kd · %·s/°C<small>freia a chegada ao alvo</small></label><input data-k="kd" inputmode="decimal"></div>
  <small class="nota">Nunca liga sozinho: começa desligado a cada boot. Passou do alvo e oscila: baixe o Ki. Demora a chegar ou fica abaixo: suba o Kp. Salva ao sair do campo.</small>
</div></div>

<div class="fundo" id="m-f2" hidden><div class="modal">
  <div class="topo"><b>Fan 02 · circulação</b><button data-fecha>FECHAR</button></div>
  <div class="chave">
    <div><span id="estadoF2" class="estado">—</span><small id="f2agora"></small></div>
    <button id="btnF2" disabled>—</button>
  </div>
  <div class="linha"><label>Máxima · %<small>com o aquecedor ligado</small></label><input data-k="f2max" inputmode="numeric"></div>
  <div class="linha"><label>Mínima · %<small>com o aquecedor desligado</small></label><input data-k="f2min" inputmode="numeric"></div>
  <div class="linha"><label>Piso de PWM · %<small>vale para os dois ventiladores</small></label><input data-k="pwmin" inputmode="numeric"></div>
  <small class="nota">Começa desligado a cada boot. Cada campo salva ao sair dele.</small>
</div></div>

<div class="fundo" id="m-f1" hidden><div class="modal">
  <div class="topo"><b>Fan 01 · hotend</b><button data-fecha>FECHAR</button></div>
  <div class="chave">
    <div><span id="estadoF1" class="estado">—</span><small>automático: segue o aquecedor</small></div>
  </div>
  <div class="linha"><label>A 100% depois de desligar · min<small>o bloco ainda está quente</small></label><input data-k="pos1" data-esc="60" inputmode="decimal"></div>
  <div class="linha"><label>Rampa até parar · min</label><input data-k="rmp1" data-esc="60" inputmode="decimal"></div>
  <div class="linha"><label>Piso de PWM · %<small>vale para os dois ventiladores</small></label><input data-k="pwmin" inputmode="numeric"></div>
  <small class="nota">100% enquanto aquece. Cada campo salva ao sair dele.</small>
</div></div>

<script>
var L = 320, A = 150;
function $(id) { return document.getElementById(id); }
function todos(sel) { return document.querySelectorAll(sel); }

function pontos(v, min, max) {
  if (!v || v.length < 2) return "";
  var s = "", i, f;
  for (i = 0; i < v.length; i++) {
    f = (Math.min(max, Math.max(min, v[i])) - min) / (max - min);
    s += (i * L / (v.length - 1)).toFixed(1) + "," + ((A - 1) * (1 - f)).toFixed(1) + " ";
  }
  return s;
}

// POST que mostra o motivo quando a placa recusa (faixa invalida etc.).
// Depois de cada envio atualiza na hora, sem esperar o ciclo de 2 s.
function envia(url) {
  return fetch(url, { method: "POST" }).then(function (r) {
    if (!r.ok) return r.text().then(function (t) { alert(t); });
  }).catch(function () {}).then(ciclo);
}

// Aceita virgula: no iPhone em portugues o teclado decimal da virgula.
function numero(txt) { return parseFloat(String(txt).replace(",", ".")); }

// Botao de liga/desliga: mostra a acao, nao o estado.
function chave(b, ligado, txtDesliga, txtLiga) {
  b.disabled = false;
  b.dataset.on = ligado ? "1" : "0";
  b.textContent = ligado ? txtDesliga : txtLiga;
  b.className = ligado ? "vermelho" : "verde";
}

function marca(span, texto, classe) {
  span.textContent = texto;
  span.className = "estado " + classe;
}

// --- campos -----------------------------------------------------------------

// Cursor no fim ao tocar, em todos os campos de texto. O iOS posiciona o
// cursor onde o dedo tocou, em momentos diferentes conforme a versao - as
// vezes depois do foco, as vezes depois do clique. Por isso o ajuste se
// repete por ~300 ms apos o toque. Para assim que a pessoa digita.
function vaiProFim(e) {
  var n = e.value.length;
  try { e.setSelectionRange(n, n); } catch (x) {}
}
function insiste(el) {
  [0, 40, 100, 200, 320].forEach(function (t) {
    setTimeout(function () {
      if (el._fim && document.activeElement === el) vaiProFim(el);
    }, t);
  });
  clearTimeout(el._fimT);
  el._fimT = setTimeout(function () { el._fim = false; }, 360);
}
todos("input:not([type=range])").forEach(function (e) {
  e.autocomplete = "off";
  e.addEventListener("focus", function () { this._fim = true; insiste(this); });
  e.addEventListener("click", function () { if (this._fim) insiste(this); });
  e.addEventListener("touchend", function () { if (this._fim) insiste(this); });
  e.addEventListener("keydown", function () { this._fim = false; });
  e.addEventListener("input", function () { this._fim = false; });
});

// Campos de configuracao: data-k e a chave em /config; data-esc converte a
// unidade da tela para a da placa (minutos na tela, segundos na placa).
todos("input[data-k]").forEach(function (e) {
  e.addEventListener("change", function () {
    var v = numero(this.value);
    if (isNaN(v)) return;
    var esc = Number(this.dataset.esc || 1);
    envia("config?" + this.dataset.k + "=" + (esc > 1 ? Math.round(v * esc) : v));
  });
});

$("tmax").addEventListener("change", function () {
  var v = numero(this.value);
  if (!isNaN(v)) envia("tmax?v=" + v);
});

// --- chaves -----------------------------------------------------------------
// Ligar o aquecedor so pelo modal: o cartao mostra o selo, nao tem botao.
todos("[data-autoriza]").forEach(function (b) {
  b.addEventListener("click", function () {
    this.disabled = true;
    envia("autorizar?v=" + (this.dataset.on === "1" ? 0 : 1));
  });
});

function selo(el, ligado, texto) {
  el.textContent = texto;
  el.className = "selo" + (ligado ? " on" : "");
}
$("btnF2").addEventListener("click", function () {
  this.disabled = true;
  envia("config?f2on=" + (this.dataset.on === "1" ? 0 : 1));
});
$("btnTela").addEventListener("click", function () {
  this.disabled = true;
  envia("tela?estado=" + (this.dataset.on === "1" ? "off" : "on"));
});

// Brilho: o trilho pinta ate o ponto escolhido; envia ao soltar.
var bri = $("bri");
function pintaBrilho(v) {
  $("briV").textContent = v + "%";
  bri.style.setProperty("--p", ((v - 5) / 95 * 100) + "%");
}
// No Safari do iPhone o controle nem sempre ganha foco ao ser arrastado:
// por isso a marca _mexendo, que impede o ciclo de puxa-lo de volta no meio
// do gesto.
bri.addEventListener("input", function () { this._mexendo = true; pintaBrilho(this.value); });
bri.addEventListener("change", function () {
  this._mexendo = false;
  envia("brilho?v=" + this.value);
});

// --- modais -----------------------------------------------------------------
function fecha() {
  // Tira o foco antes de esconder: garante o change do campo em edicao.
  if (document.activeElement) document.activeElement.blur();
  todos(".fundo").forEach(function (m) { m.hidden = true; });
  document.documentElement.classList.remove("travado");
}
todos("[data-abre]").forEach(function (c) {
  c.addEventListener("click", function () {
    $(this.dataset.abre).hidden = false;
    document.documentElement.classList.add("travado");
  });
});
todos("[data-fecha]").forEach(function (b) { b.addEventListener("click", fecha); });
todos(".fundo").forEach(function (m) {
  m.addEventListener("click", function (ev) { if (ev.target === m) fecha(); });
});
document.addEventListener("keydown", function (ev) { if (ev.key === "Escape") fecha(); });

// --- atualizacao ------------------------------------------------------------
function ciclo() {
  return fetch("dados", { cache: "no-store" }).then(function (r) {
    return r.json();
  }).then(function (d) {
    var e = $("estado");

    $("c-t").classList.toggle("vivo", d.sensor);
    $("c-u").classList.toggle("vivo", d.sensor);
    $("c-ua").classList.toggle("vivo", d.sensor);
    $("t").textContent = d.sensor ? d.t.toFixed(1) : "--";
    $("u").textContent = d.sensor ? d.u.toFixed(1) : "--";
    $("ua").textContent = d.sensor ? d.ua.toFixed(1) : "--";

    e.textContent = d.sensor
      ? "SENSOR OK · " + d.n + " AMOSTRAS · LIGADO HÁ " + Math.floor(d.ligado / 60) + " MIN"
      : "SENSOR NÃO ENCONTRADO";
    e.className = d.sensor ? "" : "ruim";

    $("lt").setAttribute("points", pontos(d.hist.t, 0, 60));
    $("lu").setAttribute("points", pontos(d.hist.u, 0, 100));

    // Aquecedor: o estado e o botao aparecem no cartao e no modal.
    var classeAq = d.estado === "PROTECAO" ? "falha" : d.aquecendo ? "quente" : "";
    var textoAq = d.estado === "AQUECENDO" ? "AQUECENDO · " + d.pot + " %" : d.estado;
    todos("[data-estado-aq]").forEach(function (s) { marca(s, textoAq, classeAq); });
    todos("[data-autoriza]").forEach(function (b) { chave(b, d.autorizado, "DESLIGAR", "LIGAR"); });
    $("motivo").textContent = d.motivo || "";
    if (d.estado === "PROTECAO") selo($("seloAq"), false, "PROTEÇÃO");
    else selo($("seloAq"), d.autorizado, d.autorizado ? "LIGADO" : "DESLIGADO");
    $("c-h").classList.toggle("vivo", d.aquecendo);
    $("alvoCard").textContent = d.cfg.alvo.toFixed(0);

    // Campos: nao sobrescreve o que esta sendo digitado.
    todos("input[data-k]").forEach(function (i) {
      if (document.activeElement === i) return;
      var esc = Number(i.dataset.esc || 1);
      var v = d.cfg[i.dataset.k];
      i.value = esc > 1 ? String(Math.round(v / esc * 10) / 10) : v;
    });

    // Temperatura maxima: verde quando definida, como no display.
    var tm = $("tmaxCard"), temTmax = d.tmax !== null;
    tm.className = "detalhe" + (temTmax ? " cfg" : "");
    tm.innerHTML = "MÁX <b>" + (temTmax ? d.tmax.toFixed(1) : "--") + "</b>" + (temTmax ? " °C" : "");
    if (document.activeElement !== $("tmax")) $("tmax").value = temTmax ? d.tmax : "";
    $("tmaxFaixa").textContent = "de " + d.tmaxMin + " a " + d.teto + " °C";

    // Ventiladores
    $("c-f1").classList.toggle("vivo", d.fan1 > 0);
    $("c-f2").classList.toggle("vivo", d.fan2 > 0);
    $("f1").textContent = d.fan1;
    $("f2").textContent = d.fan2;
    marca($("estadoF1"), d.fan1 > 0 ? "LIGADO · " + d.fan1 + " %" : "DESLIGADO", d.fan1 > 0 ? "on" : "");
    selo($("seloF1"), d.fan1 > 0, d.fan1 > 0 ? "LIGADO" : "DESLIGADO");

    var f2 = d.cfg.f2on;
    marca($("estadoF2"), f2 ? "LIGADO · " + d.fan2 + " %" : "DESLIGADO", f2 ? "on" : "");
    $("f2agora").textContent = f2
      ? (d.aquecendo ? "na máxima: aquecedor ligado" : "na mínima: aquecedor desligado")
      : "ligue aqui para começar a girar";
    chave($("btnF2"), f2, "DESLIGAR", "LIGAR");
    selo($("seloF2"), f2, f2 ? "LIGADO" : "DESLIGADO");

    // Imagem do display e brilho
    marca($("estadoTela"), d.tela ? "LIGADA" : "APAGADA", d.tela ? "on" : "");
    chave($("btnTela"), d.tela, "APAGAR", "LIGAR");
    if (!bri._mexendo && document.activeElement !== bri) {
      bri.value = d.brilho;
      pintaBrilho(d.brilho);
    }
  }).catch(function () {
    var e = $("estado");
    e.textContent = "SEM RESPOSTA DA PLACA";
    e.className = "ruim";
  });
}

ciclo();
setInterval(ciclo, 2000);
</script>
)HTML";
