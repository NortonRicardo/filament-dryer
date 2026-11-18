// ---------------------------------------------------------------------------
// Layout da tela: o mesmo desenho do painel web (cartoes claros sobre fundo
// quase preto, estados com bolinha colorida).
//
// Cores convertidas para RGB565. O erro da conversao e de no maximo 5/255
// por canal - imperceptivel.
//
// Display de 4" 320x480 em retrato (ST7796S).
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

// --- cores (o CSS do painel web ao lado) -----------------------------------
// Fundo quase preto, cartoes claros com texto escuro, estados com bolinha.
static const uint16_t LARANJA   = 0xFCCA;  // #ff9b52, listra do portal no rodape
static const uint16_t FUNDO2    = 0x0841;  // #080808, fundo da tela
static const uint16_t CLARO     = 0xF79E;  // #f5f3f2, cartao
static const uint16_t ESCURO    = 0x10A2;  // #171515, valor
static const uint16_t ROTULO_C  = 0x39A6;  // #3a3737, rotulo do cartao
static const uint16_t UNID_C    = 0x6B4D;  // #6f6a69, unidade e detalhe
static const uint16_t SEMDADO_C = 0xA4F3;  // #a39e9d, "--" sem leitura
static const uint16_t OK_C      = 0x1509;  // #16a34a, ativo / maxima definida
static const uint16_t QUENTE_C  = 0xEAC1;  // #ea580c, aquecendo
static const uint16_t PARADO_C  = 0xD924;  // #dc2626, parado / protecao
static const uint16_t TOPO_C    = 0xF79E;  // #f5f3f2, relogio
static const uint16_t TITULO_C  = 0xAD34;  // #aaa6a5, titulo e rodape
static const uint16_t CONECT_C  = 0x3ED1;  // #38d98a, conectado
static const uint16_t SEMREDE_C = 0xE2CB;  // #e45b5b, sem rede
static const uint16_t DIVISOR_C = 0xDEDB;  // #dedbda, linha que divide o cartao
static const uint16_t GOTA_C    = 0x0D3D;  // #0ea5e9, gota da umidade
static const uint16_t LARANJA_C = 0xFAE2;  // #f97316, miolo da chama
static const uint16_t AMARELO_C = 0xFE62;  // #facc15, centro da chama

// --- medidas ---------------------------------------------------------------
static const int16_t LARGURA = 320;
static const int16_t ALTURA  = 480;
static const int16_t MARGEM  = 10;
static const int16_t VAO     = 8;
static const int16_t COL_L   = (LARGURA - 2 * MARGEM - VAO) / 2;  // 146
static const int16_t LARGURA_TODA = LARGURA - 2 * MARGEM;         // 300

// Rodape com o endereco do servidor, centrado na faixa de baixo.
static const int16_t ALTURA_RODAPE = 24;
static const int16_t Y_RODAPE = ALTURA - ALTURA_RODAPE / 2;  // 468

// Topo: relogio no centro e a conexao a direita.
static const int16_t ALTURA_TOPO = 30;
static const int16_t Y_TOPO = 15;

// Quatro linhas de cartoes:
//   temperatura (largura toda)
//   umidade relativa | umidade absoluta
//   aquecedor | fan do aquecedor   (um cartao so, dividido ao meio)
//   fan 1 | fan 2
static const int16_t Y_L1 = 36;
static const int16_t A_L1 = 92;
static const int16_t Y_L2 = Y_L1 + A_L1 + VAO;   // 136
static const int16_t A_L2 = 88;
static const int16_t Y_L3 = Y_L2 + A_L2 + VAO;   // 232
static const int16_t A_L3 = 112;
static const int16_t Y_L4 = Y_L3 + A_L3 + VAO;   // 352
static const int16_t A_L4 = 96;                  // termina em 448

// Um cartao do painel. O valor e desenhado por cima, na faixa reservada.
struct Cartao {
  int16_t x, y, l, a;
  const char *rotulo;
  const char *unidade;
  bool icone;     // icone animado a esquerda; rotulo, valor e estado a direita.
                  // Sem icone, rotulo e valor centrados.
};

static const uint8_t N_CARTOES = 7;

static const int16_t COL_E = MARGEM;                // coluna da esquerda
static const int16_t COL_D = MARGEM + COL_L + VAO;  // coluna da direita

static const Cartao CARTOES[N_CARTOES] = {
  // Valor a esquerda; a direita, um sobre o outro, o alvo do aquecedor e a
  // maxima (desenhaAlvo e pintaMaximo no main).
  {COL_E, Y_L1, LARGURA_TODA, A_L1, "Temperatura", "\xC2\xB0" "C", false},
  // Rotulo com o icone de gota antes (desenhaCartao).
  {COL_E, Y_L2, COL_L, A_L2, "Relativa", "%", false},
  {COL_D, Y_L2, COL_L, A_L2, "Absoluta", "g/m\xC2\xB3", false},
  // Aquecedor e fan do aquecedor: um cartao so, dividido ao meio. Cada
  // metade ocupa a posicao de uma coluna; o fundo e desenhado inteiro.
  // Chama (aquecedor) e helices (fans) animadas a esquerda.
  {COL_E, Y_L3, COL_L, A_L3, "Aquecedor", "%", true},
  {COL_D, Y_L3, COL_L, A_L3, "Fan aquecedor", "%", true},
  {COL_E, Y_L4, COL_L, A_L4, "Fan 1", "%", true},
  {COL_D, Y_L4, COL_L, A_L4, "Fan 2", "%", true},
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
  CARTAO_TEMP = 0,
  CARTAO_UMID = 1,
  CARTAO_UABS = 2,
  CARTAO_POT = 3,     // potencia do aquecedor
  CARTAO_FANAQ = 4,   // ventilador do aquecedor
  CARTAO_CIRC1 = 5,
  CARTAO_CIRC2 = 6,
};
