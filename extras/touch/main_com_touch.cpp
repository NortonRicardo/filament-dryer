// ---------------------------------------------------------------------------
// Estufa de filamento - painel THERMO
//
// Le o BME280 e mostra temperatura e umidade no display, no layout de
// cartoes. Os cartoes de ventilador e aquecedor mostram o que o controle
// decidiu (ver controle.h).
//
// Display: 4" TFT SPI 480x320 V1.1, ST7796S, header de 14 pinos.
// Ligacao (os pinos ficam em platformio.ini, nao aqui):
//   VCC       -> 5V          SCK       -> GPIO18      RESET -> GPIO26
//   GND       -> GND         SDI(MOSI) -> GPIO23      CS    -> GPIO13
//   LED       -> 3V3                                  DC/RS -> GPIO27
//
// Sao oito fios. O SDO(MISO) do display fica sem ligar: nada e lido da tela.
//
// Touch (XPT2046), no mesmo SPI do display (pinos em platformio.ini):
//   T_CLK -> GPIO18 (junto com o SCK)     T_CS -> GPIO16
//   T_DIN -> GPIO23 (junto com o SDI)     T_DO -> GPIO19
//
// Tocar no cartao de temperatura abre um teclado para digitar a temperatura
// maxima. Ela fica salva na memoria e aparece embaixo do valor, em verde.
// Tocar no FAN 02 abre a configuracao da circulacao: liga/desliga, maxima e
// minima (ver tela_fan2.h).
//
// Ligacao do BME280 (I2C, pinos declarados abaixo):
//   VIN -> 3V3        SCL -> GPIO22
//   GND -> GND        SDA -> GPIO21
//
// O display e SPI e o sensor e I2C: barramentos diferentes, sem disputa de
// pino e sem precisar coordenar acesso entre os dois.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Preferences.h>

#include "controle.h"
#include "teclado.h"
#include "tela.h"
#include "tela_fan2.h"
#include "toque.h"

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
static const uint32_t INTERVALO_RETENTATIVA = 5000;  // ms entre buscas do sensor

static bool     sensorOk = false;
static uint8_t  enderecoSensor = 0;
static uint32_t ultimaLeitura = 0;

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
  Serial.println("Varredura I2C:");
  uint8_t achados = 0;

  for (uint8_t endereco = 1; endereco < 127; endereco++) {
    Wire.beginTransmission(endereco);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X respondeu\n", endereco);
      achados++;
    }
  }

  if (achados == 0) {
    Serial.println("  ninguem respondeu - confira SDA, SCL, GND e os 3V3");
  }
}

static bool iniciaSensor() {
  for (uint8_t endereco : ENDERECOS) {
    if (!bme.begin(endereco, &Wire)) continue;

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

static bool leSensor(float &temperatura, float &umidade) {
  if (!bme.takeForcedMeasurement()) return false;

  temperatura = bme.readTemperature();
  umidade     = bme.readHumidity();

  return !isnan(temperatura) && !isnan(umidade);
}

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
static int16_t desenhaUnidade(int16_t x, int16_t y, const Cartao &c,
                              bool medir) {
  int16_t largura = 0;

  if (c.grau) {
    if (!medir) {
      tft.drawCircle(x + 3, y - 5, 3, MUDO);
      tft.drawCircle(x + 3, y - 5, 2, MUDO);
    }
    x += 9;
    largura += 9;
  }

  tft.setTextFont(2);
  largura += tft.textWidth(c.unidade);
  if (!medir) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(MUDO, CARTAO);
    tft.drawString(c.unidade, x, y);
  }
  return largura;
}

// Numero grande mais a unidade, centrados juntos no cartao. Sem dado, "--"
// em cinza: o cartao existe no layout mas nao inventa leitura.
static void desenhaValor(uint8_t i, const char *valor, bool temDado) {
  const Cartao &c = CARTOES[i];
  const int16_t y = yValor(c);

  tft.fillRect(c.x + 3, faixaTopo(c), c.l - 6, faixaAltura(c), CARTAO);

  tft.setFreeFont(&FreeSansBold24pt7b);
  const int16_t larguraValor = tft.textWidth(valor);
  const int16_t larguraUnidade = temDado ? desenhaUnidade(0, 0, c, true) : 0;
  const int16_t total = larguraValor + (temDado ? 3 + larguraUnidade : 0);

  int16_t x = c.x + c.l / 2 - total / 2;

  // desenhaUnidade() trocou a fonte ao medir: reescolhe antes de desenhar.
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(temDado ? c.cor : MUDO, CARTAO);
  tft.drawString(valor, x, y);

  if (temDado) desenhaUnidade(x + larguraValor + 3, y, c, false);
}

static void desenhaCartao(uint8_t i) {
  const Cartao &c = CARTOES[i];

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

// Qual tela esta a mostra. Fora do painel o loop continua lendo o sensor e
// rodando o controle, mas nao desenha o painel por cima da outra tela.
enum TelaAtiva { TELA_PAINEL, TELA_TECLADO, TELA_FAN2 };
static TelaAtiva telaAtiva = TELA_PAINEL;

// O que o teclado aberto esta editando. Decide onde o valor e gravado e para
// qual tela se volta depois.
enum Edicao { ED_TEMP_MAX, ED_FAN2_MAX, ED_FAN2_MIN };
static Edicao edicao = ED_TEMP_MAX;

static bool painelVisivel() { return telaAtiva == TELA_PAINEL; }

static float ultimaTemp = 0, ultimaUmid = 0;
static bool temLeitura = false;

// --- temperatura maxima -------------------------------------------------------
// Digitada pelo toque no cartao de temperatura e salva na memoria, entao
// sobrevive a um reinicio. Por enquanto so e guardada e mostrada: ainda nao
// entra na regra do controle.
static const char *NVS_PAINEL = "painel";
static const char *NVS_TMAX   = "tmax";
static const float TMAX_MIN   = 20.0f;   // a faixa vai ate o teto do controle

static float tempMaxima = 0;
static bool  temMaxima  = false;

static void carregaMaxima() {
  Preferences nvs;
  nvs.begin(NVS_PAINEL, true);
  temMaxima = nvs.isKey(NVS_TMAX);
  if (temMaxima) tempMaxima = nvs.getFloat(NVS_TMAX, 0);
  nvs.end();
}

static void salvaMaxima(float valor) {
  tempMaxima = valor;
  temMaxima = true;

  Preferences nvs;
  nvs.begin(NVS_PAINEL, false);
  nvs.putFloat(NVS_TMAX, valor);
  nvs.end();

  Serial.printf("Temp. maxima     : %.1f C (salva)\n", valor);
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

static void pintaMedidas() {
  char buf[16];

  if (!temLeitura) {
    desenhaValor(CARTAO_TEMP, "--", false);
    desenhaValor(CARTAO_UMID, "--", false);
    return;
  }

  formataValor(ultimaTemp, buf, sizeof buf);
  desenhaValor(CARTAO_TEMP, buf, true);

  formataValor(ultimaUmid, buf, sizeof buf);
  desenhaValor(CARTAO_UMID, buf, true);

  // Linha de detalhe do cartao de umidade: a absoluta, em g/m3. O numero
  // grande e a relativa; esta embaixo e a que diz se a agua esta saindo.
  const Cartao &c = CARTOES[CARTAO_UMID];
  snprintf(buf, sizeof buf, "ABS %.1f g/m3",
           umidadeAbsoluta(ultimaTemp, ultimaUmid));
  tft.fillRect(c.x + 3, yDetalhe(c) - 6, c.l - 6, 13, CARTAO);
  tft.setTextFont(1);
  textoCentrado(c.x + c.l / 2, yDetalhe(c), buf, 1, MUDO, CARTAO);
}

static void mostraLeitura(float temperatura, float umidade) {
  ultimaTemp = temperatura;
  ultimaUmid = umidade;
  temLeitura = true;

  if (painelVisivel()) pintaMedidas();

  Serial.printf("temperatura %.2f C | umidade %.2f %%\n", temperatura, umidade);
}

static void mostraSemSensor() {
  temLeitura = false;
  if (painelVisivel()) pintaMedidas();
}

// Ventiladores e aquecedor, com o que o controle decidiu. O detalhe do cartao
// grande e reescrito junto, porque ele carrega o estado do aquecedor.
static void pintaControle() {
  char buf[16];

  for (uint8_t i = 0; i < 2; i++) {
    const uint8_t pct = controleVelocidade(i);
    snprintf(buf, sizeof buf, "%u", pct);
    desenhaValor(CARTAO_FAN1 + i, buf, pct > 0);
  }

  const Cartao &c = CARTOES[CARTAO_HOTEND];
  snprintf(buf, sizeof buf, "%.0f", controleConfig().alvo);
  desenhaValor(CARTAO_HOTEND, buf, controleAquecedorLigado());

  // Apaga e reescreve so a linha do detalhe.
  tft.fillRect(c.x + 3, yDetalhe(c) - 6, c.l - 6, 13, CARTAO);
  tft.setTextFont(1);
  textoCentrado(c.x + c.l / 2, yDetalhe(c), controleEstadoTexto(), 1,
                controleAquecedorLigado() ? LARANJA : MUDO, CARTAO);
}

// Painel inteiro: no boot e ao voltar do teclado.
static void desenhaPainel() {
  telaFixa();
  pintaControle();
  pintaMedidas();  // "--" ate a primeira leitura chegar
  pintaMaximo();
}

static bool dentroDoCartao(uint8_t i, uint16_t x, uint16_t y) {
  const Cartao &c = CARTOES[i];
  return x >= c.x && x < c.x + c.l && y >= c.y && y < c.y + c.a;
}

static void abreTeclado(Edicao e) {
  const Config c = controleConfig();
  edicao = e;
  telaAtiva = TELA_TECLADO;

  switch (e) {
    case ED_TEMP_MAX:
      tecladoAbre("TEMPERATURA MAXIMA", tempMaxima, temMaxima,
                  TMAX_MIN, TETO_ABSOLUTO, "C", true, true);
      break;
    // As faixas ja impedem minima acima da maxima: o teclado recusa o valor
    // na hora, com a faixa certa na tela, em vez de o controle recusar
    // depois sem explicar.
    case ED_FAN2_MAX:
      tecladoAbre("VELOCIDADE MAXIMA", c.fan2Max, true,
                  c.fan2Min, 100, "%", false, false);
      break;
    case ED_FAN2_MIN:
      tecladoAbre("VELOCIDADE MINIMA", c.fan2Min, true,
                  c.pwmMin, c.fan2Max, "%", false, false);
      break;
  }
}

static void salvaFan2(Edicao e, float valor) {
  Config c = controleConfig();
  if (e == ED_FAN2_MAX) c.fan2Max = (uint8_t)valor;
  else                  c.fan2Min = (uint8_t)valor;

  const char *porque;
  if (!controleDefineConfig(c, &porque)) {
    Serial.printf("FAN 02           : nao salvou (%s)\n", porque);
  }
}

static void abreFan2() {
  telaAtiva = TELA_FAN2;
  fan2Abre();
}

static void voltaAoPainel() {
  telaAtiva = TELA_PAINEL;
  desenhaPainel();
}

static void trataToque(uint16_t x, uint16_t y) {
  switch (telaAtiva) {
    case TELA_PAINEL:
      if (dentroDoCartao(CARTAO_TEMP, x, y))      abreTeclado(ED_TEMP_MAX);
      else if (dentroDoCartao(CARTAO_FAN2, x, y)) abreFan2();
      return;

    case TELA_FAN2:
      switch (fan2Toque(x, y)) {
        case FAN2_VOLTA:     voltaAoPainel(); break;
        case FAN2_EDITA_MAX: abreTeclado(ED_FAN2_MAX); break;
        case FAN2_EDITA_MIN: abreTeclado(ED_FAN2_MIN); break;
        case FAN2_SEGUE:     break;
      }
      return;

    case TELA_TECLADO: {
      float valor;
      const ResultadoTeclado r = tecladoToque(x, y, valor);
      if (r == TECLADO_SEGUE) return;

      if (edicao == ED_TEMP_MAX) {
        if (r == TECLADO_OK) salvaMaxima(valor);
        voltaAoPainel();
      } else {
        // Maxima e minima do FAN 02 voltam para a tela dele, nao ao painel.
        if (r == TECLADO_OK) salvaFan2(edicao, valor);
        abreFan2();
      }
      return;
    }
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(400);  // da tempo ao monitor serial de abrir

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  Estufa de filamento - painel THERMO");
  Serial.println("==================================================");
  Serial.printf("Driver da tela   : %s\n", DRIVER);
  Serial.printf("Inversao de cor  : %s\n", INVERSAO);
  Serial.printf("Barramento SPI   : %d MHz\n", SPI_FREQUENCY / 1000000);
  Serial.printf("Pinos da tela    : SCK=%d MOSI=%d CS=%d DC=%d RST=%d\n",
                TFT_SCLK, TFT_MOSI, TFT_CS, TFT_DC, TFT_RST);
  Serial.printf("Pinos do touch   : T_CS=%d T_DO(MISO)=%d, SCK e MOSI com a tela\n",
                TOUCH_CS, TFT_MISO);
  Serial.printf("Pinos do sensor  : SDA=%d SCL=%d\n", PIN_SDA, PIN_SCL);

  controleInicia();
  carregaMaxima();
  if (temMaxima) Serial.printf("Temp. maxima     : %.1f C\n", tempMaxima);
  else           Serial.println("Temp. maxima     : nao definida");

  Wire.begin(PIN_SDA, PIN_SCL);
  varreI2C();

  sensorOk = iniciaSensor();
  if (sensorOk) {
    const uint8_t id = bme.sensorID();
    Serial.printf("Sensor           : 0x%02X, chip ID 0x%02X (%s)\n",
                  enderecoSensor, id, nomeChip(id));
    if (id != 0x60) {
      Serial.println("ATENCAO: nao e um BME280. A umidade vai sair invalida.");
    }
  } else {
    Serial.println("Sensor           : NAO encontrado em 0x76 nem 0x77");
    Serial.println("                   seguindo sem ele, tentando de novo a cada 5 s");
  }

  tft.init();
  // 0 = retrato 320x480. Se a imagem sair de cabeca para baixo, troque por 2
  // (o mesmo retrato girado 180 graus). 1 e 3 sao as duas paisagens, mas o
  // layout de tela.h foi feito para retrato.
  tft.setRotation(0);

  Serial.printf("Resolucao em uso : %d x %d\n", tft.width(), tft.height());

  // Depende da rotacao: a calibracao vale para a orientacao em que foi feita.
  toqueInicia();
  Serial.println("--------------------------------------------------");

  desenhaPainel();
}

void loop() {
  const uint32_t agora = millis();

  uint16_t tx, ty;
  if (toqueNovo(tx, ty)) trataToque(tx, ty);

  // A rampa do ventilador desce entre leituras, entao o controle roda no
  // ritmo do loop e nao no de 2 s do sensor.
  static uint8_t velAnterior[2] = {255, 255};
  controleTick();
  if (controleVelocidade(0) != velAnterior[0] ||
      controleVelocidade(1) != velAnterior[1]) {
    velAnterior[0] = controleVelocidade(0);
    velAnterior[1] = controleVelocidade(1);
    if (painelVisivel()) pintaControle();
    else if (telaAtiva == TELA_FAN2) fan2Atualiza();  // faixa e "agora" mudaram
  }

  if (sensorOk) {
    if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
      ultimaLeitura = agora;

      float temperatura, umidade;
      if (leSensor(temperatura, umidade)) {
        mostraLeitura(temperatura, umidade);
        controleNovaLeitura(true, temperatura);
        if (painelVisivel()) pintaControle();
      } else {
        // Parou de responder: cabo solto, provavelmente. Volta a procurar
        // em vez de repetir o ultimo valor, que pareceria um sensor travado.
        Serial.println("Sensor parou de responder - voltando a procurar");
        sensorOk = false;
        mostraSemSensor();
        controleNovaLeitura(false, 0);
        if (painelVisivel()) pintaControle();
      }
    }
  } else if (agora - ultimaLeitura >= INTERVALO_RETENTATIVA) {
    ultimaLeitura = agora;

    if (iniciaSensor()) {
      sensorOk = true;
      Serial.printf("Sensor apareceu em 0x%02X\n", enderecoSensor);
    }
  }

  delay(20);
}
