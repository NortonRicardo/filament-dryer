// ---------------------------------------------------------------------------
// A pagina servida em "/". Fica em PROGMEM: nao ocupa RAM, so flash.
//
// Mesmo desenho do display: mesma paleta, mesmos cartoes, mesma ordem. As
// fontes sao as do sistema de proposito - o celular pode estar sem internet
// e uma fonte de CDN nao carregaria.
//
// Sem framework, sem CDN e sem arquivo externo: um GET entrega tudo.
// ---------------------------------------------------------------------------

#pragma once

#include <pgmspace.h>

static const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Estufa de filamento</title>
<style>
:root{
  --bg:#080b10; --card:#111720; --borda:#202b38; --texto:#f3f6fa;
  --mudo:#8290a1; --ciano:#42d7f5; --azul:#6197ff; --laranja:#ff9b52;
  --verde:#55e0a0; --roxo:#a78bfa; --rodape:#586575;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
}
*{box-sizing:border-box}
body{margin:0 auto;max-width:430px;padding:14px 14px 28px;background:var(--bg);
  color:var(--texto);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
.grade{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
.cartao{border:1px solid var(--borda);border-radius:14px;background:var(--card);
  padding:15px 10px 13px;text-align:center;min-height:120px;
  display:flex;flex-direction:column;justify-content:center}
.cartao.larga{grid-column:1/-1;min-height:132px}
.rotulo{color:var(--mudo);font:800 9px var(--mono);letter-spacing:1.5px;
  text-transform:uppercase;margin-bottom:12px}
.valor{font-size:34px;font-weight:750;line-height:1;letter-spacing:-1.2px;
  font-variant-numeric:tabular-nums;color:var(--mudo)}
.cartao.larga .valor{font-size:42px}
.valor u{font-size:14px;font-weight:600;color:var(--mudo);margin-left:3px;
  letter-spacing:0;text-decoration:none}
.detalhe{color:var(--mudo);font:650 9px var(--mono);letter-spacing:1px;
  margin-top:11px}
.vivo .valor{color:var(--cor)}
#c-t{--cor:var(--laranja)} #c-u{--cor:var(--ciano)}
#c-f1{--cor:var(--azul)} #c-f2{--cor:var(--roxo)} #c-h{--cor:var(--laranja)}

.fig{border:1px solid var(--borda);border-radius:14px;background:var(--card);
  padding:12px 10px 9px}
svg{display:block;width:100%;height:auto}
.eixo,.leg{display:flex;justify-content:space-between;padding:0 3px;
  font:650 9.5px var(--mono);letter-spacing:1px;color:var(--mudo)}
.eixo{margin-top:3px}
.leg{margin-top:8px;justify-content:flex-start;gap:15px}
.leg span{display:flex;align-items:center;gap:5px}
.leg s{width:12px;height:3px;border-radius:2px;text-decoration:none}

.painel{margin-top:10px;border:1px solid var(--borda);border-radius:14px;
  background:var(--card);padding:13px 14px;display:flex;align-items:center;
  justify-content:space-between;gap:12px}
.painel{flex-wrap:wrap}
.brilho{flex:1 1 100%;display:flex;align-items:center;gap:10px;
  border-top:1px solid var(--borda);padding-top:12px;margin-top:2px}
.brilho input{flex:1;accent-color:var(--ciano)}
.brilho b{white-space:nowrap}
.painel b{display:block;color:var(--mudo);font:800 9px var(--mono);
  letter-spacing:1.5px;text-transform:uppercase}
.painel small{color:var(--rodape);font:650 10px var(--mono)}
button{appearance:none;border:1px solid var(--borda);background:var(--bg);
  color:var(--texto);font:800 11px var(--mono);letter-spacing:1.2px;
  padding:10px 16px;border-radius:9px;cursor:pointer;min-width:96px}
button[data-on="1"]{border-color:var(--verde);color:var(--verde)}
button:disabled{opacity:.45;cursor:default}

footer{margin-top:16px;text-align:center;color:var(--rodape);
  font:700 10px var(--mono);letter-spacing:1.2px}
footer.ruim{color:var(--laranja)}
</style>

<div class="grade">
  <div class="cartao" id="c-t"><div class="rotulo">Temperatura</div>
    <div class="valor"><span id="t">--</span><u>°C</u></div>
    <div class="detalhe">AMBIENTE</div></div>

  <div class="cartao" id="c-u"><div class="rotulo">Umidade</div>
    <div class="valor"><span id="u">--</span><u>%</u></div>
    <div class="detalhe">RELATIVA</div></div>

  <div class="cartao" id="c-f1"><div class="rotulo">Fan 01</div>
    <div class="valor"><span>--</span><u>%</u></div>
    <div class="detalhe">12V · PWM</div></div>

  <div class="cartao" id="c-f2"><div class="rotulo">Fan 02</div>
    <div class="valor"><span>--</span><u>%</u></div>
    <div class="detalhe">12V · PWM</div></div>

  <div class="cartao larga" id="c-h"><div class="rotulo">Hotend · Potência</div>
    <div class="valor"><span>--</span><u>W</u></div>
    <div class="detalhe">PWM</div></div>
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

<div class="painel">
  <div><b>Painel do aparelho</b><small id="aviso">a luz de fundo não tem pino próprio</small></div>
  <button id="btn" disabled>—</button>

  <div class="brilho">
    <b>Brilho</b>
    <input type="range" id="bri" min="0" max="255" value="255">
    <small id="bri-v">255</small>
  </div>
</div>

<footer id="estado">carregando…</footer>

<script>
var L = 320, A = 150;

function pontos(v, min, max) {
  if (!v || v.length < 2) return "";
  var s = "", i, f;
  for (i = 0; i < v.length; i++) {
    f = (Math.min(max, Math.max(min, v[i])) - min) / (max - min);
    s += (i * L / (v.length - 1)).toFixed(1) + "," + ((A - 1) * (1 - f)).toFixed(1) + " ";
  }
  return s;
}

function marcaTela(ligada) {
  var b = document.getElementById("btn");
  b.disabled = false;
  b.dataset.on = ligada ? "1" : "0";
  b.textContent = ligada ? "LIGADO" : "DESLIGADO";
}

document.getElementById("btn").addEventListener("click", function () {
  var b = this;
  b.disabled = true;
  fetch("tela?estado=alterna", { method: "POST" })
    .then(function (r) { return r.json(); })
    .then(function (d) { marcaTela(d.tela); })
    .catch(function () { b.disabled = false; });
});

var bri = document.getElementById("bri");
bri.addEventListener("input", function () {
  document.getElementById("bri-v").textContent = this.value;
});
bri.addEventListener("change", function () {
  fetch("brilho?v=" + this.value, { method: "POST" }).catch(function () {});
});

function ciclo() {
  fetch("dados", { cache: "no-store" }).then(function (r) {
    return r.json();
  }).then(function (d) {
    var e = document.getElementById("estado");

    document.getElementById("c-t").classList.toggle("vivo", d.sensor);
    document.getElementById("c-u").classList.toggle("vivo", d.sensor);
    document.getElementById("t").textContent = d.sensor ? d.t.toFixed(1) : "--";
    document.getElementById("u").textContent = d.sensor ? d.u.toFixed(1) : "--";

    e.textContent = d.sensor
      ? "SENSOR OK · " + d.n + " AMOSTRAS · LIGADO HÁ " + Math.floor(d.ligado / 60) + " MIN"
      : "SENSOR NÃO ENCONTRADO";
    e.className = d.sensor ? "" : "ruim";

    document.getElementById("lt").setAttribute("points", pontos(d.hist.t, 0, 60));
    document.getElementById("lu").setAttribute("points", pontos(d.hist.u, 0, 100));

    marcaTela(d.tela);
    if (document.activeElement !== bri) {
      bri.value = d.brilho;
      document.getElementById("bri-v").textContent = d.brilho;
    }
  }).catch(function () {
    var e = document.getElementById("estado");
    e.textContent = "SEM RESPOSTA DA PLACA";
    e.className = "ruim";
  });
}

ciclo();
setInterval(ciclo, 2000);
</script>
)HTML";
