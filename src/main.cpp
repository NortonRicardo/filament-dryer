// ---------------------------------------------------------------------------
// Estufa de filamento - painel THERMO
//
// Le o BME280 e mostra temperatura e umidade no display, no layout de
// cartoes. Os cartoes de ventilador e aquecedor mostram o que o controle
// decidiu (ver controle.h). Serve tambem uma pagina na rede local, que e por
// onde tudo se configura (ver servidor.h).
//
// Display: 4" TFT SPI 480x320 V1.1, ST7796S, header de 14 pinos.
// Ligacao (os pinos ficam em platformio.ini, nao aqui):
//   VCC       -> 5V          SCK       -> GPIO18      RESET -> GPIO26
//   GND       -> GND         SDI(MOSI) -> GPIO23      CS    -> GPIO13
//   LED       -> GPIO17                               DC/RS -> GPIO27
//
// Sao oito fios. O SDO(MISO) do display fica sem ligar: nada e lido da tela.
// O LED e o controle da luz de fundo (acende em nivel alto): com PWM nele, o
// brilho e ajustavel pelo painel web.
//
// Sem touch por enquanto: este modulo veio sem o chip XPT2046 (U2). O codigo
// do touch esta guardado em extras/touch/.
//
// Ligacao do BME280 (SPI por software, pinos declarados abaixo). As pontes
// CSB -> VCC e SDO -> GND do modulo precisam estar desfeitas:
//   VCC -> 3V3        SCL (SCK) -> GPIO22
//   GND -> GND        SDA (SDI) -> GPIO21
//                     SDO       -> GPIO19
//                     CSB (CS)  -> GPIO4   (10 k do CS ao 3V3, no modulo)
//
// O display usa o SPI de hardware (VSPI, 40 MHz) nos pinos 18 e 23; o sensor
// usa outros pinos, por software e devagar: o cabo comprido ate ele nao
// atrapalha a tela, e um nao precisa esperar o outro.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <esp_system.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Adafruit_BME280.h>
#include <Preferences.h>

#include "aviso.h"
#include "controle.h"
#include "diario.h"
#include "fontes.h"
#include "icones.h"
#include "registro.h"
#include "servidor.h"
#include "tela.h"
#include "umidade.h"

TFT_eSPI tft = TFT_eSPI();

// Qual driver de tela foi compilado. Se nao acender, troque em platformio.ini.
#if defined(ST7796_DRIVER)
static const char *DRIVER = "ST7796S";
#elif defined(ILI9488_DRIVER)
static const char *DRIVER = "ILI9488";
#else
static const char *DRIVER = "nao definido";
#endif

// O init do ST7796 nao inverte; o flag em platformio.ini so deixa explicito.
// Se o preto sair branco, e a inversao que precisa mudar.
#if defined(TFT_INVERSION_OFF)
static const char *INVERSAO = "desligada (INVOFF)";
#elif defined(TFT_INVERSION_ON)
static const char *INVERSAO = "ligada (INVON)";
#else
static const char *INVERSAO = "nao definida (padrao do driver)";
#endif

// BME280 em SPI. Trocado do I2C em 15/11/2026: com o cartucho ligado o I2C
// nao respondia de jeito nenhum. No SPI as linhas sao empurradas para 0 e
// para 1 pelos pinos, em vez de puxadas para 1 por um resistor - aguentam
// bem mais ruido.
static const uint8_t PIN_SCK = 22;   // SCL do modulo
static const uint8_t PIN_SDI = 21;   // SDA do modulo (dado ESP32 -> sensor)
static const uint8_t PIN_SDO = 19;   // SDO do modulo (dado sensor -> ESP32)
static const uint8_t PIN_CS  = 4;    // CSB do modulo

// 100 kHz: o cabo tem ~1 m, e SPI foi feito para centimetros. Devagar, as
// bordas tem tempo de assentar. Uma leitura leva ~2 ms assim - sobra.
static const uint32_t SPI_SENSOR_HZ = 100000;

// A biblioteca so deixa escolher a velocidade do SPI por software criando o
// dispositivo na mao. O construtor padrao deixa spi_dev vazio; aqui ele
// recebe um a SPI_SENSOR_HZ, e a biblioteca passa a falar SPI.
class Bme280Spi : public Adafruit_BME280 {
 public:
  Bme280Spi() {
    spi_dev = new Adafruit_SPIDevice(PIN_CS, PIN_SCK, PIN_SDO, PIN_SDI,
                                     SPI_SENSOR_HZ);
  }
};
static Bme280Spi bme;

static const uint32_t INTERVALO_LEITURA = 2000;      // ms entre leituras

// Uma leitura corrompida pode dar um valor plausivel (30,0 C entre leituras
// de 29,5): passa pelos filtros de salto. A caixa muda ~0,07 C/min, entao
// uma diferenca maior que CONFIRMA_C so e aceita se a leitura seguinte, 2 s
// depois, confirmar. Depois de CONFIRMA_MAX leituras sem confirmar (mudanca
// rapida de verdade, como porta aberta) aceita a ultima.
static const float   CONFIRMA_C = 0.3f;
static const uint8_t CONFIRMA_MAX = 3;

// Leituras falhas seguidas antes de dar o sensor por perdido. Uma falha
// isolada (ruido, ou o sensor reiniciado por um tranco de tensao) nao deve
// derrubar a secagem; um sensor solto de verdade falha sempre e e pego em
// 3 x 2 s = 6 s.
static const uint8_t FALHAS_MAX = 5;

// Prazo do watchdog. Ver o comentario no fim do setup().
static const uint32_t WATCHDOG_S = 30;
static const uint32_t INTERVALO_RETENTATIVA = 5000;  // ms entre buscas do sensor

static bool     sensorOk = false;
static uint32_t ultimaLeitura = 0;
static uint8_t  falhasSeguidas = 0;

// Leitura com diferenca grande esperando confirmacao (ver CONFIRMA_C).
static bool     pendente = false;
static float    tempPendente = 0;
static uint8_t  naoConfirmadas = 0;


// ---------------------------------------------------------------------------
// Sensor
// ---------------------------------------------------------------------------

// Traduz o chip ID lido do sensor. Serve para pegar o caso em que chegou um
// BMP280 no lugar do BME280: sao quase identicos, mas o BMP nao tem umidade.
static const char *nomeChip(uint8_t id) {
  switch (id) {
    case 0x60: return "BME280";
    case 0x58: return "BMP280 - NAO mede umidade";
    case 0x56:
    case 0x57: return "BMP280 (amostra de engenharia)";
    default:   return "desconhecido";
  }
}

// bme.begin() le o chip ID (tem de ser 0x60) e reseta o sensor. Sem sensor,
// a linha SDO fica solta e o chip ID nao bate: volta false sem travar.
static bool iniciaSensor() {
  if (!bme.begin()) return false;
  {
    // MODO FORCADO em vez de continuo: o sensor dorme entre as leituras e
    // nao se aquece. Em modo continuo o proprio chip esquenta e puxa a
    // temperatura para cima alguns decimos - erro que nao da para corrigir
    // depois, e justamente temperatura e o que mais importa aqui.
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // temperatura
                    Adafruit_BME280::SAMPLING_NONE, // pressao: nao interessa
                    Adafruit_BME280::SAMPLING_X1,   // umidade
                    Adafruit_BME280::FILTER_OFF);
  }
  return true;
}


// ---------------------------------------------------------------------------
// Validacao da leitura
//
// Ruido no cabo do sensor (o aquecedor chaveia ~15 A perto dele) pode
// corromper a transferencia. O BME280 nao avisa: a biblioteca devolve os
// bytes que leu, e a compensacao do datasheet transforma lixo em numero.
//
// Os valores nao sao aleatorios, sao assinaturas do barramento:
//
//   registradores em 0xFFFFF (barramento devolvendo 0xFF) -> ~182 a 190 C
//   registradores em 0x00000 (barramento em curto)        -> ~-141 C
//
// E o 182 e o caso FELIZ: ele estoura o teto e a protecao corta. O -141
// seria o perigoso - o PID leria "fazendo muito frio" e mandaria 100% de
// potencia para sempre. Por isso a leitura e validada antes de qualquer
// coisa olhar para ela: controle, display e grafico so veem numero plausivel.
// ---------------------------------------------------------------------------

// Ultima leitura boa: a validacao compara contra ela, e o display repinta
// a partir dela ao religar.
static float ultimaTemp = 0, ultimaUmid = 0;
static bool temLeitura = false;

static const float TEMP_PLAUSIVEL_MIN = -10.0f;
static const float TEMP_PLAUSIVEL_MAX = 100.0f;

// O ar de uma caixa fechada nao muda tanto assim em 2 s, nem com o cartucho
// no maximo. Um salto maior que isso e ruido, nao temperatura.
static const float SALTO_MAX_C = 5.0f;

// Idem para a umidade. A leitura corrompida as vezes estraga so ela, e a
// biblioteca entrega exatamente 100,0 % - a temperatura passava no filtro e
// o 100 % ia para a tela. Abrir a porta muda alguns % em 2 s, nao 15.
static const float SALTO_MAX_UR = 15.0f;

// Descartar nao vira falha aqui: leSensor devolve false e o loop ja trata
// isso com o seu proprio contador de falhas seguidas.

static uint8_t descartesSeguidos = 0;
static uint32_t descartesTotal = 0;

static bool leituraPlausivel(float t, float u) {
  if (isnan(t) || isnan(u)) return false;
  if (t < TEMP_PLAUSIVEL_MIN || t > TEMP_PLAUSIVEL_MAX) return false;
  if (u < 0.0f || u > 100.0f) return false;
  if (temLeitura && fabsf(t - ultimaTemp) > SALTO_MAX_C) return false;
  if (temLeitura && fabsf(u - ultimaUmid) > SALTO_MAX_UR) return false;
  return true;
}

static bool leSensor(float &temperatura, float &umidade) {
  if (!bme.takeForcedMeasurement()) return false;

  temperatura = bme.readTemperature();
  umidade     = bme.readHumidity();

  if (leituraPlausivel(temperatura, umidade)) {
    if (descartesSeguidos > 0) {
      diario.printf("Sensor           : voltou ao normal apos %u leitura(s) descartada(s)\n",
                    descartesSeguidos);
    }
    descartesSeguidos = 0;
    return true;
  }

  descartesSeguidos++;
  descartesTotal++;
  diario.printf("Sensor           : leitura descartada (%.1f C, %.1f %%) - %u seguida(s), %lu no total\n",
                temperatura, umidade, descartesSeguidos,
                (unsigned long)descartesTotal);

  // Aguenta alguns glitches sem derrubar nada. Passando disso, e falha.
  return false;
}

uint32_t leiturasDescartadas() { return descartesTotal; }

// ---------------------------------------------------------------------------
// Desenho
//
// Regra em todo este bloco: escolher a fonte (fonte()) logo antes de cada
// desenho ou medicao.
// ---------------------------------------------------------------------------

// Texto centrado num ponto, na fonte ja escolhida.
static void textoCentrado(int16_t cx, int16_t y, const char *texto,
                          uint16_t cor, uint16_t fundo) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cor, fundo);
  tft.drawString(texto, cx, y);
}

// ---------------------------------------------------------------------------
// Fontes: a Inter, suavizada, gravada em src/fontes.h. Cada objeto (a tela e
// cada sprite) carrega a sua; trocar so quando muda, porque carregar le a
// tabela de caracteres da fonte.
// ---------------------------------------------------------------------------
template <typename A>
static void usaFonte(A &a, const uint8_t *&atual, const uint8_t *f) {
  if (atual == f) return;
  if (atual) a.unloadFont();
  a.loadFont(f);
  atual = f;
}
static const uint8_t *fonteTft = nullptr;
static void fonte(const uint8_t *f) { usaFonte(tft, fonteTft, f); }

// Posicoes dentro do cartao, por linha: rotulo no alto, valor no meio,
// estado embaixo. Temperatura e umidade centradas; aquecedor e fans com o
// icone animado a esquerda e o texto alinhado a direita.
//
// A faixa reservada ao valor e o que se apaga a cada leitura. Apagar so ela,
// e nao o cartao inteiro, e o que evita piscar. Ela cobre a altura dos
// digitos da Inter Bold 42 (~31 px) sem encostar no rotulo nem no que vem
// embaixo. Na temperatura ela e mais estreita: dos lados ficam o alvo e a
// maxima.
static const int16_t FAIXA_A = 36;
static const int16_t FAIXA_TEMP_L = 144;
static int16_t yRotulo(const Cartao &c) { return c.y + ((c.y == Y_L1 || c.y == Y_L4) ? 18 : 20); }
static int16_t yValor(const Cartao &c) {
  if (c.y == Y_L1) return c.y + 52;
  if (c.y == Y_L4) return c.y + 50;
  return c.y + 54;
}
static int16_t faixaTopo(const Cartao &c)    { return yValor(c) - FAIXA_A / 2; }
static int16_t faixaLargura(const Cartao &c) {
  return c.l == LARGURA_TODA ? FAIXA_TEMP_L : c.l - 6;
}
// Temperatura: valor e rotulo na metade esquerda; na direita, o alvo e a
// maxima. A linha fica exatamente no meio, alinhada com a do aquecedor.
static const int16_t TEMP_ESQ = LARGURA_TODA / 2;  // 150
static int16_t centroX(const Cartao &c) {
  return c.l == LARGURA_TODA ? c.x + TEMP_ESQ / 2 : c.x + c.l / 2;
}
static int16_t faixaX(const Cartao &c) { return centroX(c) - faixaLargura(c) / 2; }
static int16_t xDireita(const Cartao &c)     { return c.x + c.l - 12; }  // fim do texto a direita
static int16_t yEstado(const Cartao &c)      { return c.y == Y_L3 ? c.y + 90 : c.y + 80; }

// O valor e montado num sprite fora da tela e empurrado de uma vez. Apagar a
// faixa direto na tela e redesenhar deixava um quadro em branco visivel - a
// piscada na troca de valor. Dois sprites: meio cartao (~10 KB) e a faixa
// da temperatura (~10 KB).
static TFT_eSprite faixaMeia = TFT_eSprite(&tft);
static TFT_eSprite faixaTemp = TFT_eSprite(&tft);
static bool faixaMeiaOk = false, faixaTempOk = false, faixasCriadas = false;
static const uint8_t *fonteMeia = nullptr, *fonteTemp = nullptr;

// Onde o icone animado ficou em cada cartao (x na tela). Ele vai junto do
// numero, centrados como um grupo: muda de lugar quando o numero muda de
// largura ("0" e "100").
static int16_t xIcone[N_CARTOES];
static void iconeMudouDeLugar(uint8_t i);

// O que esta desenhado em cada cartao. Valor igual nao redesenha.
static char valorNaTela[N_CARTOES][12];
static bool dadoNaTela[N_CARTOES];

static void esqueceValor(uint8_t i) { valorNaTela[i][0] = '\0'; }

// Numero grande e unidade pequena ao lado, centrados juntos (nos cartoes com
// icone, o icone vai na frente e entra no centro do grupo). Template porque
// desenha tanto na tela quanto no sprite: na TFT_eSPI so drawPixel e
// virtual, entao passar o sprite como TFT_eSPI& desenharia na tela.
template <typename Alvo>
static int16_t montaValor(Alvo &a, const uint8_t *&fonteA, int16_t ox, int16_t oy,
                          const Cartao &c, const char *valor, bool temDado) {
  const int16_t lf = faixaLargura(c);
  a.fillRect(ox, oy, lf, FAIXA_A, CLARO);

  usaFonte(a, fonteA, INTER_ROTULO);
  const int16_t larguraUnidade = temDado ? a.textWidth(c.unidade) : 0;
  usaFonte(a, fonteA, INTER_VALOR);
  const int16_t larguraValor = a.textWidth(valor);
  const int16_t icone = c.icone ? ICONE_L + 8 : 0;
  const int16_t total = icone + larguraValor + (temDado ? 4 + larguraUnidade : 0);

  const int16_t inicio = ox + lf / 2 - total / 2;  // onde o icone fica
  const int16_t x = inicio + icone;
  const int16_t y = oy + FAIXA_A / 2;

  a.setTextDatum(ML_DATUM);
  a.setTextColor(temDado ? ESCURO : SEMDADO_C, CLARO);
  a.drawString(valor, x, y);

  if (temDado) {
    usaFonte(a, fonteA, INTER_ROTULO);
    a.setTextColor(UNID_C, CLARO);
    a.drawString(c.unidade, x + larguraValor + 4, y + 6);
  }
  return inicio - ox;
}

// Sem dado, "--" em cinza: o cartao existe no layout mas nao inventa leitura.
static void desenhaValor(uint8_t i, const char *valor, bool temDado) {
  if (dadoNaTela[i] == temDado && strcmp(valorNaTela[i], valor) == 0) return;
  snprintf(valorNaTela[i], sizeof valorNaTela[i], "%s", valor);
  dadoNaTela[i] = temDado;

  const Cartao &c = CARTOES[i];

  if (!faixasCriadas) {
    faixasCriadas = true;
    faixaMeia.setColorDepth(16);
    faixaMeiaOk = faixaMeia.createSprite(COL_L - 6, FAIXA_A) != nullptr;
    faixaTemp.setColorDepth(16);
    faixaTempOk = faixaTemp.createSprite(FAIXA_TEMP_L, FAIXA_A) != nullptr;
  }

  const bool temp = c.l == LARGURA_TODA;
  TFT_eSprite &faixa = temp ? faixaTemp : faixaMeia;
  const uint8_t *&fonteF = temp ? fonteTemp : fonteMeia;
  const bool ok = temp ? faixaTempOk : faixaMeiaOk;

  int16_t dx;
  if (ok) {
    dx = montaValor(faixa, fonteF, 0, 0, c, valor, temDado);
    faixa.pushSprite(faixaX(c), faixaTopo(c));
  } else {  // sem memoria: pisca, mas funciona
    dx = montaValor(tft, fonteTft, faixaX(c), faixaTopo(c), c, valor, temDado);
  }
  if (c.icone) {            // a faixa apagou o icone: desenha de novo no lugar novo
    xIcone[i] = faixaX(c) + dx;
    iconeMudouDeLugar(i);
  }
}

// Texto pequeno centrado num ponto do cartao.
static void textoEm(int16_t cx, int16_t y, const char *texto, const uint8_t *f,
                    uint16_t cor) {
  fonte(f);
  tft.setTextColor(cor, CLARO);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(texto, cx, y);
}

static void desenhaCartao(uint8_t i) {
  const Cartao &c = CARTOES[i];
  esqueceValor(i);

  if (i == CARTAO_POT) {
    // Aquecedor e fan do aquecedor: um fundo so, das duas colunas, com uma
    // linha no meio. A metade do fan nao pinta fundo.
    tft.fillRoundRect(COL_E, c.y, LARGURA_TODA, c.a, 10, CLARO);
    tft.drawFastVLine(LARGURA / 2, c.y + 16, c.a - 32, DIVISOR_C);
  } else if (i != CARTAO_FANAQ) {
    tft.fillRoundRect(c.x, c.y, c.l, c.a, 10, CLARO);
  }
  if (i == CARTAO_TEMP) {
    tft.drawFastVLine(c.x + TEMP_ESQ, c.y + 14, c.a - 28, DIVISOR_C);
  }

  if (c.icone) {                         // rotulo alinhado a direita
    fonte(INTER_ROTULO);
    tft.setTextColor(ROTULO_C, CLARO);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(c.rotulo, xDireita(c), yRotulo(c));
  } else if (i == CARTAO_UMID || i == CARTAO_UABS) {
    // Umidade: gota antes do rotulo, os dois centrados juntos.
    fonte(INTER_ROTULO);
    const int16_t lt = tft.textWidth(c.rotulo), total = 10 + 5 + lt;
    const int16_t x = centroX(c) - total / 2, cy = yRotulo(c);
    tft.fillCircle(x + 5, cy + 2, 4, GOTA_C);
    tft.fillTriangle(x + 1, cy + 1, x + 9, cy + 1, x + 5, cy - 7, GOTA_C);
    tft.setTextColor(ROTULO_C, CLARO);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(c.rotulo, x + 15, cy);
  } else {
    textoEm(centroX(c), yRotulo(c), c.rotulo, INTER_ROTULO, ROTULO_C);
  }
}

static void telaFixa() {
  tft.fillScreen(FUNDO2);
  for (uint8_t i = 0; i < N_CARTOES; i++) desenhaCartao(i);
}

// Coluna da direita da temperatura: uma etiqueta (icone dentro do nome,
// texto claro numa pilula da cor do valor) e o numero a direita. Linha 0 e o
// alvo do aquecedor (bandeira); linha 1, a maxima (escudo) - os mesmos do
// painel web.
enum IconeTemp { ICONE_ALVO, ICONE_MAX };

// Icones de 12 px, desenhados com linhas: nessa escala ficam mais nitidos
// que qualquer fonte de icones.
static void desenhaIcone(IconeTemp icone, int16_t x, int16_t cy, uint16_t cor) {
  if (icone == ICONE_ALVO) {                       // bandeira
    tft.fillRect(x, cy - 6, 2, 13, cor);           // mastro
    tft.fillTriangle(x + 2, cy - 6, x + 10, cy - 3, x + 2, cy, cor);
  } else {                                         // escudo com "!"
    tft.drawLine(x, cy - 5, x + 5, cy - 7, cor);
    tft.drawLine(x + 5, cy - 7, x + 10, cy - 5, cor);
    tft.drawLine(x, cy - 5, x, cy, cor);
    tft.drawLine(x + 10, cy - 5, x + 10, cy, cor);
    tft.drawLine(x, cy, x + 5, cy + 6, cor);
    tft.drawLine(x + 10, cy, x + 5, cy + 6, cor);
    tft.drawFastVLine(x + 5, cy - 3, 4, cor);
    tft.drawPixel(x + 5, cy + 3, cor);
  }
}

static void linhaTemperatura(uint8_t linha, IconeTemp icone, const char *nome,
                             const char *valor, uint16_t cor) {
  const Cartao &c = CARTOES[CARTAO_TEMP];
  const int16_t x0 = c.x + TEMP_ESQ + 1, x1 = c.x + c.l;
  const int16_t y = c.y + (linha == 0 ? 32 : 62);
  tft.fillRect(x0 + 2, y - 11, x1 - x0 - 6, 22, CLARO);

  fonte(INTER_ESTADO);
  const int16_t lt = tft.textWidth(nome);
  const int16_t px = x0 + 8, pa = 20, pl = 7 + 11 + 5 + lt + 8;
  tft.fillRoundRect(px, y - pa / 2, pl, pa, pa / 2, cor);
  desenhaIcone(icone, px + 7, y, CLARO);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLARO, cor);
  tft.drawString(nome, px + 7 + 11 + 5, y + 1);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(cor, CLARO);
  tft.drawString(valor, x1 - 8, y + 1);
}

// Uma casa decimal, menos a partir de 100: assim o texto nunca passa de
// quatro digitos e cabe na largura do cartao sem reduzir a fonte.
static void formataValor(float valor, char *buf, size_t n) {
  if (valor >= 100.0f || valor <= -100.0f) snprintf(buf, n, "%.0f", valor);
  else                                     snprintf(buf, n, "%.1f", valor);
}

// Com a imagem desligada pelo painel web, o loop continua lendo o sensor e
// rodando o controle, so nao desenha. Ao religar, tudo volta com os valores
// atuais.
static bool telaLigada = true;

// --- luz de fundo ---------------------------------------------------------------
// PWM no pino LED do display. Pela documentacao do modulo (lcdwiki MSP4021),
// esse pino comanda a luz e acende em nivel alto. O canal 2 do LEDC usa outro
// temporizador que os canais 0 e 1 dos ventiladores, entao a frequencia aqui
// nao interfere nos 25 kHz deles.
static const uint8_t  PIN_LUZ    = 17;
static const uint8_t  CANAL_LUZ  = 2;
static const uint32_t LUZ_FREQ   = 5000;  // bem acima do que o olho percebe
static const uint8_t  BRILHO_MIN = 5;     // abaixo disso a tela parece morta
static const uint8_t  BRILHO_PADRAO = 60;
static const char    *NVS_BRILHO = "brilho";

static uint8_t brilho = BRILHO_PADRAO;  // %, 5 a 100

// Com a imagem apagada pelo painel web a luz tambem apaga: tela preta com a
// luz acesa ainda brilha, e era isso que o modulo anterior obrigava.
static void aplicaLuz() {
  const uint8_t pct = telaLigada ? brilho : 0;
  ledcWrite(CANAL_LUZ, (uint32_t)pct * 255 / 100);
}

static void iniciaLuz() {
  ledcSetup(CANAL_LUZ, LUZ_FREQ, 8);
  ledcAttachPin(PIN_LUZ, CANAL_LUZ);
  ledcWrite(CANAL_LUZ, 0);  // apagada ate o painel estar desenhado

  Preferences nvs;
  if (nvs.begin("painel", true)) {
    if (nvs.isKey(NVS_BRILHO)) brilho = nvs.getUChar(NVS_BRILHO, BRILHO_PADRAO);
    nvs.end();
  }
  if (brilho < BRILHO_MIN || brilho > 100) brilho = BRILHO_PADRAO;
}


// --- temperatura maxima -------------------------------------------------------
// Definida no painel web e salva na memoria, entao sobrevive a um reinicio.
// Por enquanto so e guardada e mostrada: ainda nao entra na regra do controle.
static const char *NVS_PAINEL = "painel";
static const char *NVS_TMAX   = "tmax";
static const float TMAX_PADRAO = 30.0f;  // ate ser mudada no painel web

static float tempMaxima = 0;
static bool  temMaxima  = false;

// Sem valor salvo, parte de TMAX_PADRAO ja como configurada. O namespace
// "painel" pode nem existir no primeiro boot, e ai begin() em leitura falha:
// por isso o padrao e aplicado antes, nao depende do getFloat.
static void carregaMaxima() {
  tempMaxima = TMAX_PADRAO;
  temMaxima = true;

  Preferences nvs;
  if (nvs.begin(NVS_PAINEL, true)) {
    // isKey antes: getFloat de chave ausente imprime um erro no console.
    if (nvs.isKey(NVS_TMAX)) tempMaxima = nvs.getFloat(NVS_TMAX, TMAX_PADRAO);
    nvs.end();
  }
}

static void salvaMaxima(float valor) {
  tempMaxima = valor;
  temMaxima = true;

  Preferences nvs;
  nvs.begin(NVS_PAINEL, false);
  nvs.putFloat(NVS_TMAX, valor);
  nvs.end();

  diario.printf("Temp. maxima     : %.1f C (salva)\n", valor);
}

// Linha de baixo do cartao de temperatura. Em verde quando configurada, para
// distinguir de leitura; em cinza, "MAX --", quando ainda nao foi definida.
// Fonte 2, maior que a dos outros detalhes, porque e um valor que se
// consulta de longe.
static void pintaMaximo() {
  char buf[12];
  if (temMaxima) {
    snprintf(buf, sizeof buf, "%.1f\xC2\xB0", tempMaxima);
    linhaTemperatura(1, ICONE_MAX, "MAX", buf, OK_C);
  } else {
    linhaTemperatura(1, ICONE_MAX, "MAX", "--", UNID_C);
  }
}

// Temperatura, umidade relativa e umidade absoluta. A absoluta tem cartao
// proprio: e ela, e nao a relativa, que diz se a agua esta saindo da caixa.
// Sem leitura os tres mostram "--" em cinza, em vez de congelar o ultimo valor.
static void pintaMedidas() {
  char buf[16];

  if (!temLeitura) {
    desenhaValor(CARTAO_TEMP, "--", false);
    desenhaValor(CARTAO_UMID, "--", false);
    desenhaValor(CARTAO_UABS, "--", false);
    return;
  }

  formataValor(ultimaTemp, buf, sizeof buf);
  desenhaValor(CARTAO_TEMP, buf, true);

  formataValor(ultimaUmid, buf, sizeof buf);
  desenhaValor(CARTAO_UMID, buf, true);

  formataValor(umidadeAbsoluta(ultimaTemp, ultimaUmid), buf, sizeof buf);
  desenhaValor(CARTAO_UABS, buf, true);
}

static void mostraLeitura(float temperatura, float umidade) {
  ultimaTemp = temperatura;
  ultimaUmid = umidade;
  temLeitura = true;

  if (telaLigada) pintaMedidas();
}

static void mostraSemSensor() {
  temLeitura = false;
  if (telaLigada) pintaMedidas();
}

// Linha de estado do cartao: pilula verde (ativo, no alvo), laranja
// (aquecendo) ou vermelha (parado, protecao) - o mesmo do painel web. So
// redesenha quando muda.
static char estadoNaTela[N_CARTOES][16];

static void desenhaEstado(uint8_t i, const char *texto, uint16_t cor) {
  if (strcmp(estadoNaTela[i], texto) == 0) return;
  snprintf(estadoNaTela[i], sizeof estadoNaTela[i], "%s", texto);

  const Cartao &c = CARTOES[i];
  const int16_t cy = yEstado(c), cx = c.x + c.l / 2;
  tft.fillRect(c.x + 3, cy - 11, c.l - 6, 22, CLARO);

  // Pilula da cor do estado, texto claro dentro, centrada.
  fonte(INTER_ESTADO);
  const int16_t lt = tft.textWidth(texto), l = lt + 20, a = 20;
  tft.fillRoundRect(cx - l / 2, cy - a / 2, l, a, a / 2, cor);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLARO, cor);
  tft.drawString(texto, cx - lt / 2, cy + 1);
}

// Esquerda do valor da temperatura: o alvo do aquecedor.
static float alvoNaTela = -1;   // -1: redesenhar
static void desenhaAlvo() {
  const float alvo = controleConfig().alvo;
  if (alvo == alvoNaTela) return;
  alvoNaTela = alvo;
  char buf[12];
  snprintf(buf, sizeof buf, "%.0f\xC2\xB0", alvo);
  linhaTemperatura(0, ICONE_ALVO, "ALVO", buf, QUENTE_C);
}

// Ventiladores e aquecedor, com o que o controle decidiu. No boot tudo esta
// desligado: os cartoes mostram a pilula vermelha ate o painel web ligar.
static void pintaControle() {
  char buf[16];

  // Potencia que o PID pede, e o estado do aquecedor.
  snprintf(buf, sizeof buf, "%u", controleAutorizado() ? controlePotencia() : 0);
  desenhaValor(CARTAO_POT, buf, true);
  if (controleEstado() == EST_FALHA)          desenhaEstado(CARTAO_POT, "PROTE\xC3\x87\xC3\x83O", PARADO_C);
  else if (controleEstado() == EST_AQUECENDO) desenhaEstado(CARTAO_POT, "AQUECENDO", QUENTE_C);
  else if (controleAutorizado())              desenhaEstado(CARTAO_POT, "NO ALVO", OK_C);
  else                                        desenhaEstado(CARTAO_POT, "DESLIGADO", PARADO_C);
  desenhaAlvo();

  // Fan do aquecedor (canal 0) e os dois de circulacao (canal 1, juntos).
  const uint8_t fanAq = controleVelocidade(0), circ = controleVelocidade(1);
  snprintf(buf, sizeof buf, "%u", fanAq);
  desenhaValor(CARTAO_FANAQ, buf, true);
  if (fanAq > 0) desenhaEstado(CARTAO_FANAQ, "ATIVO", OK_C);
  else           desenhaEstado(CARTAO_FANAQ, "INATIVO", PARADO_C);

  snprintf(buf, sizeof buf, "%u", circ);
  for (uint8_t i = CARTAO_CIRC1; i <= CARTAO_CIRC2; i++) {
    desenhaValor(i, buf, true);
    if (circ > 0) desenhaEstado(i, "ATIVO", OK_C);
    else          desenhaEstado(i, "INATIVO", PARADO_C);
  }
}

// ---------------------------------------------------------------------------
// Icones animados dos cartoes do aquecedor e dos fans.
//
// Fan: helice de tres pas. Ligado, verde e girando - mais rapido quanto
// maior a velocidade. Parado, cinza.
// Aquecedor: chama. Aquecendo, vermelha com o miolo laranja e amarelo,
// tremulando. Parado, cinza.
//
// Cada quadro e montado num sprite de 32x32 e empurrado de uma vez: so o
// icone e redesenhado, sem piscar o cartao.
// ---------------------------------------------------------------------------
static TFT_eSprite icone = TFT_eSprite(&tft);
static bool iconeOk = false;
static const uint32_t QUADRO_MS = 50;   // ~20 quadros por segundo

// Pinta uma mascara de 4 bits no sprite do icone, misturando a cor com o que
// ja esta embaixo (as camadas da chama se sobrepoem).
static void pintaMascara(const uint8_t *mascara, uint16_t cor) {
  for (uint16_t p = 0; p < ICONE_L * ICONE_L; p++) {
    const uint8_t b = pgm_read_byte(mascara + p / 2);
    const uint8_t a = (p & 1) ? (b & 0x0F) : (b >> 4);
    if (!a) continue;
    const int16_t x = p % ICONE_L, y = p / ICONE_L;
    icone.drawPixel(x, y, a == 15 ? cor : icone.alphaBlend(a * 17, cor, icone.readPixel(x, y)));
  }
}

static void desenhaHelice(uint8_t quadro, uint16_t cor) {
  icone.fillSprite(CLARO);
  pintaMascara(HELICE[quadro], cor);
}

static void desenhaChama(uint8_t quadro, bool acesa) {
  icone.fillSprite(CLARO);
  if (!acesa) { pintaMascara(CHAMA_FORA[0], SEMDADO_C); return; }
  pintaMascara(CHAMA_FORA[quadro], PARADO_C);
  pintaMascara(CHAMA_MEIO[quadro], LARANJA_C);
  pintaMascara(CHAMA_NUCLEO[quadro], AMARELO_C);
}

static void pushIcone(uint8_t i) {
  icone.pushSprite(xIcone[i], yValor(CARTOES[i]) - ICONE_L / 2);
}

static float anguloFan[3] = {0, 0, 0};   // em quadros da helice
static int8_t  estadoIcone[4] = {-1, -1, -1, -1};  // -1 = redesenhar

static void esqueceIcones() {
  for (uint8_t k = 0; k < 4; k++) estadoIcone[k] = -1;
}

static void iconeMudouDeLugar(uint8_t i) {
  if (i == CARTAO_POT) estadoIcone[0] = -1;
  else if (i == CARTAO_FANAQ) estadoIcone[1] = -1;
  else if (i == CARTAO_CIRC1) estadoIcone[2] = -1;
  else if (i == CARTAO_CIRC2) estadoIcone[3] = -1;
}

static void animaIcones() {
  static uint32_t ultimo = 0;
  if (!telaLigada || millis() - ultimo < QUADRO_MS) return;
  ultimo = millis();

  if (!iconeOk) {
    icone.setColorDepth(16);
    iconeOk = icone.createSprite(ICONE_L, ICONE_L) != nullptr;
    if (!iconeOk) return;
  }

  // Chama: so anima enquanto aquece; parada, desenha uma vez.
  static uint8_t quadro = 0, conta = 0;
  const bool acesa = controleEstado() == EST_AQUECENDO;
  if (acesa) {
    if (++conta >= 3) { conta = 0; quadro = (quadro + 1) % CHAMA_QUADROS; estadoIcone[0] = -1; }
  }
  if (estadoIcone[0] != (acesa ? quadro + 1 : 0)) {
    estadoIcone[0] = acesa ? quadro + 1 : 0;
    desenhaChama(quadro, acesa);
    pushIcone(CARTAO_POT);
  }

  // Fans: canal 0 e o do aquecedor; canal 1 move os dois de circulacao.
  static const uint8_t CARTAO_FAN[3] = {CARTAO_FANAQ, CARTAO_CIRC1, CARTAO_CIRC2};
  for (uint8_t f = 0; f < 3; f++) {
    const uint8_t pct = controleVelocidade(f == 0 ? 0 : 1);
    if (pct > 0) {
      // Cada quadro e 12 graus. 30% gira devagar, 100% bem rapido: de ~0,7
      // a ~2 voltas por segundo.
      anguloFan[f] += 0.6f + pct * 0.0145f;
      while (anguloFan[f] >= HELICE_QUADROS) anguloFan[f] -= HELICE_QUADROS;
      desenhaHelice((uint8_t)anguloFan[f], OK_C);
      pushIcone(CARTAO_FAN[f]);
      estadoIcone[1 + f] = 1;
    } else if (estadoIcone[1 + f] != 0) {
      estadoIcone[1 + f] = 0;
      desenhaHelice((uint8_t)anguloFan[f], SEMDADO_C);
      pushIcone(CARTAO_FAN[f]);
    }
  }
}

// Rodape: so o endereco, centrado. Como ele vira "sem Wi-Fi" ou
// "conectando..." quando a rede cai, o estado da conexao continua visivel
// aqui sem precisar de um indicador separado.
static void desenhaRodape(const char *endereco) {
  // A faixa inteira do rodape, de borda a borda: a listra de aviso ocupa tudo,
  // e ao sumir ela precisa ser apagada tambem nas laterais.
  const int16_t topo = Y_RODAPE - ALTURA_RODAPE / 2;

  if (servidorPortalAtivo()) {
    // Sem a rede de casa: listra laranja com o nome e a senha da rede propria,
    // alternando com o aviso a cada 3 s (servidorEndereco()).
    tft.fillRect(0, topo, LARGURA, ALTURA_RODAPE, LARANJA);
    fonte(INTER_ESTADO);
    textoCentrado(LARGURA / 2, Y_RODAPE, endereco, FUNDO2, LARANJA);
    return;
  }

  tft.fillRect(0, topo, LARGURA, ALTURA_RODAPE, FUNDO2);
  fonte(INTER_ROTULO);
  textoCentrado(LARGURA / 2, Y_RODAPE, endereco, TITULO_C, FUNDO2);
}

// Relogio no centro do topo. Horas e minutos so: redesenha uma vez por
// minuto. "--:--" ate a hora chegar pela internet.
static char relogioNaTela[8] = "";
static int8_t conexaoNaTela = -1;   // -1 nunca desenhado, 0 sem rede, 1 conectado

// Indicador de conexao no canto direito: tres arcos e um ponto, como o
// simbolo de Wi-Fi, e o texto. Verde conectado, vermelho sem rede.
static void desenhaConexao(bool forca) {
  const int8_t ok = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  if (!forca && ok == conexaoNaTela) return;
  conexaoNaTela = ok;

  const uint16_t cor = ok ? CONECT_C : SEMREDE_C;
  const char *texto = ok ? "conectado" : "sem rede";
  tft.fillRect(LARGURA - MARGEM - 92, 0, 92, ALTURA_TOPO, FUNDO2);

  fonte(INTER_ROTULO);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(cor, FUNDO2);
  tft.drawString(texto, LARGURA - MARGEM, Y_TOPO);

  const int16_t cx = LARGURA - MARGEM - tft.textWidth(texto) - 12, cy = Y_TOPO + 5;
  tft.fillCircle(cx, cy, 2, cor);
  for (int16_t r = 5; r <= 13; r += 4) {
    tft.drawArc(cx, cy, r + 1, r, 135, 225, cor, FUNDO2, false);
  }
}

static void desenhaRelogio(bool forca) {
  desenhaConexao(forca);

  char texto[8] = "--:--";
  if (horaCerta()) {
    const time_t agora = time(nullptr);
    struct tm t;
    localtime_r(&agora, &t);
    strftime(texto, sizeof texto, "%H:%M", &t);
  }
  if (!forca && strcmp(texto, relogioNaTela) == 0) return;
  snprintf(relogioNaTela, sizeof relogioNaTela, "%s", texto);

  tft.fillRect(LARGURA / 2 - 40, 0, 80, ALTURA_TOPO, FUNDO2);
  fonte(INTER_RELOGIO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TOPO_C, FUNDO2);
  tft.drawString(texto, LARGURA / 2, Y_TOPO);
}

// Painel inteiro: no boot e ao religar a imagem.
static void desenhaPainel() {
  for (uint8_t i = 0; i < N_CARTOES; i++) estadoNaTela[i][0] = '\0';
  alvoNaTela = -1;
  esqueceIcones();
  telaFixa();
  desenhaRelogio(true);
  desenhaRodape(servidorEndereco());
  pintaControle();
  pintaMedidas();  // "--" ate a primeira leitura chegar
  pintaMaximo();
}

// --- chamadas do servidor -------------------------------------------------------

// Apagar e pintar de preto, nao mandar DISPOFF ao controlador: no ST7789 do
// display anterior o DISPOFF mostrava uma pagina branca, e pintar de preto da
// o resultado esperado em qualquer controlador, ao custo de redesenhar tudo
// ao religar. A luz de fundo continua acesa: o LED esta no 3V3.
void telaDefineLigada(bool ligada) {
  if (ligada == telaLigada) return;
  telaLigada = ligada;

  if (ligada) desenhaPainel();
  else        tft.fillScreen(TFT_BLACK);
  aplicaLuz();

  diario.printf("Tela             : %s\n", ligada ? "ligada" : "apagada");
}

bool telaEstaLigada() { return telaLigada; }

void telaDefineBrilho(uint8_t pct) {
  if (pct < BRILHO_MIN) pct = BRILHO_MIN;
  if (pct > 100) pct = 100;
  brilho = pct;
  aplicaLuz();

  Preferences nvs;
  nvs.begin("painel", false);
  nvs.putUChar(NVS_BRILHO, brilho);
  nvs.end();
}

uint8_t telaBrilho() { return brilho; }

// Tela de atualizacao: titulo e barra de progresso. Desenhada direto, sem os
// cartoes, porque durante a gravacao o loop nao roda e o painel ficaria
// congelado com valores velhos - e parecendo que o aquecedor ainda funciona.
void telaAtualizando(uint8_t pct) {
  static bool montada = false;
  const int16_t bx = 30, by = ALTURA / 2 + 10, bl = LARGURA - 60, ba = 18;

  if (pct == 0 || !montada) {
    montada = true;
    tft.fillScreen(FUNDO2);
    fonte(INTER_ESTADO);
    textoCentrado(LARGURA / 2, ALTURA / 2 - 40, "ATUALIZANDO", TOPO_C, FUNDO2);
    fonte(INTER_ROTULO);
    textoCentrado(LARGURA / 2, ALTURA / 2 - 14,
                  "aquecedor desligado \xC2\xB7 n\xC3\xA3o desligue a energia",
                  TITULO_C, FUNDO2);
    tft.drawRoundRect(bx, by, bl, ba, 6, TITULO_C);
  }

  tft.fillRoundRect(bx + 2, by + 2, (bl - 4) * pct / 100, ba - 4, 4, CONECT_C);

  char buf[8];
  snprintf(buf, sizeof buf, "%u%%", pct);
  tft.fillRect(LARGURA / 2 - 30, by + ba + 12, 60, 20, FUNDO2);
  fonte(INTER_ESTADO);
  textoCentrado(LARGURA / 2, by + ba + 22, buf, TOPO_C, FUNDO2);

  if (!telaLigada) {          // imagem apagada pelo painel: mostra mesmo assim
    telaLigada = true;
    aplicaLuz();
  }
}

void telaFimAtualizacao() {
  desenhaPainel();
  aplicaLuz();
}

bool painelTempMaxima(float &valor) {
  valor = tempMaxima;
  return temMaxima;
}

void painelDefineTempMaxima(float valor) {
  salvaMaxima(valor);
  if (telaLigada) pintaMaximo();
}

// ---------------------------------------------------------------------------

// Por que a placa reiniciou. "WATCHDOG" significa que o loop travou e o chip
// se reiniciou sozinho - e o cartucho, por regra, voltou bloqueado.
static const char *motivoReinicio() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "ligada na energia";
    case ESP_RST_SW:       return "por software (ex.: fim de atualizacao)";
    case ESP_RST_PANIC:    return "FALHA no firmware (excecao)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "WATCHDOG - o loop travou e a placa se reiniciou";
    case ESP_RST_BROWNOUT: return "QUEDA DE TENSAO na alimentacao do ESP32";
    case ESP_RST_EXT:      return "botao EN";
    default:               return "outro";
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);  // da tempo ao monitor serial de abrir

  // Antes da primeira linha do diario: assim o boot tambem fica gravado.
  registroInicia();

  diario.println();
  diario.println("==================================================");
  diario.println("  Estufa de filamento - painel THERMO");
  diario.println("==================================================");
  diario.printf("Reinicio         : %s\n", motivoReinicio());
  diario.printf("Registro         : %u de %u KB usados na flash\n",
                (unsigned)(registroUsado() / 1024),
                (unsigned)(registroTotal() / 1024));

  avisoInicia();
  diario.printf("Avisos           : %s\n",
                avisoLigado() ? "ligados (ntfy)" : "desligados - sem NTFY_TOPICO no .env");

  // Todo boot avisa: e assim que se fica sabendo de uma queda de energia, de
  // uma atualizacao que entrou ou de um travamento. E em todos o aquecedor
  // volta bloqueado, entao o aviso lembra disso. Fica na fila ate o Wi-Fi subir.
  {
    const char *titulo = "Estufa reiniciou";
    uint8_t prioridade = 4;  // padrao: reinicio inesperado
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON: titulo = "Estufa ligada";     prioridade = 3; break;
      case ESP_RST_SW:      titulo = "Estufa atualizada"; prioridade = 3; break;
      case ESP_RST_EXT:     titulo = "Estufa reiniciada"; prioridade = 3; break;
      default: break;
    }
    char texto[160];
    snprintf(texto, sizeof texto,
             "Motivo: %s. Firmware de %s %s. Aquecedor desligado - autorize "
             "pelo painel.",
             motivoReinicio(), __DATE__, __TIME__);
    avisa(titulo, texto, prioridade);
  }
  diario.printf("Driver da tela   : %s\n", DRIVER);
  diario.printf("Inversao de cor  : %s\n", INVERSAO);
  diario.printf("Barramento SPI   : %d MHz\n", SPI_FREQUENCY / 1000000);
  diario.printf("Pinos da tela    : SCK=%d MOSI=%d CS=%d DC=%d RST=%d\n",
                TFT_SCLK, TFT_MOSI, TFT_CS, TFT_DC, TFT_RST);
  diario.printf("Pinos do sensor  : SPI SCK=%d SDI=%d SDO=%d CS=%d\n",
                PIN_SCK, PIN_SDI, PIN_SDO, PIN_CS);

  controleInicia();
  servidorInicia();  // nao bloqueia: a rede sobe em paralelo ao resto
  carregaMaxima();
  diario.printf("Temp. maxima     : %.1f C\n", tempMaxima);

  diario.printf("SPI do sensor    : %lu kHz por software (cabo longo)\n",
                (unsigned long)(SPI_SENSOR_HZ / 1000));

  sensorOk = iniciaSensor();
  if (sensorOk) {
    const uint8_t id = bme.sensorID();
    diario.printf("Sensor           : SPI, chip ID 0x%02X (%s)\n", id, nomeChip(id));
  } else {
    diario.println("Sensor           : NAO respondeu no SPI");
    diario.println("                   confira SCK, SDI, SDO, CS, 3V3, GND e se as");
    diario.println("                   pontes CSB-VCC e SDO-GND foram desfeitas");
    diario.println("                   seguindo sem ele, tentando de novo a cada 5 s");
  }

  iniciaLuz();  // antes do init: a luz so acende com a imagem pronta
  tft.init();
  // 0 = retrato 320x480. Se a imagem sair de cabeca para baixo, troque por 2
  // (o mesmo retrato girado 180 graus). 1 e 3 sao as duas paisagens, mas o
  // layout de tela.h foi feito para retrato.
  tft.setRotation(0);

  diario.printf("Resolucao em uso : %d x %d\n", tft.width(), tft.height());
  diario.printf("Luz de fundo     : PWM no GPIO%u, brilho %u%%\n", PIN_LUZ, brilho);
  diario.println("--------------------------------------------------");

  desenhaPainel();
  aplicaLuz();

  // --- watchdog -------------------------------------------------------------
  // Se o loop() parar de girar por 5 s, o chip se reinicia sozinho. Sem isso,
  // um travamento com o cartucho a 100% o deixaria a 100% para sempre: todas
  // as protecoes (teto, sensor, "nao esquenta") sao software, e software
  // travado nao protege nada. Reiniciando, o GPIO25 solta, o 10 kOhm segura
  // o gate em zero e o firmware nasce com o aquecedor bloqueado.
  //
  // O core ja vem com o watchdog de tarefas em modo "reinicia" (PANIC=y) e
  // 5 s de prazo; enableLoopWDT() poe o loop() sob ele, e o proprio core o
  // alimenta a cada volta. So o OTA bloqueia mais que isso - ele alimenta
  // por conta propria (servidor.cpp).
  enableLoopWDT();
  // O prazo padrao do core e 5 s, e o WebServer espera ate 5 s por um cliente
  // lento (HTTP_MAX_DATA_WAIT) - um celular saindo do alcance no meio de um
  // pedido derrubaria a placa no meio da secagem. 30 s dao folga para isso e
  // para o OTA, e termicamente nao fazem diferenca: 30 s a mais de cartucho
  // num travamento real nao chegam perto do teto.
  esp_task_wdt_init(WATCHDOG_S, true);
  diario.printf("Watchdog         : ligado - loop travado por %u s reinicia a placa\n",
                WATCHDOG_S);
}

void loop() {
  const uint32_t agora = millis();

  servidorAtende();

  // A rampa do ventilador desce entre leituras, entao o controle roda no
  // ritmo do loop e nao no de 2 s do sensor.
  static uint8_t velAnterior[2] = {255, 255};
  controleTick();
  if (controleVelocidade(0) != velAnterior[0] ||
      controleVelocidade(1) != velAnterior[1]) {
    velAnterior[0] = controleVelocidade(0);
    velAnterior[1] = controleVelocidade(1);
    if (telaLigada) pintaControle();
  }

  // O endereco muda sozinho: conectando -> IP, ou IP -> reconectando. A tela
  // acompanha comparando o texto, para nao repintar a linha a cada volta.
  if (telaEstaLigada()) desenhaRelogio(false);
  animaIcones();

  int codigoAviso;
  if (avisoResultado(codigoAviso)) {
    if (codigoAviso == 200)
      diario.println("Aviso            : entregue ao celular");
    else
      diario.printf("Aviso            : FALHOU (codigo %d)\n", codigoAviso);
  }

  static bool avisouHora = false;
  if (!avisouHora && horaCerta()) {
    avisouHora = true;
    diario.println("Hora             : acertada pela internet (Brasilia)");
  }

  static char enderecoNaTela[40] = "";
  const char *endereco = servidorEndereco();
  if (strcmp(endereco, enderecoNaTela) != 0) {
    snprintf(enderecoNaTela, sizeof enderecoNaTela, "%s", endereco);
    // Com a tela apagada so guarda: ao religar, o rodape e redesenhado.
    if (telaLigada) desenhaRodape(endereco);
  }

  // Le o sensor a cada 2 s, com o cartucho ligado ou nao: no SPI o
  // barramento aguenta os 15 A do aquecedor (no I2C nao aguentava, e a
  // leitura precisava desligar o cartucho por alguns ms).
  if (sensorOk) {
    if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
      ultimaLeitura = agora;

      float temperatura, umidade;
      const bool leu = leSensor(temperatura, umidade);

      // Diferenca grande e plausivel: so vale se a proxima confirmar.
      bool aguardando = false;
      if (leu && temLeitura && fabsf(temperatura - ultimaTemp) > CONFIRMA_C) {
        const bool confirma = pendente
                              && fabsf(temperatura - tempPendente) <= CONFIRMA_C;
        if (!confirma && naoConfirmadas < CONFIRMA_MAX) {
          diario.printf("Sensor           : %.2f C sem confirmar (ultima %.2f C), lendo de novo\n",
                        temperatura, ultimaTemp);
          pendente = true;
          tempPendente = temperatura;
          naoConfirmadas++;
          aguardando = true;
        }
      }

      if (aguardando) {
        // Nem leitura boa nem falha: so espera a proxima, em 2 s.
      } else if (leu) {
        pendente = false;
        naoConfirmadas = 0;
        if (falhasSeguidas > 0) {
          diario.printf("Sensor           : voltou a ler (%.1f C)\n", temperatura);
        }
        falhasSeguidas = 0;
        mostraLeitura(temperatura, umidade);
        registroMedida(temperatura, umidade);
        servidorRegistra(temperatura, umidade);
        controleNovaLeitura(true, temperatura);
        if (telaLigada) pintaControle();
      } else if (++falhasSeguidas < FALHAS_MAX) {
        // Falha isolada: ruido na transferencia; o sensor nao perdeu nada e
        // a proxima leitura vem boa. A partir da segunda seguida, reconfigura:
        // o sensor pode ter reiniciado num tranco de tensao e perdido o modo
        // forcado. Ate la o controle segue com a ultima potencia.
        const bool reconfigura = falhasSeguidas >= 2;
        if (reconfigura) iniciaSensor();
        diario.printf("Sensor           : leitura falhou (%u de %u)%s\n",
                      falhasSeguidas, FALHAS_MAX,
                      reconfigura ? ", reconfigurado" : "");
      } else {
        // Falhou FALHAS_MAX vezes seguidas: cabo solto, provavelmente. Volta
        // a procurar em vez de repetir o ultimo valor, que pareceria um
        // sensor travado.
        diario.printf("Sensor           : %u leituras falhas seguidas - perdido, voltando a procurar\n",
                      FALHAS_MAX);
        falhasSeguidas = 0;
        pendente = false;
        naoConfirmadas = 0;
        sensorOk = false;
        mostraSemSensor();
        servidorSemSensor();
        controleNovaLeitura(false, 0);
        if (telaLigada) pintaControle();
      }
    }
  } else if (agora - ultimaLeitura >= INTERVALO_RETENTATIVA) {
    ultimaLeitura = agora;

    if (iniciaSensor()) {
      sensorOk = true;
      diario.println("Sensor           : apareceu (SPI)");
    }
  }

  delay(20);
}
