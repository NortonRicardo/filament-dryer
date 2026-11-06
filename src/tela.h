// ---------------------------------------------------------------------------
// Layout da tela, seguindo o desenho THERMO.
//
// Paleta e medidas vindas do mockup, convertidas para RGB565. O erro da
// conversao e de no maximo 5/255 por canal - imperceptivel.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

// --- cores (o CSS original ao lado) ----------------------------------------
static const uint16_t FUNDO   = 0x0842;  // #080b10
static const uint16_t CARTAO  = 0x10A4;  // #111720
static const uint16_t BORDA   = 0x2147;  // #202b38
static const uint16_t TEXTO   = 0xF7BF;  // #f3f6fa
static const uint16_t MUDO    = 0x8494;  // #8290a1
static const uint16_t CIANO   = 0x46BE;  // #42d7f5
static const uint16_t AZUL    = 0x64BF;  // #6197ff
static const uint16_t LARANJA = 0xFCCA;  // #ff9b52
static const uint16_t VERDE   = 0x5714;  // #55e0a0
static const uint16_t ROXO    = 0xA45F;  // #a78bfa
static const uint16_t RODAPE  = 0x5B2E;  // #586575

// --- medidas ---------------------------------------------------------------
static const int16_t LARGURA = 240;
static const int16_t ALTURA  = 320;
static const int16_t MARGEM  = 10;
static const int16_t VAO     = 8;
static const int16_t COL_L   = (LARGURA - 2 * MARGEM - VAO) / 2;  // 106

static const int16_t Y_RODAPE = 308;

// Um cartao do painel. O valor e desenhado por cima, na faixa reservada.
struct Cartao {
  int16_t x, y, l, a;
  const char *rotulo;
  const char *detalhe;
  const char *unidade;
  bool grau;      // desenha o anelzinho do grau antes da unidade
  uint16_t cor;   // cor do valor quando ha dado
  bool grande;    // fonte maior e faixa mais alta (o cartao do hotend)
};

static const uint8_t N_CARTOES = 5;

// Sem o cabecalho, os cartoes ocupam a tela toda: 90 px de altura nas duas
// linhas de cima e 88 no de baixo, contra 76 e 80 de antes.
// A ordem importa: os indices sao usados para atualizar cada valor.
static const Cartao CARTOES[N_CARTOES] = {
  {MARGEM,               14, COL_L, 90, "TEMPERATURA", "AMBIENTE",
   "C", true, LARANJA, false},
  {MARGEM + COL_L + VAO, 14, COL_L, 90, "UMIDADE", "RELATIVA",
   "%", false, CIANO, false},

  {MARGEM,               112, COL_L, 90, "FAN 01", "12V \xC2\xB7 PWM",
   "%", false, AZUL, false},
  {MARGEM + COL_L + VAO, 112, COL_L, 90, "FAN 02", "12V \xC2\xB7 PWM",
   "%", false, ROXO, false},

  // Sem acento no "POTENCIA": as fontes da TFT_eSPI cobrem so 0x20 a 0x7E.
  {MARGEM, 210, LARGURA - 2 * MARGEM, 88, "HOTEND \xC2\xB7 POTENCIA", "PWM",
   "W", false, LARANJA, true},
};

// --- controle do painel ----------------------------------------------------
// Implementadas no main.cpp. O servidor so chama: nao precisa conhecer a
// TFT_eSPI nem incluir nada dela.
//
// Desliga a IMAGEM, nao a luz de fundo: este modulo nao expoe pino de
// backlight. A tela fica preta, mas ainda acesa.
void telaDefineLigada(bool ligada);
bool telaEstaLigada();

// Brilho pelos registradores do proprio controlador. Veja o comentario da
// implementacao no main.cpp: provavelmente nao faz nada neste painel, mas
// descobrir custa quatro bytes no barramento.
void telaDefineBrilho(uint8_t valor);
uint8_t telaBrilho();

enum {
  CARTAO_TEMP = 0,
  CARTAO_UMID = 1,
  CARTAO_FAN1 = 2,
  CARTAO_FAN2 = 3,
  CARTAO_HOTEND = 4,
};
