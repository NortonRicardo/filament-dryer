#include "teclado.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "tela.h"

extern TFT_eSPI tft;

// --- layout (320x480, retrato) ---------------------------------------------
//
//   titulo                      y  28
//   visor com o valor           y  50 .. 130
//   faixa permitida / erro      y 148
//   4 linhas de 3 teclas        y 166 ..
//   CANCELAR | OK               .. y 468
static const int16_t Y_TITULO = 28;
static const int16_t VISOR_Y = 50, VISOR_A = 80;
static const int16_t Y_AVISO = 148;

static const int16_t TECLA_X0 = 11;
static const int16_t TECLA_Y0 = 166;
static const int16_t TECLA_L = 94;   // 3 x 94 + 2 x 8 = 298
static const int16_t TECLA_A = 54;   // 5 x 54 + 4 x 8 = 302
static const int16_t TECLA_VAO = 8;

static const uint8_t N_TECLAS = 14;
static const uint8_t T_PONTO = 9, T_ZERO = 10, T_APAGA = 11;
static const uint8_t T_CANCELA = 12, T_OK = 13;

static const char *ROTULOS[N_TECLAS] = {
  "1", "2", "3",
  "4", "5", "6",
  "7", "8", "9",
  ".", "0", "APAGAR",
  "CANCELAR", "OK",
};

// --- estado ---------------------------------------------------------------
static char texto[6] = "";   // o que foi digitado, ex.: "45.5"
static const char *titulo = "";
static float minimo = 0, maximo = 0;
static float valorAtual = 0;
static bool temValor = false;
static const char *unidade = "";
static bool grau = false;
static bool decimal = true;
static uint8_t maxInteiros = 2;

// --- geometria -------------------------------------------------------------
static void retangulo(uint8_t i, int16_t &x, int16_t &y, int16_t &l, int16_t &a) {
  a = TECLA_A;
  if (i < 12) {
    x = TECLA_X0 + (i % 3) * (TECLA_L + TECLA_VAO);
    y = TECLA_Y0 + (i / 3) * (TECLA_A + TECLA_VAO);
    l = TECLA_L;
  } else {
    // A ultima linha tem so duas teclas, cada uma com metade da largura.
    l = (3 * TECLA_L + 2 * TECLA_VAO - TECLA_VAO) / 2;
    x = TECLA_X0 + (i - 12) * (l + TECLA_VAO);
    y = TECLA_Y0 + 4 * (TECLA_A + TECLA_VAO);
  }
}

static int8_t teclaEm(uint16_t px, uint16_t py) {
  for (uint8_t i = 0; i < N_TECLAS; i++) {
    int16_t x, y, l, a;
    retangulo(i, x, y, l, a);
    if (px >= x && px < x + l && py >= y && py < y + a) return i;
  }
  return -1;
}

// --- desenho ---------------------------------------------------------------
static void desenhaTecla(uint8_t i, bool apertada) {
  int16_t x, y, l, a;
  retangulo(i, x, y, l, a);

  const bool ok = i == T_OK;
  const bool inativa = i == T_PONTO && !decimal;
  const uint16_t fundo = apertada ? BORDA : CARTAO;

  tft.fillRoundRect(x, y, l, a, 9, fundo);
  tft.drawRoundRect(x, y, l, a, 9, ok ? VERDE : BORDA);

  tft.setTextDatum(MC_DATUM);
  uint16_t cor = ok ? VERDE : (i >= T_APAGA ? MUDO : TEXTO);
  if (inativa) cor = BORDA;  // ponto sem uso neste valor: quase invisivel
  tft.setTextColor(cor, fundo);

  // Digitos, ponto e OK na fonte grande; as palavras em fonte menor, para
  // caber na tecla.
  if (i == T_APAGA || i == T_CANCELA) {
    tft.setTextFont(2);
    tft.drawString(ROTULOS[i], x + l / 2, y + a / 2);
  } else {
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.drawString(ROTULOS[i], x + l / 2, y + a / 2 - 2);
  }
}

// O visor mostra o que foi digitado. Antes da primeira tecla, mostra o
// valor atual apagado, so como referencia.
static void desenhaVisor() {
  tft.fillRoundRect(10, VISOR_Y, LARGURA - 20, VISOR_A, 9, CARTAO);
  tft.drawRoundRect(10, VISOR_Y, LARGURA - 20, VISOR_A, 9, BORDA);

  char ref[8];
  const char *mostra = texto;
  uint16_t cor = TEXTO;

  if (texto[0] == '\0') {
    cor = MUDO;
    if (temValor) {
      snprintf(ref, sizeof ref, decimal ? "%.1f" : "%.0f", valorAtual);
      mostra = ref;
    } else {
      mostra = "--";
    }
  }

  tft.setFreeFont(&FreeSansBold24pt7b);
  const int16_t larguraNum = tft.textWidth(mostra);
  tft.setTextFont(4);
  const int16_t larguraGrau = grau ? 9 : 0;
  const int16_t larguraC = larguraGrau + tft.textWidth(unidade);
  const int16_t total = larguraNum + 6 + larguraC;

  const int16_t x = LARGURA / 2 - total / 2;
  const int16_t y = VISOR_Y + VISOR_A / 2;

  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(cor, CARTAO);
  tft.drawString(mostra, x, y);

  // Grau desenhado, como nos cartoes: a fonte nao tem o simbolo.
  const int16_t xu = x + larguraNum + 6;
  if (grau) {
    tft.drawCircle(xu + 3, y - 9, 3, MUDO);
    tft.drawCircle(xu + 3, y - 9, 2, MUDO);
  }
  tft.setTextFont(4);
  tft.setTextColor(MUDO, CARTAO);
  tft.drawString(unidade, xu + larguraGrau, y);
}

static void desenhaAviso(const char *msg, uint16_t cor) {
  tft.fillRect(0, Y_AVISO - 9, LARGURA, 18, FUNDO);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cor, FUNDO);
  tft.drawString(msg, LARGURA / 2, Y_AVISO);
}

static void avisoFaixa() {
  char msg[40];
  snprintf(msg, sizeof msg, "de %.0f a %.0f %s", minimo, maximo, unidade);
  desenhaAviso(msg, MUDO);
}

// --- regras de digitacao ----------------------------------------------------
// Digitos inteiros limitados pelo maximo da faixa e no maximo uma casa
// decimal: mais do que isso so abriria espaco para erro de digitacao.
static void digita(char c) {
  const size_t n = strlen(texto);
  const char *ponto = strchr(texto, '.');

  if (c == '.') {
    if (!decimal || ponto) return;
    if (n == 0) { strcpy(texto, "0."); return; }
    texto[n] = '.';
    texto[n + 1] = '\0';
    return;
  }

  if (ponto) {
    if (strlen(ponto + 1) >= 1) return;  // ja tem a casa decimal
  } else {
    if (n >= maxInteiros) return;        // ja tem todos os inteiros
    // "0" seguido de digito vira so o digito: "05" nao e um numero que
    // alguem queira ver no visor.
    if (n == 1 && texto[0] == '0') texto[0] = '\0';
  }

  const size_t m = strlen(texto);
  texto[m] = c;
  texto[m + 1] = '\0';
}

// --- interface ---------------------------------------------------------------
void tecladoAbre(const char *t, float atual, bool tem, float mn, float mx,
                 const char *u, bool g, bool dec) {
  titulo = t;
  valorAtual = atual;
  temValor = tem;
  minimo = mn;
  maximo = mx;
  unidade = u;
  grau = g;
  decimal = dec;
  maxInteiros = mx >= 100 ? 3 : mx >= 10 ? 2 : 1;
  texto[0] = '\0';

  tft.fillScreen(FUNDO);

  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXTO, FUNDO);
  tft.drawString(titulo, LARGURA / 2, Y_TITULO);

  desenhaVisor();
  avisoFaixa();
  for (uint8_t i = 0; i < N_TECLAS; i++) desenhaTecla(i, false);
}

ResultadoTeclado tecladoToque(uint16_t x, uint16_t y, float &valor) {
  const int8_t i = teclaEm(x, y);
  if (i < 0) return TECLADO_SEGUE;
  if (i == T_PONTO && !decimal) return TECLADO_SEGUE;  // tecla apagada

  // Retorno visual curto: sem ele, num resistivo, nao da para saber se o
  // toque pegou.
  desenhaTecla(i, true);
  delay(70);
  desenhaTecla(i, false);

  if (i == T_CANCELA) return TECLADO_CANCELA;

  if (i == T_OK) {
    if (texto[0] == '\0') {
      // OK sem digitar nada: se ja havia valor, so fecha mantendo.
      if (temValor) return TECLADO_CANCELA;
      desenhaAviso("digite um valor", VERMELHO);
      return TECLADO_SEGUE;
    }

    const float v = atof(texto);
    if (v < minimo || v > maximo) {
      char msg[40];
      snprintf(msg, sizeof msg, "fora da faixa: de %.0f a %.0f %s",
               minimo, maximo, unidade);
      desenhaAviso(msg, VERMELHO);
      return TECLADO_SEGUE;
    }

    valor = v;
    return TECLADO_OK;
  }

  if (i == T_APAGA) {
    const size_t n = strlen(texto);
    if (n > 0) texto[n - 1] = '\0';
  } else {
    digita(i == T_PONTO ? '.' : i == T_ZERO ? '0' : (char)('1' + i));
  }

  desenhaVisor();
  avisoFaixa();
  return TECLADO_SEGUE;
}
