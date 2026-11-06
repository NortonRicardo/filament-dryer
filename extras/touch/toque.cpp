#include "toque.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

#include "tela.h"

extern TFT_eSPI tft;

static const char *NVS_NOME = "painel";
static const char *NVS_CAL  = "toque_cal";

// Pressao minima para contar como toque. E o mesmo limiar padrao da
// getTouch(): abaixo disso a leitura do resistivo e ruido.
static const uint16_t PRESSAO_MIN = 600;

// Quanto tempo sem contato ate considerar o dedo solto. O resistivo perde
// leituras isoladas no meio de um toque firme; sem esta folga, um toque
// viraria dois.
static const uint32_t SOLTO_APOS_MS = 80;

static bool dedoNaTela() { return tft.getTouchRawZ() > PRESSAO_MIN; }

// Diagnostico: mostra no monitor o que o XPT2046 responde, cru, antes de
// calibrar. A calibracao espera para sempre por um toque; se o chip nao
// responde, ela trava sem dizer por que. Isto diz.
//
// Como ler (valores de 0 a 4095):
//   x, y e z mudando ao tocar, z acima de ~175 -> o chip responde.
//   x=0 y=0 z=0 sempre     -> nada volta pelo T_DO: linha presa em 0.
//   x=4095 y=4095 z=0      -> linha presa em 1: T_DO solto ou chip mudo.
// (z sai 0 nos dois casos de linha presa: a biblioteca calcula z pela
// diferenca de duas leituras, e duas leituras iguais zeram.)
static void diagnostico() {
  tft.fillScreen(FUNDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXTO, FUNDO);
  tft.drawString("TOQUE E SEGURE", LARGURA / 2, ALTURA / 2 - 14, 4);
  tft.setTextColor(MUDO, FUNDO);
  tft.drawString("diagnostico do touch, 5 s", LARGURA / 2, ALTURA / 2 + 18, 2);

  Serial.println("Touch diagnostico: toque e segure a tela por 5 s");
  const uint32_t inicio = millis();
  while (millis() - inicio < 5000) {
    uint16_t x = 0, y = 0;
    tft.getTouchRaw(&x, &y);
    const uint16_t z = tft.getTouchRawZ();
    Serial.printf("  bruto x=%4u y=%4u z=%4u\n", x, y, z);
    delay(200);
  }
  Serial.println("Touch diagnostico: fim");
}

static void calibra(uint16_t cal[5]) {
  diagnostico();

  tft.fillScreen(FUNDO);
  tft.setTextDatum(MC_DATUM);

  // Se a calibracao foi forcada com o dedo na tela, espera soltar: senao o
  // primeiro canto seria marcado na hora, no lugar errado.
  if (dedoNaTela()) {
    tft.setTextColor(TEXTO, FUNDO);
    tft.drawString("SOLTE A TELA", LARGURA / 2, ALTURA / 2, 4);
    while (dedoNaTela()) delay(20);
    delay(300);
    tft.fillScreen(FUNDO);
  }

  tft.setTextColor(TEXTO, FUNDO);
  tft.drawString("CALIBRAR O TOQUE", LARGURA / 2, ALTURA / 2 - 30, 4);
  tft.setTextColor(MUDO, FUNDO);
  tft.drawString("Toque na ponta de cada seta,", LARGURA / 2, ALTURA / 2 + 10, 2);
  tft.drawString("de preferencia com a unha.", LARGURA / 2, ALTURA / 2 + 30, 2);

  // Desenha uma seta em cada canto, espera o toque em cada uma e devolve os
  // cinco numeros da calibracao. Ja aplica o resultado.
  tft.calibrateTouch(cal, LARANJA, FUNDO, 15);
}

void toqueInicia() {
  uint16_t cal[5];
  Preferences nvs;
  nvs.begin(NVS_NOME, false);

  const bool temSalva = nvs.getBytes(NVS_CAL, cal, sizeof cal) == sizeof cal;

  // Dedo na tela durante o boot forca recalibrar. E a saida para quando a
  // calibracao salva ficar ruim, sem precisar regravar o firmware.
  const bool forcada = dedoNaTela();

  if (temSalva && !forcada) {
    tft.setTouch(cal);
    Serial.println("Touch            : calibracao carregada da memoria");
  } else {
    Serial.println(forcada ? "Touch            : recalibrando (tela tocada no boot)"
                           : "Touch            : sem calibracao salva, calibrando");
    calibra(cal);
    nvs.putBytes(NVS_CAL, cal, sizeof cal);
    Serial.printf("Touch            : calibrado {%u, %u, %u, %u, %u}\n",
                  cal[0], cal[1], cal[2], cal[3], cal[4]);
  }

  nvs.end();
}

bool toqueNovo(uint16_t &x, uint16_t &y) {
  static bool pressionado = false;
  static uint32_t ultimoContato = 0;

  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty, PRESSAO_MIN)) {
    ultimoContato = millis();
    if (pressionado) return false;  // ainda e o mesmo toque

    pressionado = true;
    x = tx;
    y = ty;
    Serial.printf("Toque            : x=%u y=%u\n", tx, ty);
    return true;
  }

  if (pressionado && millis() - ultimoContato > SOLTO_APOS_MS) {
    pressionado = false;
  }
  return false;
}
