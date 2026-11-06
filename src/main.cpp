// ---------------------------------------------------------------------------
// Estufa de filamento - painel THERMO
//
// Le o BME280 e mostra temperatura e umidade no display, no layout de
// cartoes. Serve tambem uma pagina na rede local (ver servidor.h).
//
// Os cartoes de ventilador e hotend ja estao no layout, mas ainda nao ha
// hardware atras deles: mostram "--" em cinza ate existirem.
//
// Ligacao do display (os pinos ficam em platformio.ini, nao aqui):
//   VCC -> 5V         SCL/SCK  -> GPIO18        RES -> GPIO26
//   GND -> GND        SDA/MOSI -> GPIO23        CS  -> GPIO13
//                                               DC  -> GPIO27
//
// Sao sete fios. Este modulo nao tem pino de luz de fundo: ela e alimentada
// por dentro, a partir do VCC, e fica sempre acesa.
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

#include "servidor.h"
#include "tela.h"

TFT_eSPI tft = TFT_eSPI();
Adafruit_BME280 bme;

// Qual driver de tela foi compilado. Se nao acender, troque em platformio.ini.
#if defined(ST7789_DRIVER)
static const char *DRIVER = "ST7789V";
#elif defined(ILI9341_DRIVER)
static const char *DRIVER = "ILI9341";
#else
static const char *DRIVER = "nao definido";
#endif

// O init do ST7789 manda INVON sem condicao, e a biblioteca so o desfaz se
// um destes estiver definido. Sem nenhum, o preto sai branco na tela.
#if defined(TFT_INVERSION_OFF)
static const char *INVERSAO = "desligada (INVOFF)";
#elif defined(TFT_INVERSION_ON)
static const char *INVERSAO = "ligada (INVON)";
#else
static const char *INVERSAO = "nao definida - o preto pode sair branco";
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

// A faixa reservada ao valor dentro do cartao. Apagar so ela, e nao o cartao
// inteiro, e o que evita piscar a cada leitura.
// No cartao grande tudo desce 2 px, inclusive o topo da faixa: com o mesmo
// topo dos outros, ela apagaria a ultima linha de pixels do rotulo a cada
// leitura.
static int16_t yRotulo(const Cartao &c)     { return c.y + (c.grande ? 18 : 17); }
static int16_t faixaTopo(const Cartao &c)   { return c.y + (c.grande ? 26 : 24); }
static int16_t faixaAltura(const Cartao &c) { return c.grande ? 46 : 44; }
static int16_t yValor(const Cartao &c)      { return c.y + (c.grande ? 50 : 48); }
static int16_t yDetalhe(const Cartao &c)    { return c.y + (c.grande ? 80 : 76); }

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

  if (c.grande) tft.setFreeFont(&FreeSansBold24pt7b);
  else          tft.setFreeFont(&FreeSansBold18pt7b);

  const int16_t larguraValor = tft.textWidth(valor);
  const int16_t larguraUnidade = temDado ? desenhaUnidade(0, 0, c, true) : 0;
  const int16_t total = larguraValor + (temDado ? 3 + larguraUnidade : 0);

  int16_t x = c.x + c.l / 2 - total / 2;

  if (c.grande) tft.setFreeFont(&FreeSansBold24pt7b);
  else          tft.setFreeFont(&FreeSansBold18pt7b);
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

// Rodape: so o endereco, centrado. Como ele vira "sem Wi-Fi" ou
// "conectando..." quando a rede cai, o estado da conexao continua visivel
// aqui sem precisar de um indicador separado.
static void desenhaRodape(const char *endereco) {
  tft.fillRect(MARGEM, Y_RODAPE - 7, LARGURA - 2 * MARGEM, 14, FUNDO);

  tft.setTextFont(1);
  textoCentrado(LARGURA / 2, Y_RODAPE, endereco, 1, RODAPE, FUNDO);
}

static void telaFixa() {
  tft.fillScreen(FUNDO);
  for (uint8_t i = 0; i < N_CARTOES; i++) desenhaCartao(i);
}

static bool telaLigada = true;

// Uma casa decimal, menos a partir de 100: assim o texto nunca passa de
// quatro digitos e cabe na largura do cartao sem reduzir a fonte.
static void formataValor(float valor, char *buf, size_t n) {
  if (valor >= 100.0f || valor <= -100.0f) snprintf(buf, n, "%.0f", valor);
  else                                     snprintf(buf, n, "%.1f", valor);
}

// A ultima leitura fica guardada: com a tela desligada nada e desenhado, e
// ao religar os cartoes voltam com o valor atual em vez de "--".
static float ultimaTemp = 0, ultimaUmid = 0;
static bool temLeitura = false;

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
}

static void mostraLeitura(float temperatura, float umidade) {
  ultimaTemp = temperatura;
  ultimaUmid = umidade;
  temLeitura = true;

  if (telaLigada) pintaMedidas();

  Serial.printf("temperatura %.2f C | umidade %.2f %%\n", temperatura, umidade);
}

static void mostraSemSensor() {
  temLeitura = false;
  if (telaLigada) pintaMedidas();
}

// Ventiladores e hotend ainda nao existem no circuito. Ficam em "--" ate
// existirem, em vez de mostrar numero inventado.
static void mostraSemHardware() {
  desenhaValor(CARTAO_FAN1, "--", false);
  desenhaValor(CARTAO_FAN2, "--", false);
  desenhaValor(CARTAO_HOTEND, "--", false);
}

// --- liga/desliga, chamado pelo servidor -----------------------------------
//
// Apagar e pintar de preto, nao mandar DISPOFF ao controlador. DISPOFF nao
// significa "tela apagada": pelo datasheet do ST7789 ele desliga a saida da
// memoria de video e insere uma "pagina em branco" no lugar - e neste painel
// essa pagina e literalmente branca. Pintar de preto da o resultado que se
// espera, ao custo de redesenhar tudo ao religar.
//
// A luz de fundo continua acesa de qualquer forma. Este modulo (GMT024-10,
// header de 7 pinos) nao expoe pino de backlight: ela vem por dentro, do
// proprio VCC. Nao ha o que comandar sem mexer na placa do display.
void telaDefineLigada(bool ligada) {
  if (ligada == telaLigada) return;
  telaLigada = ligada;

  if (ligada) {
    telaFixa();
    desenhaRodape(servidorEndereco());
    mostraSemHardware();
    pintaMedidas();
  } else {
    tft.fillScreen(TFT_BLACK);
  }

  Serial.printf("Tela             : %s\n", ligada ? "ligada" : "apagada");
}

bool telaEstaLigada() { return telaLigada; }

// --- brilho, um tiro no escuro que vale dar ---------------------------------
//
// O ST7789 tem registradores de brilho: WRCTRLD (0x53) liga o controle e
// WRDISBV (0x51) recebe 0 a 255. So que eles comandam os pads de backlight do
// PROPRIO controlador, e na maioria dos modulos baratos esses pads nao vao a
// lugar nenhum - os LEDs sao alimentados direto pelo VCC, por fora dele.
//
// Como este modulo nem pino de backlight expoe, a chance e pequena. Mas o
// teste custa quatro bytes no SPI e nao pode danificar nada: se mudar o
// brilho, ganhamos de graca; se nao mudar, a questao fica encerrada.
static uint8_t brilhoAtual = 255;

void telaDefineBrilho(uint8_t valor) {
  brilhoAtual = valor;

  tft.writecommand(0x53);  // WRCTRLD
  tft.writedata(0x2C);     // BCTRL + DD + BL ligados
  tft.writecommand(0x51);  // WRDISBV
  tft.writedata(valor);

  Serial.printf("Brilho           : %u enviado ao painel\n", valor);
}

uint8_t telaBrilho() { return brilhoAtual; }

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
  Serial.printf("Pinos do sensor  : SDA=%d SCL=%d\n", PIN_SDA, PIN_SCL);

  servidorInicia();  // nao bloqueia: a rede sobe em paralelo ao resto

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
  // 0 = retrato 240x320. Se a imagem sair de cabeca para baixo, troque por 2
  // (o mesmo retrato girado 180 graus). 1 e 3 sao as duas paisagens.
  tft.setRotation(0);

  Serial.printf("Resolucao em uso : %d x %d\n", tft.width(), tft.height());
  Serial.println("--------------------------------------------------");

  telaFixa();
  desenhaRodape(servidorEndereco());
  mostraSemHardware();
  pintaMedidas();  // "--" ate a primeira leitura chegar
}

void loop() {
  const uint32_t agora = millis();

  servidorAtende();

  // O endereco muda sozinho: conectando -> IP, ou IP -> reconectando. A tela
  // acompanha comparando o texto, para nao repintar a linha a cada volta.
  static char enderecoNaTela[40] = "";

  const char *endereco = servidorEndereco();
  if (strcmp(endereco, enderecoNaTela) != 0) {
    snprintf(enderecoNaTela, sizeof enderecoNaTela, "%s", endereco);
    // Com a tela apagada so guarda: ao religar, o rodape e redesenhado com
    // o endereco de entao.
    if (telaEstaLigada()) desenhaRodape(endereco);
  }

  if (sensorOk) {
    if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
      ultimaLeitura = agora;

      float temperatura, umidade;
      if (leSensor(temperatura, umidade)) {
        mostraLeitura(temperatura, umidade);
        servidorRegistra(temperatura, umidade);
      } else {
        // Parou de responder: cabo solto, provavelmente. Volta a procurar
        // em vez de repetir o ultimo valor, que pareceria um sensor travado.
        Serial.println("Sensor parou de responder - voltando a procurar");
        sensorOk = false;
        mostraSemSensor();
        servidorSemSensor();
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
