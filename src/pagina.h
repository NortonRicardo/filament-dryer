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
<meta name="theme-color" content="#11100f">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Estufa">
<title>Estufa de filamento</title>
<style>
:root{
  --bg:#11100f; --card:#0c0b0b; --borda:#33302f; --texto:#f5f3f2;
  --mudo:#aaa6a5; --ciano:#42d7f5; --azul:#6197ff; --laranja:#ff9b52;
  --verde:#38d98a; --roxo:#a78bfa; --vermelho:#e45b5b; --rodape:#7d7978;
  --turquesa:#5eead4; --claro:#f5f3f2; --escuro:#171515;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{-webkit-text-size-adjust:100%}
html.travado,html.travado body{overflow:hidden}
body{margin:0 auto;max-width:460px;background:var(--bg);color:var(--texto);
  font:15px/1.5 Inter,-apple-system,system-ui,"Segoe UI",sans-serif;
  padding:calc(12px + env(safe-area-inset-top)) calc(12px + env(safe-area-inset-right))
          calc(24px + env(safe-area-inset-bottom)) calc(12px + env(safe-area-inset-left))}
button,.clicavel,input{touch-action:manipulation}

.painel{border:1px solid var(--borda);border-radius:34px;
  background:linear-gradient(160deg,#0c0b0b 0%,#050505 72%);
  box-shadow:0 22px 60px #0007;padding:22px 20px 22px}
.topline{height:34px;display:grid;grid-template-columns:1fr 1fr 1fr;
  align-items:center;margin-bottom:22px}
.relogio{grid-column:2;justify-self:center;font-size:13px;font-weight:650;
  letter-spacing:.09em;font-variant-numeric:tabular-nums}
.conexao{grid-column:3;justify-self:end;display:inline-flex;align-items:center;
  gap:6px;color:var(--verde);font-size:10px;font-weight:600;white-space:nowrap}
.conexao.ruim{color:var(--vermelho)}
.conexao svg{width:17px;height:17px}
.titulo{margin:0 0 5px;font-size:12px;font-weight:600;letter-spacing:.13em;
  text-transform:uppercase;color:var(--mudo)}
.descricao{margin:0 0 19px;font-size:11px;color:#d5d1d0}

/* Cartoes claros. Tocar num cartao com "›" abre a configuracao dele. */
.grade{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}
.cartao{min-width:0;min-height:104px;padding:15px 14px 13px;border:1.5px solid var(--claro);
  border-radius:10px;background:var(--claro);color:var(--escuro);text-align:center;
  display:flex;flex-direction:column;justify-content:space-between;align-items:center}
.clicavel{cursor:pointer;position:relative;user-select:none;-webkit-user-select:none;
  transition:transform .12s}
.clicavel:active{transform:scale(.98)}
.clicavel::after{content:"›";position:absolute;right:11px;top:9px;
  font:400 20px -apple-system,system-ui;color:#8f8a89}
.rotulo{font-size:11px;line-height:1.25;font-weight:550;opacity:.83}
.leitura{display:flex;align-items:baseline;justify-content:center;gap:5px;margin-top:10px;
  white-space:nowrap;font-variant-numeric:tabular-nums}
.valor{font-size:29px;line-height:1;font-weight:650;letter-spacing:-.055em}
.valor u,.un{font-size:11px;font-weight:600;opacity:.72;text-decoration:none;
  letter-spacing:0;margin-left:4px}
.detalhe{font-size:10.5px;font-weight:600;color:#6f6a69;margin-top:8px}
.detalhe b{color:var(--escuro);font-weight:700}
.detalhe.cfg,.detalhe.cfg b{color:#16a34a}
.sem .valor{color:#a39e9d}

.temperatura{grid-column:1/-1}
/* Temperatura: valor a esquerda; a direita, um sobre o outro, o alvo do
   aquecedor e a maxima, separados do valor por uma linha */
.temperatura{flex-direction:row;align-items:stretch;padding:14px 0}
.temperatura .esq{flex:none;width:50%;display:flex;flex-direction:column;align-items:center;
  justify-content:center}
.temperatura .dir{flex:none;width:50%;display:flex;flex-direction:column;justify-content:center;
  gap:6px;padding:0 12px 0 12px;border-left:1px solid #dedbda}
/* Sem o "›" na temperatura: a coluna da direita precisa do espaco */
.temperatura.clicavel::after{display:none}

.lado{display:flex;justify-content:space-between;align-items:center;gap:8px;
  min-height:28px}
.lado b{font-size:15px;font-weight:700;font-variant-numeric:tabular-nums;color:#6f6a69}
/* Etiqueta: icone dentro do nome, texto claro numa pilula da cor do valor */
.tag{display:inline-flex;align-items:center;gap:5px;padding:3px 9px 3px 7px;
  border-radius:999px;background:#8f8a89;color:#fff;font-size:10px;font-weight:800;
  letter-spacing:.05em;white-space:nowrap}
.tag svg{width:12px;height:12px}
.lado.alvo b{color:#ea580c} .lado.alvo .tag{background:#ea580c}
.lado.cfg b{color:#16a34a} .lado.cfg .tag{background:#16a34a}
.lado.aq .tag{background:#6f6a69}

/* Aquecedor: potencia e fan do aquecedor lado a lado, cada um abre o seu modal */
.aquecedor{grid-column:1/-1;display:grid;grid-template-columns:1fr 1fr;gap:0;
  padding:0;min-height:0;align-items:stretch}
.aquecedor .coluna + .coluna::before{content:"";position:absolute;left:0;top:16px;
  bottom:16px;border-left:1px solid #dedbda}
.coluna{position:relative;min-height:124px;border-radius:10px;color:var(--escuro);
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  gap:10px;text-align:center;padding:16px 10px}
.grande{font-size:32px;line-height:1;font-weight:700;font-variant-numeric:tabular-nums}
.st{font-size:11px;font-weight:700;letter-spacing:.04em;color:#dc2626}
.st.ativo{color:#16a34a}
.st.quente{color:#ea580c}
/* Parado: sem bolinha, texto claro numa pilula vermelha */
.st.parado{color:var(--claro);background:#dc2626;border-radius:999px;padding:3px 10px}

.fan .valor{font-size:29px}

/* Cartoes com icone: icone animado a esquerda, texto alinhado a direita */
.icard{flex-direction:row;align-items:center;justify-content:space-between;gap:10px}
.icard .txt{display:flex;flex-direction:column;align-items:flex-end;text-align:right;gap:6px}
.icard .leitura{justify-content:flex-end;margin-top:0}
.icard.clicavel::after{display:none}
.helice,.chama{width:46px;height:46px;flex:none;color:#b5b0af}
.helice.gira{color:#16a34a;animation:gira var(--dur,1s) linear infinite}
@keyframes gira{to{transform:rotate(360deg)}}
.chama .fora{fill:#b5b0af} .chama .miolo{fill:none}
.chama.acesa{transform-origin:50% 92%;animation:tremula .45s ease-in-out infinite alternate}
.chama.acesa .fora{fill:#dc2626} .chama.acesa .miolo{fill:#f97316}
@keyframes tremula{0%{transform:scale(1,1) rotate(-3deg)}
  50%{transform:scale(.96,1.06) rotate(1deg)}100%{transform:scale(1.04,.95) rotate(3deg)}}
@media (prefers-reduced-motion:reduce){.helice.gira,.chama.acesa{animation:none}}
.gota{display:inline-block;width:13px;height:13px;fill:#0ea5e9;vertical-align:-1px;margin-right:5px}
#c-u,#c-ua{justify-content:flex-start;gap:12px}
#c-u .leitura,#c-ua .leitura{margin-top:4px}
.st{white-space:nowrap}

/* Cabecalho: titulo a esquerda; icone do display e o interruptor dele a direita */
.cabecalho{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}
.telactl{display:flex;align-items:center;gap:10px}
button.icone{min-width:0;min-height:0;width:38px;height:38px;padding:0;border-radius:11px;
  background:transparent;color:var(--texto);display:inline-flex;align-items:center;
  justify-content:center}
.icone svg{width:20px;height:20px}
/* Interruptor: a bolinha vai para a direita quando liga */
button.sw{min-width:0;min-height:0;width:48px;height:30px;padding:0;border:0;
  border-radius:999px;background:#4a4645;position:relative;flex:none;
  transition:background .18s}
button.sw::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;
  border-radius:50%;background:#fff;box-shadow:0 1px 3px #0006;transition:transform .18s}
button.sw.on{background:var(--verde)}
button.sw.on::after{transform:translateX(18px)}
.cartao button.sw{background:#cfcac9;width:42px;height:26px}
.cartao button.sw::after{width:20px;height:20px}
.cartao button.sw.on{background:#16a34a}
.cartao button.sw.on::after{transform:translateX(16px)}

.fig{border:1px solid var(--borda);border-radius:16px;background:var(--card);
  padding:14px 12px 10px}
svg{display:block;width:100%;height:auto}
.figtopo{display:flex;justify-content:space-between;align-items:baseline;margin:0 2px 8px}
.figtopo b{font-size:12px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;
  color:var(--mudo)}
.legenda{display:flex;gap:14px;font-size:12px;font-weight:650;font-variant-numeric:tabular-nums}
.legenda span{display:flex;align-items:center;gap:6px}
.legenda i{width:8px;height:8px;border-radius:50%;background:var(--c)}
.eixo{display:flex;justify-content:space-between;margin:2px 10.6% 0;font-size:10.5px;
  color:var(--rodape);font-variant-numeric:tabular-nums}

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
/* Celular estreito: menos margem e etiquetas/interruptor um pouco menores,
   para a etiqueta AQUECEDOR e o interruptor caberem na metade do cartao */
@media (max-width:420px){
  body{padding-left:calc(8px + env(safe-area-inset-left));padding-right:calc(8px + env(safe-area-inset-right))}
  .painel{padding-left:14px;padding-right:14px}
  .temperatura .dir{padding:0 10px}
  .tag{font-size:9.5px;letter-spacing:.03em;padding:3px 7px 3px 6px;gap:4px}
  .tag svg{width:11px;height:11px}
  .lado{gap:6px}
  .cartao button.sw{width:38px;height:22px}
  .cartao button.sw::after{width:16px;height:16px}
  .cartao button.sw.on::after{transform:translateX(16px)}
}
</style>

<main class="painel">
<header class="topline">
  <time class="relogio" id="relogio">--:--</time>
  <div class="conexao" id="conexao">
    <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
      <path d="M2.8 8.8a14.2 14.2 0 0 1 18.4 0M6.1 12.2a9.1 9.1 0 0 1 11.8 0M9.5 15.6a4.1 4.1 0 0 1 5 0" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"/>
      <circle cx="12" cy="19.2" r="1.5" fill="currentColor"/>
    </svg>
    <span id="conexaoTxt">conectando</span>
  </div>
</header>
<div class="cabecalho">
  <div><p class="descricao" style="margin:10px 0 0">Leituras atuais da caixa</p></div>
  <div class="telactl">
    <button class="icone" data-abre="m-d" aria-label="Brilho do display"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/></svg></button>
    <button class="sw" id="swTela" role="switch" aria-label="Ligar ou desligar o display" disabled></button>
  </div>
</div>

<div class="grade">
  <div class="cartao clicavel temperatura" id="c-t" data-abre="m-t">
    <div class="esq"><div class="rotulo">Temperatura</div>
      <div class="leitura"><span class="valor" id="t">--</span><span class="un">°C</span></div></div>
    <div class="dir">
      <div class="lado alvo"><span class="tag"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 21V4M5 4h11l-2 4 2 4H5"/></svg>ALVO</span><b><span id="alvoCard">--</span>°</b></div>
      <div class="lado" id="tmaxCard"><span class="tag"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3l7 3v5c0 5-3.5 8.5-7 10-3.5-1.5-7-5-7-10V6z"/><path d="M12 8v5M12 16h.01"/></svg>MÁXIMO</span><b id="tmaxV">--</b></div>
      <div class="lado aq"><span class="tag"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v8M6.5 7a7 7 0 1 0 11 0"/></svg>AQUECEDOR</span><button class="sw" data-autoriza role="switch" aria-label="Ligar ou desligar o aquecedor" disabled></button></div>
    </div></div>

  <div class="cartao" id="c-u"><div class="rotulo"><svg class="gota" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3c3.5 4.5 6 7.6 6 11a6 6 0 0 1-12 0c0-3.4 2.5-6.5 6-11z"/></svg>Relativa</div>
    <div class="leitura"><span class="valor" id="u">--</span><span class="un">% UR</span></div></div>

  <div class="cartao" id="c-ua"><div class="rotulo"><svg class="gota" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3c3.5 4.5 6 7.6 6 11a6 6 0 0 1-12 0c0-3.4 2.5-6.5 6-11z"/></svg>Absoluta</div>
    <div class="leitura"><span class="valor" id="ua">--</span><span class="un">g/m³</span></div>
    <div class="detalhe">água real no ar</div></div>

  <div class="cartao aquecedor">
    <div class="coluna clicavel icard" id="c-h" data-abre="m-h">
      <svg class="chama" id="iAq" viewBox="0 0 48 48" aria-hidden="true"><path class="fora" d="M24 4c3 8 13 13 13 25a13 13 0 0 1-26 0c0-6 3-10 6.5-13 .8 4 3 6.5 6 7.2C21 17 21 10 24 4z"/><path class="miolo" d="M24 22c2 4 7 7 7 12a7 7 0 0 1-14 0c0-3 1.5-5 3.5-6.5.5 2 1.6 3.2 3.2 3.6C23 28 22.8 25 24 22z"/></svg>
      <div class="txt"><span class="rotulo">Aquecedor</span>
        <div class="leitura"><span class="valor" id="potCard">--</span><span class="un">%</span></div>
        <span class="st parado" id="stAq">DESLIGADO</span></div>
    </div>
    <div class="coluna clicavel icard" id="c-f1" data-abre="m-f1">
      <svg class="helice" id="iF1" viewBox="0 0 48 48" aria-hidden="true"><circle cx="24" cy="24" r="22" fill="none" stroke="currentColor" stroke-width="2.5"/><g fill="currentColor"><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z"/><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z" transform="rotate(120 24 24)"/><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z" transform="rotate(240 24 24)"/></g><circle cx="24" cy="24" r="4" fill="#f5f3f2" stroke="currentColor" stroke-width="2.5"/></svg>
      <div class="txt"><span class="rotulo">Fan aquecedor</span>
        <div class="leitura"><span class="valor" id="f1">--</span><span class="un">%</span></div>
        <span class="st parado" id="stF1">INATIVO</span></div>
    </div>
  </div>

  <div class="cartao clicavel fan icard" id="c-f2" data-abre="m-f2">
    <svg class="helice" id="iF2" viewBox="0 0 48 48" aria-hidden="true"><circle cx="24" cy="24" r="22" fill="none" stroke="currentColor" stroke-width="2.5"/><g fill="currentColor"><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z"/><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z" transform="rotate(120 24 24)"/><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z" transform="rotate(240 24 24)"/></g><circle cx="24" cy="24" r="4" fill="#f5f3f2" stroke="currentColor" stroke-width="2.5"/></svg>
    <div class="txt"><div class="rotulo">Fan 1</div>
      <div class="leitura"><span class="valor" id="f2">--</span><span class="un">%</span></div>
      <span class="st parado" id="stF2">INATIVO</span></div></div>

  <div class="cartao clicavel fan icard" id="c-f3" data-abre="m-f2">
    <svg class="helice" id="iF3" viewBox="0 0 48 48" aria-hidden="true"><circle cx="24" cy="24" r="22" fill="none" stroke="currentColor" stroke-width="2.5"/><g fill="currentColor"><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z"/><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z" transform="rotate(120 24 24)"/><path d="M24 24C22 15 25 7 31 6c5 1 4 9-7 18z" transform="rotate(240 24 24)"/></g><circle cx="24" cy="24" r="4" fill="#f5f3f2" stroke="currentColor" stroke-width="2.5"/></svg>
    <div class="txt"><div class="rotulo">Fan 2</div>
      <div class="leitura"><span class="valor" id="f3">--</span><span class="un">%</span></div>
      <span class="st parado" id="stF3">INATIVO</span></div></div>
</div>

<div class="fig">
  <div class="figtopo"><b>Última hora</b>
    <span class="legenda"><span><i style="--c:#ff9b52"></i><span id="legT">--</span></span>
      <span><i style="--c:#42d7f5"></i><span id="legU">--</span></span></span></div>
  <svg viewBox="0 0 320 156" aria-label="Temperatura e umidade da última hora">
    <defs><linearGradient id="gT" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ff9b52" stop-opacity=".32"/>
      <stop offset="1" stop-color="#ff9b52" stop-opacity="0"/></linearGradient></defs>
    <g id="grade" font-size="9" font-family="Inter,-apple-system,system-ui,sans-serif"></g>
    <path id="at" fill="url(#gT)"/>
    <path id="lu" fill="none" stroke="#42d7f5" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round"/>
    <path id="lt" fill="none" stroke="#ff9b52" stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round"/>
    <circle id="pu" r="3" fill="#42d7f5"/>
    <circle id="pt" r="3.6" fill="#ff9b52" stroke="#0c0b0b" stroke-width="1.5"/>
  </svg>
  <div class="eixo"><span>−60 min</span><span>−30 min</span><span>agora</span></div>
</div>

<footer id="estado">carregando…</footer>
</main>
<p style="text-align:center;margin:6px 0 0"><a href="log" style="color:var(--rodape);font:700 10px var(--mono);letter-spacing:1.2px">VER LOG</a> &nbsp;·&nbsp; <a href="historico" style="color:var(--rodape);font:700 10px var(--mono);letter-spacing:1.2px">HISTÓRICO 24 H</a> &nbsp;·&nbsp; <a href="historico?horas=0" style="color:var(--rodape);font:700 10px var(--mono);letter-spacing:1.2px">HISTÓRICO TODO</a> &nbsp;·&nbsp; <a href="#" id="testaAviso" style="color:var(--rodape);font:700 10px var(--mono);letter-spacing:1.2px">TESTAR AVISO</a> &nbsp;·&nbsp; <a href="#" id="limpaHist" style="color:var(--rodape);font:700 10px var(--mono);letter-spacing:1.2px">LIMPAR HISTÓRICO</a> &nbsp;·&nbsp; <a href="#" id="limpaLog" style="color:var(--rodape);font:700 10px var(--mono);letter-spacing:1.2px">LIMPAR LOG</a></p>

<!-- ======================= Modais. Abrem ao tocar no cartao. ======================= -->
<div class="fundo" id="m-t" hidden><div class="modal">
  <div class="topo"><b>Temperatura e aquecedor</b><button data-fecha>FECHAR</button></div>
  <div class="chave">
    <div><span class="estado" data-estado-aq>—</span><small id="motivo"></small></div>
    <button class="sw" data-autoriza role="switch" aria-label="Ligar ou desligar o aquecedor" disabled></button>
  </div>
  <div class="linha"><label>Alvo · °C<small>PETG: 55, o máximo da caixa</small></label><input data-k="alvo" inputmode="decimal"></div>
  <div class="linha"><label>Temperatura máxima · °C<small id="tmaxFaixa"></small></label><input id="tmax" inputmode="decimal" placeholder="--"></div>
  <small class="nota">O aquecedor nunca liga sozinho: começa desligado a cada boot. A máxima aparece em verde ao lado da temperatura. Salva ao sair do campo.</small>
</div></div>

<div class="fundo" id="m-h" hidden><div class="modal">
  <div class="topo"><b>Aquecedor · ajustes</b><button data-fecha>FECHAR</button></div>
  <div class="linha"><label>Proteção · min<small>corta se ficar no máximo esse tempo sem subir 1 °C (5 a 60)</small></label><input data-k="subir" inputmode="numeric"></div>
  <div class="sub">Ajuste do PID</div>
  <div class="linha"><label>Kp · %/°C<small>potência por grau abaixo do alvo</small></label><input data-k="kp" inputmode="decimal"></div>
  <div class="linha"><label>Ki · %/°C·s<small>corrige o erro que sobra com o tempo</small></label><input data-k="ki" inputmode="decimal"></div>
  <div class="linha"><label>Kd · %·s/°C<small>freia a chegada ao alvo</small></label><input data-k="kd" inputmode="decimal"></div>
  <small class="nota">Ligar, desligar e o alvo ficam no cartão da temperatura. Passou do alvo e oscila: baixe o Ki. Demora a chegar ou fica abaixo: suba o Kp. Salva ao sair do campo.</small>
</div></div>

<div class="fundo" id="m-d" hidden><div class="modal">
  <div class="topo"><b>Display</b><button data-fecha>FECHAR</button></div>
  <div class="brilho" style="margin:0;padding:0;border:0">
    <label for="bri">Brilho</label>
    <input type="range" id="bri" min="5" max="100" step="5" value="60">
    <output id="briV">--</output>
  </div>
  <small class="nota">Ligar e apagar ficam no interruptor ao lado do ícone. Apagar desliga também a luz de fundo.</small>
</div></div>

<div class="fundo" id="m-f2" hidden><div class="modal">
  <div class="topo"><b>Fan 1 e Fan 2 · circulação</b><button data-fecha>FECHAR</button></div>
  <div class="chave">
    <div><span id="estadoF2" class="estado">—</span><small id="f2agora"></small></div>
    <button id="btnF2" disabled>—</button>
  </div>
  <div class="linha"><label>Máxima · %<small>com o aquecedor ligado</small></label><input data-k="f2max" inputmode="numeric"></div>
  <div class="linha"><label>Mínima · %<small>com o aquecedor desligado</small></label><input data-k="f2min" inputmode="numeric"></div>
  <div class="linha"><label>Piso de PWM · %<small>vale para todos os ventiladores</small></label><input data-k="pwmin" inputmode="numeric"></div>
  <small class="nota">Começa desligado a cada boot. Cada campo salva ao sair dele.</small>
</div></div>

<div class="fundo" id="m-f1" hidden><div class="modal">
  <div class="topo"><b>Fan · aquecedor</b><button data-fecha>FECHAR</button></div>
  <div class="chave">
    <div><span id="estadoF1" class="estado">—</span><small>automático: segue o aquecedor</small></div>
  </div>
  <div class="linha"><label>A 100% depois de desligar · min<small>o aquecedor ainda está quente</small></label><input data-k="pos1" data-esc="60" inputmode="decimal"></div>
  <div class="linha"><label>Rampa até parar · min</label><input data-k="rmp1" data-esc="60" inputmode="decimal"></div>
  <div class="linha"><label>Piso de PWM · %<small>vale para todos os ventiladores</small></label><input data-k="pwmin" inputmode="numeric"></div>
  <small class="nota">100% enquanto aquece. Cada campo salva ao sair dele.</small>
</div></div>

<script>
var L = 320, A = 150;
function $(id) { return document.getElementById(id); }
function todos(sel) { return document.querySelectorAll(sel); }

// --- grafico -------------------------------------------------------------------
// Area de desenho dentro do viewBox 320x156; a temperatura a esquerda, a
// umidade a direita. Cada ponto e 15 s e a janela tem 240 pontos (1 h): o
// eixo x e o tempo, entao com menos de 1 h de dados a linha comeca no meio.
var GX0 = 34, GX1 = 286, GY0 = 10, GY1 = 140, NPTS = 240;
function escala(v) {
  var mn = Math.min.apply(null, v), mx = Math.max.apply(null, v);
  return isFinite(mn) ? [mn, mx] : null;
}
function caminho(v, mn, mx) {
  var s = "", n = v.length, i, x, y;
  for (i = 0; i < n; i++) {
    x = GX1 - (n - 1 - i) * (GX1 - GX0) / (NPTS - 1);
    y = GY1 - (v[i] - mn) / (mx - mn) * (GY1 - GY0);
    s += (i ? "L" : "M") + x.toFixed(1) + "," + y.toFixed(1);
  }
  return s;
}
function grafico(d) {
  var t = d.hist.t, u = d.hist.u, et = escala(t), eu = escala(u);
  // Temperatura: do menor ao maior da ultima hora, com 1 C de folga e no
  // minimo 4 C de altura. Umidade: 2 % de folga, minimo 10 %, dentro de 0-100.
  var tMin = et ? Math.floor(et[0]) - 1 : 20, tMax = et ? Math.ceil(et[1]) + 1 : 30;
  if (tMax - tMin < 4) tMax = tMin + 4;
  var uMin = eu ? Math.max(0, Math.floor(eu[0]) - 2) : 40;
  var uMax = eu ? Math.min(100, Math.ceil(eu[1]) + 2) : 60;
  if (uMax - uMin < 10) { uMax = Math.min(100, uMin + 10); uMin = uMax - 10; }

  var g = "", k, y;
  for (k = 0; k <= 3; k++) {
    y = GY0 + k * (GY1 - GY0) / 3;
    g += '<line x1="' + GX0 + '" x2="' + GX1 + '" y1="' + y + '" y2="' + y +
         '" stroke="#262322" stroke-dasharray="' + (k === 3 ? "" : "2 4") + '"/>';
    g += '<text x="' + (GX0 - 6) + '" y="' + (y + 3) + '" text-anchor="end" fill="#c9875a">' +
         (tMax - k * (tMax - tMin) / 3).toFixed(0) + '°</text>';
    g += '<text x="' + (GX1 + 6) + '" y="' + (y + 3) + '" fill="#4fa7bd">' +
         (uMax - k * (uMax - uMin) / 3).toFixed(0) + '%</text>';
  }
  $("grade").innerHTML = g;

  var pt = t.length ? caminho(t, tMin, tMax) : "";
  $("lt").setAttribute("d", pt);
  $("at").setAttribute("d", t.length > 1 ? pt + "L" + GX1 + "," + GY1 + "L" +
    (GX1 - (t.length - 1) * (GX1 - GX0) / (NPTS - 1)).toFixed(1) + "," + GY1 + "Z" : "");
  $("lu").setAttribute("d", u.length ? caminho(u, uMin, uMax) : "");

  function ponto(id, v, mn, mx) {
    var c = $(id);
    if (!v.length) { c.setAttribute("r", 0); return; }
    c.setAttribute("cx", GX1);
    c.setAttribute("cy", GY1 - (v[v.length - 1] - mn) / (mx - mn) * (GY1 - GY0));
  }
  ponto("pt", t, tMin, tMax);
  ponto("pu", u, uMin, uMax);
  $("legT").textContent = d.sensor ? d.t.toFixed(1) + " °C" : "--";
  $("legU").textContent = d.sensor ? d.u.toFixed(0) + " %" : "--";
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

// Interruptor: a bolinha a direita e verde quando ligado.
function interruptor(b, ligado) {
  b.disabled = false;
  b.dataset.on = ligado ? "1" : "0";
  b.classList.toggle("on", ligado);
  b.setAttribute("aria-checked", ligado ? "true" : "false");
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
  b.addEventListener("click", function (ev) {
    ev.stopPropagation();  // o do cartao nao abre o modal
    this.disabled = true;
    envia("autorizar?v=" + (this.dataset.on === "1" ? 0 : 1));
  });
});

// Estado num cartao claro: bolinha verde (ativo) ou laranja (aquecendo);
// parado vai sem bolinha, numa pilula vermelha.
function st(el, classe, texto) {
  el.textContent = classe ? "● " + texto : texto;
  el.className = "st " + (classe || "parado");
}
$("btnF2").addEventListener("click", function () {
  this.disabled = true;
  envia("config?f2on=" + (this.dataset.on === "1" ? 0 : 1));
});
$("swTela").addEventListener("click", function () {
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
document.getElementById("testaAviso").addEventListener("click", function (e) {
  e.preventDefault();
  var a = this;
  fetch("aviso-teste", { method: "POST" }).then(function (r) {
    a.textContent = r.ok ? "AVISO ENVIADO" : "AVISOS DESLIGADOS";
  }).catch(function () { a.textContent = "SEM RESPOSTA"; });
});

document.getElementById("limpaHist").addEventListener("click", function (e) {
  e.preventDefault();
  if (!confirm("Apagar todas as medidas gravadas? O log fica. Não dá para desfazer.")) return;
  var a = this;
  fetch("historico-limpar", { method: "POST" }).then(function (r) {
    a.textContent = r.ok ? "HISTÓRICO APAGADO" : "NÃO APAGOU";
  }).catch(function () { a.textContent = "SEM RESPOSTA"; });
});

document.getElementById("limpaLog").addEventListener("click", function (e) {
  e.preventDefault();
  if (!confirm("Apagar todo o log? As medidas ficam. Não dá para desfazer.")) return;
  var a = this;
  fetch("log-limpar", { method: "POST" }).then(function (r) {
    a.textContent = r.ok ? "LOG APAGADO" : "NÃO APAGOU";
  }).catch(function () { a.textContent = "SEM RESPOSTA"; });
});

function ciclo() {
  return fetch("dados", { cache: "no-store" }).then(function (r) {
    return r.json();
  }).then(function (d) {
    var e = $("estado");

    $("conexao").className = "conexao";
    $("conexaoTxt").textContent = "conectado";
    $("c-t").classList.toggle("sem", !d.sensor);
    $("c-u").classList.toggle("sem", !d.sensor);
    $("c-ua").classList.toggle("sem", !d.sensor);
    $("t").textContent = d.sensor ? d.t.toFixed(1) : "--";
    $("u").textContent = d.sensor ? d.u.toFixed(1) : "--";
    $("ua").textContent = d.sensor ? d.ua.toFixed(1) : "--";

    e.textContent = d.sensor
      ? "SENSOR OK · " + d.n + " AMOSTRAS · LIGADO HÁ " + Math.floor(d.ligado / 60) + " MIN"
      : "SENSOR NÃO ENCONTRADO";
    e.className = d.sensor ? "" : "ruim";

    grafico(d);

    // Aquecedor: o estado e o botao aparecem no cartao e no modal.
    var classeAq = d.estado === "PROTECAO" ? "falha" : d.aquecendo ? "quente" : "";
    var textoAq = d.estado === "AQUECENDO" ? "AQUECENDO · " + d.pot + " %" : d.estado;
    todos("[data-estado-aq]").forEach(function (s) { marca(s, textoAq, classeAq); });
    todos("[data-autoriza]").forEach(function (b) { interruptor(b, d.autorizado); });
    $("motivo").textContent = d.motivo || "";
    $("potCard").textContent = d.autorizado ? d.pot : "0";
    if (d.estado === "PROTECAO") st($("stAq"), "", "PROTEÇÃO");
    else if (d.estado === "AQUECENDO") st($("stAq"), "quente", "AQUECENDO");
    else if (d.autorizado) st($("stAq"), "ativo", "NO ALVO");
    else st($("stAq"), "", "DESLIGADO");
    $("alvoCard").textContent = d.cfg.alvo.toFixed(0);

    // Campos: nao sobrescreve o que esta sendo digitado.
    todos("input[data-k]").forEach(function (i) {
      if (document.activeElement === i) return;
      var esc = Number(i.dataset.esc || 1);
      var v = d.cfg[i.dataset.k];
      i.value = esc > 1 ? String(Math.round(v / esc * 10) / 10) : v;
    });

    // Temperatura maxima: verde quando definida, como no display.
    var temTmax = d.tmax !== null;
    $("tmaxCard").className = "lado" + (temTmax ? " cfg" : "");
    $("tmaxV").textContent = temTmax ? d.tmax.toFixed(1) + "°" : "--";
    if (document.activeElement !== $("tmax")) $("tmax").value = temTmax ? d.tmax : "";
    $("tmaxFaixa").textContent = "de " + d.tmaxMin + " a " + d.teto + " °C";

    // Ventiladores
    // Icones: chama acesa so aquecendo; helice verde girando quando ligada,
    // mais rapido quanto maior a velocidade (de ~1,6 s a ~0,35 s por volta).
    $("iAq").classList.toggle("acesa", d.estado === "AQUECENDO");
    [["iF1", d.fan1], ["iF2", d.fan2], ["iF3", d.fan2]].forEach(function (h) {
      var el = $(h[0]), on = h[1] > 0;
      el.classList.toggle("gira", on);
      if (on) el.style.setProperty("--dur", (1.6 - h[1] / 100 * 1.25).toFixed(2) + "s");
    });

    // Os dois de circulacao saem do mesmo canal: mesmo valor nos dois cartoes.
    $("f1").textContent = d.fan1;
    $("f2").textContent = d.fan2;
    $("f3").textContent = d.fan2;
    st($("stF1"), d.fan1 > 0 ? "ativo" : "", d.fan1 > 0 ? "ATIVO" : "INATIVO");
    st($("stF2"), d.fan2 > 0 ? "ativo" : "", d.fan2 > 0 ? "ATIVO" : "INATIVO");
    st($("stF3"), d.fan2 > 0 ? "ativo" : "", d.fan2 > 0 ? "ATIVO" : "INATIVO");
    marca($("estadoF1"), d.fan1 > 0 ? "LIGADO · " + d.fan1 + " %" : "DESLIGADO", d.fan1 > 0 ? "on" : "");

    var f2 = d.cfg.f2on;
    marca($("estadoF2"), f2 ? "LIGADO · " + d.fan2 + " %" : "DESLIGADO", f2 ? "on" : "");
    $("f2agora").textContent = f2
      ? (d.aquecendo ? "na máxima: aquecedor ligado" : "na mínima: aquecedor desligado")
      : "ligue aqui para começar a girar";
    chave($("btnF2"), f2, "DESLIGAR", "LIGAR");

    // Imagem do display e brilho
    interruptor($("swTela"), d.tela);
    if (!bri._mexendo && document.activeElement !== bri) {
      bri.value = d.brilho;
      pintaBrilho(d.brilho);
    }
  }).catch(function () {
    var e = $("estado");
    e.textContent = "SEM RESPOSTA DA PLACA";
    e.className = "ruim";
    $("conexao").className = "conexao ruim";
    $("conexaoTxt").textContent = "sem resposta";
  });
}

// Relogio do topo: a hora do celular.
function relogio() {
  $("relogio").textContent = new Date().toLocaleTimeString("pt-BR",
    { hour: "2-digit", minute: "2-digit" });
}
relogio();
setInterval(relogio, 10000);

ciclo();
setInterval(ciclo, 2000);
</script>
)HTML";

// ---------------------------------------------------------------------------
// Pagina da rede propria (ESTUFA-SETUP): so escolhe a rede de casa. O painel
// nao e servido por aqui - quem conecta nesta rede nao autoriza aquecedor.
// ---------------------------------------------------------------------------
static const char PAGINA_PORTAL[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Estufa - escolher rede</title>
<style>
:root{--bg:#080b10;--card:#111720;--borda:#202b38;--texto:#f3f6fa;--mudo:#8290a1;
  --ciano:#42d7f5;--laranja:#ff9b52;--verde:#55e0a0;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
*{box-sizing:border-box}
body{margin:0 auto;max-width:430px;padding:18px 14px 28px;background:var(--bg);
  color:var(--texto);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
h1{font-size:20px;margin:0 0 4px}
p{color:var(--mudo);font-size:13.5px;margin:0 0 16px}
.rotulo{color:var(--mudo);font:800 9px var(--mono);letter-spacing:1.5px;
  text-transform:uppercase;margin:0 0 8px}
.lista{border:1px solid var(--borda);border-radius:14px;background:var(--card);
  overflow:hidden;margin-bottom:16px}
.rede{display:flex;align-items:center;justify-content:space-between;gap:10px;
  padding:13px 14px;border-bottom:1px solid var(--borda);cursor:pointer}
.rede:last-child{border-bottom:none}
.rede.sel{background:#16202c}
.rede small{color:var(--mudo);font:650 11px var(--mono)}
.vazio{padding:14px;color:var(--mudo);font-size:13px}
.form{border:1px solid var(--borda);border-radius:14px;background:var(--card);padding:14px}
label{display:block;color:var(--mudo);font-size:12.5px;margin:0 0 5px}
input{width:100%;background:var(--bg);color:var(--texto);border:1px solid var(--borda);
  border-radius:9px;padding:10px 11px;font:600 15px var(--mono);margin-bottom:12px}
button{width:100%;appearance:none;border:1px solid var(--verde);background:var(--bg);
  color:var(--verde);font:800 12px var(--mono);letter-spacing:1.2px;padding:12px;
  border-radius:9px;cursor:pointer}
button.sec{border-color:var(--borda);color:var(--mudo);margin-bottom:16px}
#msg{margin-top:12px;font:650 12px var(--mono);color:var(--mudo)}
#msg.ok{color:var(--verde)} #msg.erro{color:var(--laranja)}
</style>

<h1>Escolher a rede</h1>
<p>A estufa não encontrou a rede <b id="atual">—</b>. Escolha a rede da casa e
digite a senha. Ela continua medindo e controlando normalmente enquanto isso.</p>

<div class="rotulo">Redes encontradas</div>
<div class="lista" id="lista"><div class="vazio">procurando…</div></div>
<button class="sec" id="denovo">PROCURAR DE NOVO</button>

<div class="form">
  <label for="ssid">Nome da rede</label>
  <input id="ssid" maxlength="32" autocomplete="off" autocapitalize="off">
  <label for="senha">Senha</label>
  <input id="senha" type="password" maxlength="63" autocomplete="off">
  <button id="salvar">CONECTAR</button>
  <div id="msg"></div>
</div>

<script>
function barras(r) { return r > -55 ? "▂▄▆█" : r > -67 ? "▂▄▆" : r > -78 ? "▂▄" : "▂"; }

function carrega() {
  fetch("redes", { cache: "no-store" }).then(function (r) { return r.json(); })
  .then(function (d) {
    document.getElementById("atual").textContent = d.atual || "—";
    var l = document.getElementById("lista");
    if (!d.redes.length) {
      l.innerHTML = '<div class="vazio">' + (d.procurando ? "procurando…" : "nenhuma rede encontrada") + "</div>";
      if (d.procurando) setTimeout(carrega, 2000);
      return;
    }
    l.innerHTML = "";
    d.redes.forEach(function (n) {
      var e = document.createElement("div");
      e.className = "rede";
      var nome = document.createElement("span");
      nome.textContent = n.ssid;
      var info = document.createElement("small");
      info.textContent = (n.aberta ? "aberta  " : "") + barras(n.rssi);
      e.appendChild(nome); e.appendChild(info);
      e.onclick = function () {
        document.querySelectorAll(".rede").forEach(function (x) { x.classList.remove("sel"); });
        e.classList.add("sel");
        document.getElementById("ssid").value = n.ssid;
        document.getElementById("senha").focus();
      };
      l.appendChild(e);
    });
  }).catch(function () { setTimeout(carrega, 3000); });
}

document.getElementById("denovo").onclick = function () {
  document.getElementById("lista").innerHTML = '<div class="vazio">procurando…</div>';
  setTimeout(carrega, 4000);
};

document.getElementById("salvar").onclick = function () {
  var m = document.getElementById("msg");
  var ssid = document.getElementById("ssid").value.trim();
  var senha = document.getElementById("senha").value;
  if (!ssid) { m.className = "erro"; m.textContent = "escolha ou digite o nome da rede"; return; }
  if (senha && senha.length < 8) { m.className = "erro"; m.textContent = "a senha precisa de pelo menos 8 caracteres"; return; }
  m.className = ""; m.textContent = "salvando…";
  var corpo = "ssid=" + encodeURIComponent(ssid) + "&senha=" + encodeURIComponent(senha);
  fetch("rede", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" }, body: corpo })
  .then(function (r) { return r.ok ? r.json() : r.text().then(function (t) { throw t; }); })
  .then(function () {
    m.className = "ok";
    m.textContent = "Conectando em " + ssid + ". Se a senha estiver certa, esta rede vai sumir em alguns segundos e a estufa volta em http://estufa.local";
  }).catch(function (t) { m.className = "erro"; m.textContent = typeof t === "string" ? t : "sem resposta da estufa"; });
};

carrega();
</script>
)HTML";

