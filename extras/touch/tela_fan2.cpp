#include "tela_fan2.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "controle.h"
#include "tela.h"

extern TFT_eSPI tft;

// --- layout (320x480, retrato) ---------------------------------------------
//
//   FAN 02 / CIRCULACAO           y  26 .. 50
//   [ LIGADO / DESLIGADO ]        y  66 .. 130
//   [ MAXIMA       100 % ]        y 142 .. 242
//   [ MINIMA        70 % ]        y 252 .. 352
//   agora: ...                    y 374
//   [ VOLTAR ]                    y 400 .. 466
struct Area { int16_t x, y, l, a; };

static const Area BOTAO_LIGA = {10,  66, LARGURA - 20, 64};
static const Area CARTAO_MAX = {10, 142, LARGURA - 20, 100};
static const Area CARTAO_MIN = {10, 252, LARGURA - 20, 100};
static const Area BOTAO_VOLTA = {10, 400, LARGURA - 20, 66};
static const int16_t Y_AGORA = 374;

static bool dentro(const Area &a, uint16_t x, uint16_t y) {
  return x >= a.x && x < a.x + a.l && y >= a.y && y < a.y + a.a;
}

// Moldura com borda de 2 px quando destacada.
static void moldura(const Area &a, uint16_t borda, bool grossa) {
  tft.fillRoundRect(a.x, a.y, a.l, a.a, 9, CARTAO);
  tft.drawRoundRect(a.x, a.y, a.l, a.a, 9, borda);
  if (grossa) tft.drawRoundRect(a.x + 1, a.y + 1, a.l - 2, a.a - 2, 8, borda);
}

static void desenhaLiga() {
  const bool ligado = controleConfig().fan2Ligado;
  const uint16_t cor = ligado ? VERDE : MUDO;

  moldura(BOTAO_LIGA, cor, ligado);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(cor, CARTAO);
  tft.drawString(ligado ? "LIGADO" : "DESLIGADO",
                 LARGURA / 2, BOTAO_LIGA.y + 24);

  tft.setTextFont(2);
  tft.setTextColor(MUDO, CARTAO);
  tft.drawString(ligado ? "toque para desligar" : "toque para ligar",
                 LARGURA / 2, BOTAO_LIGA.y + 48);
}

// Um cartao de velocidade. O que esta valendo agora ganha borda na cor do
// FAN 02; o outro fica neutro. Com o ventilador desligado, nenhum vale.
static void desenhaFaixa(const Area &a, const char *rotulo, const char *quando,
                         uint8_t pct, bool valendo) {
  moldura(a, valendo ? ROXO : BORDA, valendo);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(valendo ? TEXTO : MUDO, CARTAO);
  tft.drawString(rotulo, LARGURA / 2, a.y + 16);

  // Numero grande mais o "%", centrados juntos.
  char buf[8];
  snprintf(buf, sizeof buf, "%u", pct);
  tft.setFreeFont(&FreeSansBold24pt7b);
  const int16_t lNum = tft.textWidth(buf);
  tft.setTextFont(4);
  const int16_t lPct = tft.textWidth("%");
  const int16_t x = LARGURA / 2 - (lNum + 4 + lPct) / 2;
  const int16_t y = a.y + 52;

  tft.setTextDatum(ML_DATUM);
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextColor(ROXO, CARTAO);
  tft.drawString(buf, x, y);
  tft.setTextFont(4);
  tft.setTextColor(MUDO, CARTAO);
  tft.drawString("%", x + lNum + 4, y);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(MUDO, CARTAO);
  tft.drawString(quando, LARGURA / 2, a.y + a.a - 14);
}

static void desenhaAgora() {
  char buf[48];
  const Config c = controleConfig();

  if (!c.fan2Ligado) {
    snprintf(buf, sizeof buf, "agora: desligado, 0 %%");
  } else {
    snprintf(buf, sizeof buf, "agora: %u %%, aquecedor %s",
             controleVelocidade(1),
             controleAquecedorLigado() ? "ligado" : "desligado");
  }

  tft.fillRect(0, Y_AGORA - 9, LARGURA, 18, FUNDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(MUDO, FUNDO);
  tft.drawString(buf, LARGURA / 2, Y_AGORA);
}

static void desenhaVolta() {
  moldura(BOTAO_VOLTA, BORDA, false);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TEXTO, CARTAO);
  tft.drawString("VOLTAR", LARGURA / 2, BOTAO_VOLTA.y + BOTAO_VOLTA.a / 2);
}

void fan2Atualiza() {
  const Config c = controleConfig();
  const bool aquecendo = controleAquecedorLigado();

  desenhaFaixa(CARTAO_MAX, "MAXIMA", "COM O AQUECEDOR LIGADO",
               c.fan2Max, c.fan2Ligado && aquecendo);
  desenhaFaixa(CARTAO_MIN, "MINIMA", "COM O AQUECEDOR DESLIGADO",
               c.fan2Min, c.fan2Ligado && !aquecendo);
  desenhaAgora();
}

void fan2Abre() {
  tft.fillScreen(FUNDO);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TEXTO, FUNDO);
  tft.drawString("FAN 02", LARGURA / 2, 26);
  tft.setTextFont(2);
  tft.setTextColor(MUDO, FUNDO);
  tft.drawString("CIRCULACAO", LARGURA / 2, 50);

  desenhaLiga();
  fan2Atualiza();
  desenhaVolta();
}

ResultadoFan2 fan2Toque(uint16_t x, uint16_t y) {
  if (dentro(BOTAO_VOLTA, x, y)) return FAN2_VOLTA;
  if (dentro(CARTAO_MAX, x, y))  return FAN2_EDITA_MAX;
  if (dentro(CARTAO_MIN, x, y))  return FAN2_EDITA_MIN;

  if (dentro(BOTAO_LIGA, x, y)) {
    Config c = controleConfig();
    c.fan2Ligado = !c.fan2Ligado;

    const char *porque;
    if (!controleDefineConfig(c, &porque)) {
      Serial.printf("FAN 02           : nao salvou (%s)\n", porque);
    }

    desenhaLiga();
    fan2Atualiza();
  }
  return FAN2_SEGUE;
}
