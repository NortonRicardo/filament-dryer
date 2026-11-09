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
// Ligacao do BME280 (I2C, pinos declarados abaixo):
//   VIN -> 3V3        SCL -> GPIO22
//   GND -> GND        SDA -> GPIO21
//
// O display e SPI e o sensor e I2C: barramentos diferentes, sem disputa de
// pino e sem precisar coordenar acesso entre os dois.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <esp_system.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Preferences.h>

#include "aviso.h"
#include "controle.h"
#include "diario.h"
#include "registro.h"
#include "servidor.h"
#include "tela.h"

TFT_eSPI tft = TFT_eSPI();
Adafruit_BME280 bme;

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

static const uint8_t PIN_SDA = 21;
static const uint8_t PIN_SCL = 22;

// O modulo responde em 0x76 ou 0x77, conforme o resistor de endereco da placa.
static const uint8_t ENDERECOS[] = {0x76, 0x77};

static const uint32_t INTERVALO_LEITURA = 2000;      // ms entre leituras

// Com o cartucho ligado e a caixa longe do teto, le menos: cada leitura com
// os 4,2 A passando e uma chance de o I2C se corromper, e a caixa sobe
// ~0,07 C/min - em 10 s nao muda nada que o PID ou o teto precisem ver.
// Perto do teto, ou depois de uma falha, volta para INTERVALO_LEITURA.
static const uint32_t INTERVALO_AQUECENDO = 10000;
static const float    MARGEM_TETO = 10.0f;   // C abaixo do teto = "longe"

// Com o cartucho ligado o I2C se corrompe de vez em quando (testado em
// 09/11/2026, ja com o VIN ressoldado e os pull-ups de 4,7 k). Entao:
//
// 1. Abaixo de 100% de potencia, a leitura espera o trecho da janela de PWM
//    em que o cartucho ja esta desligado: sem pausa nenhuma. LEITURA_MS e o
//    quanto esse trecho precisa durar (medicao de ~10 ms + I2C a 20 kHz).
//    Se em ESPERA_TRECHO_MS ele nao vier, le assim mesmo.
// 2. A 100%, le com o cartucho ligado. Se falhar, escala:
//      falhas 0 e 1 : cartucho ligado
//      falha 2      : pausa curta - a medicao acontece com ele ligado e so
//                     a busca dos dados (~8 ms) e feita com ele desligado,
//                     depois de SILENCIO_CURTO_MS de folga
//      falhas 3 e 4 : pausa longa - desligado por SILENCIO_MS antes e
//                     durante a leitura inteira, reconfigurando se falhar
//    Carga resistiva corta na hora; a folga e para a fonte e a fiacao
//    acomodarem.
static const uint32_t LEITURA_MS = 50;
static const uint32_t ESPERA_TRECHO_MS = 4000;
static const uint8_t  FALHA_PAUSA_CURTA = 2;
static const uint8_t  FALHA_PAUSA_LONGA = 3;
static const uint32_t SILENCIO_CURTO_MS = 5;
static const uint32_t SILENCIO_MS = 20;

// Uma leitura corrompida pode dar um valor plausivel (30,0 C entre leituras
// de 29,5): passa pelos filtros de salto. A caixa muda ~0,07 C/min, entao
// uma diferenca maior que CONFIRMA_C so e aceita se a leitura seguinte, 2 s
// depois, confirmar. Depois de CONFIRMA_MAX leituras sem confirmar (mudanca
// rapida de verdade, como porta aberta) aceita a ultima.
static const float   CONFIRMA_C = 0.3f;
static const uint8_t CONFIRMA_MAX = 3;

// I2C a 20 kHz em vez dos 100 kHz padrao. O sensor fica a ~1 m da placa, e
// I2C foi feito para centimetros: cabo longo tem capacitancia, deixa as bordas
// do sinal moles e pega o ruido do cartucho chaveando 4,2 A. Mais devagar, o
// sinal tem tempo de assentar. Para uma leitura a cada 2 s sobra velocidade.
static const uint32_t I2C_FREQ = 20000;

// Leituras falhas seguidas antes de dar o sensor por perdido. Uma falha
// isolada (ruido, ou o sensor reiniciado por um tranco de tensao) nao deve
// derrubar a secagem; um sensor solto de verdade falha sempre e e pego em
// 3 x 2 s = 6 s.
static const uint8_t FALHAS_MAX = 5;

// Prazo do watchdog. Ver o comentario no fim do setup().
static const uint32_t WATCHDOG_S = 30;
static const uint32_t INTERVALO_RETENTATIVA = 5000;  // ms entre buscas do sensor

static bool     sensorOk = false;
static uint8_t  enderecoSensor = 0;
static uint32_t ultimaLeitura = 0;
static uint8_t  falhasSeguidas = 0;

// Leitura com diferenca grande esperando confirmacao (ver CONFIRMA_C).
static bool     pendente = false;
static float    tempPendente = 0;
static uint8_t  naoConfirmadas = 0;

enum ModoLeitura { COM_CARTUCHO, PAUSA_CURTA, PAUSA_LONGA };

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

// Lista quem responde no barramento. Se o sensor nao aparecer, isso separa
// "ligacao errada" de "endereco diferente do esperado".
static void varreI2C() {
  diario.println("Varredura I2C:");
  uint8_t achados = 0;

  for (uint8_t endereco = 1; endereco < 127; endereco++) {
    Wire.beginTransmission(endereco);
    if (Wire.endTransmission() == 0) {
      diario.printf("  0x%02X respondeu\n", endereco);
      achados++;
    }
  }

  if (achados == 0) {
    diario.println("  ninguem respondeu - confira SDA, SCL, GND e os 3V3");
  }
}

static bool iniciaSensor() {
  for (uint8_t endereco : ENDERECOS) {
    if (!bme.begin(endereco, &Wire)) continue;
    Wire.setClock(I2C_FREQ);  // a biblioteca pode reabrir o barramento

    enderecoSensor = endereco;

    // MODO FORCADO em vez de continuo: o sensor dorme entre as leituras e
    // nao se aquece. Em modo continuo o proprio chip esquenta e puxa a
    // temperatura para cima alguns decimos - erro que nao da para corrigir
    // depois, e justamente temperatura e o que mais importa aqui.
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // temperatura
                    Adafruit_BME280::SAMPLING_NONE, // pressao: nao interessa
                    Adafruit_BME280::SAMPLING_X1,   // umidade
                    Adafruit_BME280::FILTER_OFF);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Umidade absoluta - quanta agua existe mesmo no ar, em gramas por metro
// cubico. Formula de Magnus, a mesma do projeto DryFilaBox do Marlon Nardi.
//
// Por que isso importa numa estufa, e muito:
//
//   A umidade RELATIVA diz o quanto o ar esta cheio em relacao ao que ele
//   AGUENTA naquela temperatura. E ar quente aguenta muito mais agua. Entao
//   so de aquecer a caixa a UR despenca, sem uma unica gota ter saido.
//
//   Exemplo real: 25 C com 60% de UR tem 13,8 g/m3 de agua. Aqueca para
//   45 C sem tirar nada e a UR marca 21% -- parece seco. Mas continuam os
//   mesmos 13,8 g/m3 ali dentro.
//
//   A umidade ABSOLUTA so cai quando a agua realmente deixa a caixa. E ela,
//   nao a relativa, que diz se a secagem esta funcionando.
//
// Conferida contra tabela: 20 C e 50% dao 8,64 g/m3 (tabela: 8,65).
static float umidadeAbsoluta(float tempC, float umidadeRel) {
  const float pressaoSat = 6.112f * expf((17.67f * tempC) / (tempC + 243.5f));
  return pressaoSat * umidadeRel * 2.1674f / (273.15f + tempC);
}

// ---------------------------------------------------------------------------
// Validacao da leitura
//
// Quando o cartucho chaveia 4,2 A, o degrau de corrente acopla no cabo do
// I2C e a transferencia se corrompe. O BME280 nao avisa: a biblioteca devolve
// os bytes que leu, e a compensacao do datasheet transforma lixo em numero.
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

// Quem chama religa o cartucho (controleSilenciaSaida(false)) depois.
static bool leSensor(float &temperatura, float &umidade, ModoLeitura modo) {
  if (modo == PAUSA_LONGA) {
    controleSilenciaSaida(true);
    delay(SILENCIO_MS);
  }
  if (!bme.takeForcedMeasurement()) return false;

  if (modo == PAUSA_CURTA) {
    controleSilenciaSaida(true);
    delay(SILENCIO_CURTO_MS);
  }
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
// Regra em todo este bloco: escolher a fonte logo antes de cada desenho ou
// medicao. setTextFont() zera a free font e setFreeFont() a repoe, entao
// misturar as duas sem reescolher daria a fonte errada sem erro de compilacao.
// ---------------------------------------------------------------------------

// Desenha texto com espaco extra entre as letras - metade do visual deste
// layout vem dai, e a TFT_eSPI nao tem letter-spacing.
//
// Resolve de passagem dois caracteres que as fontes nao tem, por cobrirem so
// 0x20 a 0x7E: "·" vira um ponto desenhado e "Ø" vira um O cortado.
//
// Com medir=true nao desenha nada, so devolve a largura - e assim que se
// centraliza ou alinha a direita.
static int16_t textoEspacado(int16_t x, int16_t y, const char *texto,
                             int16_t espaco, uint16_t cor, uint16_t fundo,
                             bool medir = false) {
  const int16_t inicio = x;
  const int16_t altura = tft.fontHeight();
  bool primeiro = true;

  if (!medir) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(cor, fundo);
  }

  for (const uint8_t *p = (const uint8_t *)texto; *p; p++) {
    if (!primeiro) x += espaco;
    primeiro = false;

    if (p[0] == 0xC2 && p[1] == 0xB7) {  // "·"
      if (!medir) tft.fillCircle(x + 2, y, 2, cor);
      x += 5;
      p++;
      continue;
    }

    char letra[2] = {0, 0};
    bool cortado = false;

    if (p[0] == 0xC3 && p[1] == 0x98) {  // "Ø"
      letra[0] = 'O';
      cortado = true;
      p++;
    } else {
      letra[0] = (char)p[0];
    }

    const int16_t largura = tft.textWidth(letra);
    if (!medir) {
      tft.drawString(letra, x, y);
      if (cortado) {
        tft.drawLine(x + largura - 1, y - altura / 2 + 2,
                     x + 1, y + altura / 2 - 2, cor);
      }
    }
    x += largura;
  }

  return x - inicio;
}

static void textoCentrado(int16_t cx, int16_t y, const char *texto,
                          int16_t espaco, uint16_t cor, uint16_t fundo) {
  const int16_t largura = textoEspacado(0, 0, texto, espaco, cor, fundo, true);
  textoEspacado(cx - largura / 2, y, texto, espaco, cor, fundo);
}

// Posicoes dentro do cartao, relativas a altura dele: rotulo no alto,
// valor no meio, detalhe embaixo.
//
// A faixa reservada ao valor e o que se apaga a cada leitura. Apagar so ela,
// e nao o cartao inteiro, e o que evita piscar. Ela cobre a altura dos
// digitos da FreeSansBold24pt (~34 px) com folga, sem encostar no rotulo nem
// no detalhe.
static int16_t yRotulo(const Cartao &c)     { return c.y + 20; }
static int16_t yValor(const Cartao &c)      { return c.y + c.a / 2 + 4; }
static int16_t faixaTopo(const Cartao &c)   { return yValor(c) - 24; }
static int16_t faixaAltura(const Cartao &)  { return 48; }
static int16_t yDetalhe(const Cartao &c)    { return c.y + c.a - 18; }

// Unidade ao lado do numero, em fonte pequena e cinza. O grau nao existe na
// fonte, entao vai desenhado como dois circulos - que e o que ele e.
// Template porque desenha tanto na tela quanto no sprite: na TFT_eSPI so
// drawPixel e virtual, entao passar o sprite como TFT_eSPI& desenharia na
// tela.
template <typename Alvo>
static int16_t desenhaUnidade(Alvo &a, int16_t x, int16_t y, const Cartao &c,
                              bool medir) {
  int16_t largura = 0;

  if (c.grau) {
    if (!medir) {
      a.drawCircle(x + 3, y - 5, 3, MUDO);
      a.drawCircle(x + 3, y - 5, 2, MUDO);
    }
    x += 9;
    largura += 9;
  }

  a.setTextFont(2);
  largura += a.textWidth(c.unidade);
  if (!medir) {
    a.setTextDatum(ML_DATUM);
    a.setTextColor(MUDO, CARTAO);
    a.drawString(c.unidade, x, y);
  }
  return largura;
}

// O valor e montado num sprite fora da tela e empurrado de uma vez. Apagar a
// faixa direto na tela e redesenhar deixava um quadro em branco visivel - a
// piscada na troca de valor. Todos os cartoes tem a mesma largura, entao um
// sprite so serve para todos (~13 KB).
static TFT_eSprite faixa = TFT_eSprite(&tft);
static bool faixaOk = false;

// O que esta desenhado em cada cartao. Valor igual nao redesenha.
static char valorNaTela[N_CARTOES][12];
static bool dadoNaTela[N_CARTOES];

static void esqueceValor(uint8_t i) { valorNaTela[i][0] = '\0'; }

template <typename Alvo>
static void montaValor(Alvo &a, int16_t ox, int16_t oy, const Cartao &c,
                       const char *valor, bool temDado) {
  a.fillRect(ox, oy, c.l - 6, faixaAltura(c), CARTAO);

  a.setFreeFont(&FreeSansBold24pt7b);
  const int16_t larguraValor = a.textWidth(valor);
  const int16_t larguraUnidade = temDado ? desenhaUnidade(a, 0, 0, c, true) : 0;
  const int16_t total = larguraValor + (temDado ? 3 + larguraUnidade : 0);

  const int16_t x = ox + (c.l - 6) / 2 - total / 2;
  const int16_t y = oy + faixaAltura(c) / 2;

  // desenhaUnidade() trocou a fonte ao medir: reescolhe antes de desenhar.
  a.setFreeFont(&FreeSansBold24pt7b);
  a.setTextDatum(ML_DATUM);
  a.setTextColor(temDado ? c.cor : MUDO, CARTAO);
  a.drawString(valor, x, y);

  if (temDado) desenhaUnidade(a, x + larguraValor + 3, y, c, false);
}

// Numero grande mais a unidade, centrados juntos no cartao. Sem dado, "--"
// em cinza: o cartao existe no layout mas nao inventa leitura.
static void desenhaValor(uint8_t i, const char *valor, bool temDado) {
  if (dadoNaTela[i] == temDado && strcmp(valorNaTela[i], valor) == 0) return;
  snprintf(valorNaTela[i], sizeof valorNaTela[i], "%s", valor);
  dadoNaTela[i] = temDado;

  const Cartao &c = CARTOES[i];

  if (!faixaOk) {
    faixa.setColorDepth(16);
    faixaOk = faixa.createSprite(c.l - 6, faixaAltura(c)) != nullptr;
  }

  if (faixaOk) {
    montaValor(faixa, 0, 0, c, valor, temDado);
    faixa.pushSprite(c.x + 3, faixaTopo(c));
  } else {
    montaValor(tft, c.x + 3, faixaTopo(c), c, valor, temDado);  // sem memoria: pisca, mas funciona
  }
}

static void desenhaCartao(uint8_t i) {
  const Cartao &c = CARTOES[i];
  esqueceValor(i);

  tft.fillRoundRect(c.x, c.y, c.l, c.a, 9, CARTAO);
  tft.drawRoundRect(c.x, c.y, c.l, c.a, 9, BORDA);

  tft.setTextFont(1);
  textoCentrado(c.x + c.l / 2, yRotulo(c), c.rotulo, 2, MUDO, CARTAO);
  textoCentrado(c.x + c.l / 2, yDetalhe(c), c.detalhe, 1, MUDO, CARTAO);
}

static void telaFixa() {
  tft.fillScreen(FUNDO);
  for (uint8_t i = 0; i < N_CARTOES; i++) desenhaCartao(i);
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
  const Cartao &c = CARTOES[CARTAO_TEMP];
  char buf[20];

  tft.fillRect(c.x + 3, yDetalhe(c) - 9, c.l - 6, 18, CARTAO);
  tft.setTextFont(2);

  if (temMaxima) {
    snprintf(buf, sizeof buf, "MAX %.1f C", tempMaxima);
    textoCentrado(c.x + c.l / 2, yDetalhe(c), buf, 1, VERDE, CARTAO);
  } else {
    textoCentrado(c.x + c.l / 2, yDetalhe(c), "MAX --", 1, MUDO, CARTAO);
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

// Ventiladores e aquecedor, com o que o controle decidiu. O detalhe do cartao
// grande e reescrito junto, porque ele carrega o estado do aquecedor.
// Selo de estado na linha de baixo do cartao: borda arredondada com o texto
// dentro, so informativo. Verde ligado, vermelho desligado - o mesmo selo da
// pagina web.
static void desenhaSelo(uint8_t i, const char *texto, uint16_t cor) {
  const Cartao &c = CARTOES[i];
  const int16_t cy = yDetalhe(c);
  const int16_t a = 18;

  tft.fillRect(c.x + 3, cy - 10, c.l - 6, 20, CARTAO);
  tft.setTextFont(1);
  const int16_t lt = textoEspacado(0, 0, texto, 1, cor, CARTAO, true);
  const int16_t l = lt + 18;
  const int16_t x = c.x + c.l / 2 - l / 2;

  tft.drawRoundRect(x, cy - a / 2, l, a, a / 2, cor);
  textoEspacado(x + 9, cy, texto, 1, cor, CARTAO);
}

// Ventiladores e aquecedor, com o que o controle decidiu. No boot tudo esta
// desligado, e os tres cartoes mostram o selo vermelho ate o painel web ligar.
static void pintaControle() {
  char buf[16];

  for (uint8_t i = 0; i < 2; i++) {
    const uint8_t pct = controleVelocidade(i);
    const uint8_t cartao = CARTAO_FAN1 + i;
    snprintf(buf, sizeof buf, "%u", pct);
    desenhaValor(cartao, buf, pct > 0);
    desenhaSelo(cartao, pct > 0 ? "LIGADO" : "DESLIGADO",
                pct > 0 ? VERDE : VERMELHO);
  }

  snprintf(buf, sizeof buf, "%.0f", controleConfig().alvo);
  desenhaValor(CARTAO_HOTEND, buf, controleAquecedorLigado());

  // Ligado = liberado no painel, mesmo que agora esteja no alvo sem aquecer;
  // o numero fica laranja quando aquece de fato. Protecao aparece em vez de
  // DESLIGADO para a falha nao passar por desligamento normal.
  if (controleEstado() == EST_FALHA)  desenhaSelo(CARTAO_HOTEND, "PROTECAO", VERMELHO);
  else if (controleAutorizado())      desenhaSelo(CARTAO_HOTEND, "LIGADO", VERDE);
  else                                desenhaSelo(CARTAO_HOTEND, "DESLIGADO", VERMELHO);
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
    tft.setTextFont(2);
    textoCentrado(LARGURA / 2, Y_RODAPE, endereco, 1, FUNDO, LARANJA);
    return;
  }

  tft.fillRect(0, topo, LARGURA, ALTURA_RODAPE, FUNDO);
  tft.setTextFont(2);
  textoCentrado(LARGURA / 2, Y_RODAPE, endereco, 1, RODAPE, FUNDO);
}

// Relogio pequeno no canto direito do topo. Horas e minutos so: redesenha uma
// vez por minuto. "--:--" ate a hora chegar pela internet.
static char relogioNaTela[8] = "";

static void desenhaRelogio(bool forca) {
  char texto[8] = "--:--";
  if (horaCerta()) {
    const time_t agora = time(nullptr);
    struct tm t;
    localtime_r(&agora, &t);
    strftime(texto, sizeof texto, "%H:%M", &t);
  }
  if (!forca && strcmp(texto, relogioNaTela) == 0) return;
  snprintf(relogioNaTela, sizeof relogioNaTela, "%s", texto);

  tft.fillRect(LARGURA - MARGEM - 60, 0, 60, ALTURA_TOPO, FUNDO);
  tft.setTextFont(2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(MUDO, FUNDO);
  tft.drawString(texto, LARGURA - MARGEM, Y_TOPO);
}

// Painel inteiro: no boot e ao religar a imagem.
static void desenhaPainel() {
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
    tft.fillScreen(FUNDO);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEXTO, FUNDO);
    tft.drawString("ATUALIZANDO", LARGURA / 2, ALTURA / 2 - 40, 4);
    tft.setTextColor(MUDO, FUNDO);
    tft.drawString("aquecedor desligado - nao desligue a energia",
                   LARGURA / 2, ALTURA / 2 - 12, 2);
    tft.drawRoundRect(bx, by, bl, ba, 6, BORDA);
  }

  tft.fillRoundRect(bx + 2, by + 2, (bl - 4) * pct / 100, ba - 4, 4, CIANO);

  char buf[8];
  snprintf(buf, sizeof buf, "%u%%", pct);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXTO, FUNDO);
  tft.setTextPadding(60);
  tft.drawString(buf, LARGURA / 2, by + ba + 22, 4);
  tft.setTextPadding(0);

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
  diario.printf("Pinos do sensor  : SDA=%d SCL=%d\n", PIN_SDA, PIN_SCL);

  controleInicia();
  servidorInicia();  // nao bloqueia: a rede sobe em paralelo ao resto
  carregaMaxima();
  diario.printf("Temp. maxima     : %.1f C\n", tempMaxima);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FREQ);
  diario.printf("I2C              : %lu kHz (cabo longo ate o sensor)\n",
                (unsigned long)(I2C_FREQ / 1000));
  varreI2C();

  sensorOk = iniciaSensor();
  if (sensorOk) {
    const uint8_t id = bme.sensorID();
    diario.printf("Sensor           : 0x%02X, chip ID 0x%02X (%s)\n",
                  enderecoSensor, id, nomeChip(id));
    if (id != 0x60) {
      diario.println("ATENCAO: nao e um BME280. A umidade vai sair invalida.");
    }
  } else {
    diario.println("Sensor           : NAO encontrado em 0x76 nem 0x77");
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

  // Cartucho ligado, caixa longe do teto e nada pendente: le a cada 10 s.
  // Em qualquer outro caso, a cada 2 s.
  const bool leituraEspacada = falhasSeguidas == 0 && !pendente
                               && controleAquecedorLigado()
                               && ultimaTemp < TETO_ABSOLUTO - MARGEM_TETO;
  const uint32_t intervalo = leituraEspacada ? INTERVALO_AQUECENDO
                                             : INTERVALO_LEITURA;

  // Abaixo de 100% espera o trecho em que o PWM ja desligou o cartucho -
  // ate ESPERA_TRECHO_MS; depois disso le assim mesmo.
  const bool esperaTrecho = !controleSaidaQuieta(LEITURA_MS)
                            && controleTemTrechoQuieto(LEITURA_MS)
                            && agora - ultimaLeitura < intervalo + ESPERA_TRECHO_MS;

  if (sensorOk) {
    if (agora - ultimaLeitura >= intervalo && !esperaTrecho) {
      ultimaLeitura = agora;

      // Escala conforme as falhas seguidas (ver FALHA_PAUSA_CURTA). O PID
      // nem fica sabendo das pausas: a potencia pedida nao muda.
      const ModoLeitura modo = falhasSeguidas >= FALHA_PAUSA_LONGA ? PAUSA_LONGA
                             : falhasSeguidas >= FALHA_PAUSA_CURTA ? PAUSA_CURTA
                             : COM_CARTUCHO;

      float temperatura, umidade;
      bool leu = leSensor(temperatura, umidade, modo);

      // Reconfigurar (bme.begin) espera a calibracao num laco sem limite de
      // tempo: com o barramento corrompido pelo cartucho, nao sai nunca. Por
      // isso so acontece na pausa longa, com ele desligado.
      const bool reconfigurou = !leu && modo == PAUSA_LONGA
                                && falhasSeguidas + 1 < FALHAS_MAX;
      if (reconfigurou) iniciaSensor();

      // Religa ja, sem esperar a proxima volta do loop.
      controleSilenciaSaida(false);
      controleTick();

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
        // Falha isolada: quase sempre o cartucho corrompendo a transferencia;
        // o sensor nao perdeu nada e a proxima leitura vem boa. Tenta de novo
        // em 2 s, escalando a pausa (e, na longa, reconfigurando - o sensor
        // pode ter reiniciado num tranco de tensao). Ate la o controle segue
        // com a ultima potencia.
        const char *proxima = falhasSeguidas >= FALHA_PAUSA_LONGA ? "pausa longa"
                            : falhasSeguidas >= FALHA_PAUSA_CURTA ? "pausa curta"
                            : "cartucho ligado";
        diario.printf("Sensor           : leitura falhou (%u de %u)%s, a proxima com %s\n",
                      falhasSeguidas, FALHAS_MAX,
                      reconfigurou ? ", reconfigurado" : "", proxima);
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
      diario.printf("Sensor apareceu em 0x%02X\n", enderecoSensor);
    }
  }

  delay(20);
}
