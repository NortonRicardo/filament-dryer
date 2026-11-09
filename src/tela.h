// ---------------------------------------------------------------------------
// Layout da tela, seguindo o desenho THERMO.
//
// Paleta e medidas vindas do mockup, convertidas para RGB565. O erro da
// conversao e de no maximo 5/255 por canal - imperceptivel.
//
// Display de 4" 320x480 em retrato (ST7796S). Os cinco cartoes sao os mesmos
// do painel de 2,4"; so cresceram para ocupar a tela.
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
static const uint16_t VERMELHO = 0xFB4D; // #ff6b6b
static const uint16_t RODAPE  = 0x5B2E;  // #586575
static const uint16_t TURQUESA = 0x5F5A; // #5eead4, umidade absoluta

// --- medidas ---------------------------------------------------------------
static const int16_t LARGURA = 320;
static const int16_t ALTURA  = 480;
static const int16_t MARGEM  = 10;
static const int16_t VAO     = 8;
static const int16_t COL_L   = (LARGURA - 2 * MARGEM - VAO) / 2;  // 146

// Rodape com o endereco do servidor, centrado na faixa de baixo.
static const int16_t ALTURA_RODAPE = 24;
static const int16_t Y_RODAPE = ALTURA - ALTURA_RODAPE / 2;  // 468

// Faixa de topo com o relogio, no canto direito.
static const int16_t ALTURA_TOPO = 20;
static const int16_t Y_TOPO = ALTURA_TOPO / 2;  // 10

// Tres linhas de cartoes iguais entre o topo e o rodape:
// (480 - topo - rodape - 3 vaos) / 3 = 137 px cada. A ultima linha termina
// em y 447, e o rodape fica sozinho na faixa de 456 a 480.
static const int16_t LINHA_A =
    (ALTURA - ALTURA_TOPO - ALTURA_RODAPE - 3 * VAO) / 3;  // 137
static const int16_t Y_L1 = ALTURA_TOPO;
static const int16_t Y_L2 = Y_L1 + LINHA_A + VAO;
static const int16_t Y_L3 = Y_L2 + LINHA_A + VAO;

// Um cartao do painel. O valor e desenhado por cima, na faixa reservada.
struct Cartao {
  int16_t x, y, l, a;
  const char *rotulo;
  const char *detalhe;
  const char *unidade;
  bool grau;      // desenha o anelzinho do grau antes da unidade
  uint16_t cor;   // cor do valor quando ha dado
};

static const uint8_t N_CARTOES = 6;

// Seis cartoes em tres linhas de dois:
//   umidade relativa | umidade absoluta
//   temperatura      | aquecedor
//   FAN 01           | FAN 02
// A ordem importa: os indices (enum abaixo) sao usados para atualizar cada
// valor.
static const int16_t COL_E = MARGEM;                // coluna da esquerda
static const int16_t COL_D = MARGEM + COL_L + VAO;  // coluna da direita

static const Cartao CARTOES[N_CARTOES] = {
  // A relativa depende da temperatura: so de aquecer, ela cai. A absoluta
  // so cai quando a agua sai da caixa - e ela que diz se esta secando.
  {COL_E, Y_L1, COL_L, LINHA_A, "UMIDADE REL.", "CAI SO DE AQUECER",
   "%", false, CIANO},
  {COL_D, Y_L1, COL_L, LINHA_A, "UMIDADE ABS.", "AGUA REAL NO AR",
   "g/m3", false, TURQUESA},

  // Sem detalhe fixo: a linha de baixo mostra a temperatura maxima
  // configurada no painel web, e e desenhada a parte (pintaMaximo no main.cpp).
  {COL_E, Y_L2, COL_L, LINHA_A, "TEMPERATURA", "",
   "C", true, LARANJA},
  // Sem detalhe fixo: a linha de baixo e o selo de estado (pintaControle).
  {COL_D, Y_L2, COL_L, LINHA_A, "AQUECEDOR",
   "", "C", true, LARANJA},

  // Sem detalhe fixo: a linha de baixo e o selo de estado (pintaControle).
  {COL_E, Y_L3, COL_L, LINHA_A, "FAN 01", "",
   "%", false, AZUL},
  {COL_D, Y_L3, COL_L, LINHA_A, "FAN 02", "",
   "%", false, ROXO},
};

// --- chamadas do servidor -----------------------------------------------------
// Implementadas no main.cpp. O servidor so chama: nao precisa conhecer a
// TFT_eSPI nem incluir nada dela.
//
// Desligar pinta de preto e apaga a luz de fundo (PWM no pino LED).
void telaDefineLigada(bool ligada);
bool telaEstaLigada();

// Brilho da luz de fundo, em %. Abaixo de 5 sobe para 5: para apagar de vez
// use telaDefineLigada(false). Salvo na memoria.
void telaDefineBrilho(uint8_t pct);
uint8_t telaBrilho();

// Atualizacao de firmware pelo Wi-Fi: tela de progresso e volta ao painel se
// ela falhar (no sucesso a placa reinicia sozinha).
void telaAtualizando(uint8_t pct);
void telaFimAtualizacao();

// Temperatura maxima configurada no painel web. Salva na memoria e mostrada
// em verde embaixo da temperatura. Ainda nao entra na regra do controle.
static const float TMAX_MIN = 20.0f;  // vai ate TETO_ABSOLUTO (controle.h)
bool painelTempMaxima(float &valor);  // false se nunca foi definida
void painelDefineTempMaxima(float valor);

enum {
  CARTAO_UMID = 0,
  CARTAO_UABS = 1,
  CARTAO_TEMP = 2,
  CARTAO_HOTEND = 3,
  CARTAO_FAN1 = 4,
  CARTAO_FAN2 = 5,
};
