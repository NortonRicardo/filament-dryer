// ---------------------------------------------------------------------------
// Etapa 3 - display + BME280 (temperatura e umidade)
//
// Cada grandeza aparece num anel: a barra enche da esquerda para a direita,
// passando por cima, e o valor fica em negrito no centro. Os mesmos valores
// saem no monitor serial.
//
// Ligacao do display (os pinos ficam em platformio.ini, nao aqui):
//   VCC -> 5V         SCL/SCK  -> GPIO18        RES -> GPIO26
//   GND -> GND        SDA/MOSI -> GPIO23        CS  -> GPIO13
//   BLK -> 3V3                                  DC  -> GPIO27
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

// Cores escolhidas para nao apagarem no fundo preto.
static const uint16_t COR_TEMP  = TFT_ORANGE;
static const uint16_t COR_UMID  = TFT_CYAN;
static const uint16_t COR_TEXTO = TFT_WHITE;
static const uint16_t COR_TRILHA = 0x4208;  // cinza, o anel vazio por tras

// ---------------------------------------------------------------------------
// Geometria dos aneis
//
// Na TFT_eSPI o angulo 0 fica as 6 horas e cresce no sentido horario, ou seja
// passando pelas 9. Comecando em 45 e varrendo 270 graus, a barra sai da base
// esquerda, sobe pela esquerda, cruza o topo e desce pela direita -- de um
// lado ao outro, com a abertura embaixo. Nessa abertura cabe o rotulo.
// ---------------------------------------------------------------------------

static const int16_t ANEL_CX = 120;
static const int16_t ANEL_CY[2] = {88, 228};
// Raio interno 48 e nao 46: o retangulo que o padding do valor apaga mede
// 78x42 px (medido nos glifos da FreeSansBold18pt), e o canto dele fica a
// 44,3 px do centro. Com 46 sobrava 1,7 px e o texto encostava no anel.
static const int16_t ANEL_RE = 64;  // raio externo
static const int16_t ANEL_RI = 48;  // raio interno
static const int16_t DESLOC_ROTULO = 52;

static const uint16_t ANG_INICIO = 45;
static const uint16_t ANG_VARRIDO = 270;

struct Serie {
  const char *simbolo;
  bool grau;  // desenha o anelzinho do grau antes do simbolo
  float minimo, maximo;
  uint16_t cor;
};

// Escalas fixas, nao automaticas. Escala automatica faria qualquer oscilacao
// de decimo de grau encher meio anel e mudaria o significado da mesma barra
// a cada leitura - impossivel comparar uma hora com a outra.
static const Serie SERIES[2] = {
  {"C", true,  0.0f,  60.0f, COR_TEMP},
  {"%", false, 0.0f, 100.0f, COR_UMID},
};

static bool     sensorOk = false;
static uint8_t  enderecoSensor = 0;
static uint32_t ultimaLeitura = 0;
static int16_t  larguraValor = 0;
static uint16_t varrido[2] = {0, 0};  // graus ja pintados em cada anel

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
// Aneis
// ---------------------------------------------------------------------------

// Pinta somente o trecho que mudou: o que cresceu sai na cor da serie, o que
// recuou volta para a cor da trilha. O anel nunca e repintado inteiro, entao
// nao ha o piscar de apagar e redesenhar.
//
// drawArc (e nao drawSmoothArc) de proposito: a propria biblioteca diz que as
// pontas sem anti-aliasing existem para permitir mudanca dinamica de
// comprimento com juncao limpa entre segmentos. Com drawSmoothArc as pontas
// suavizadas deixariam costura visivel a cada emenda.
static void desenhaAnel(uint8_t i, float valor) {
  const Serie &s = SERIES[i];

  float fracao = (valor - s.minimo) / (s.maximo - s.minimo);
  fracao = constrain(fracao, 0.0f, 1.0f);
  const uint16_t alvo = (uint16_t)(ANG_VARRIDO * fracao + 0.5f);

  if (alvo > varrido[i]) {
    tft.drawArc(ANEL_CX, ANEL_CY[i], ANEL_RE, ANEL_RI,
                ANG_INICIO + varrido[i], ANG_INICIO + alvo, s.cor, TFT_BLACK);
  } else if (alvo < varrido[i]) {
    tft.drawArc(ANEL_CX, ANEL_CY[i], ANEL_RE, ANEL_RI,
                ANG_INICIO + alvo, ANG_INICIO + varrido[i], COR_TRILHA,
                TFT_BLACK);
  }

  varrido[i] = alvo;
}

static void esvaziaAnel(uint8_t i) {
  if (varrido[i] == 0) return;
  tft.drawArc(ANEL_CX, ANEL_CY[i], ANEL_RE, ANEL_RI,
              ANG_INICIO, ANG_INICIO + varrido[i], COR_TRILHA, TFT_BLACK);
  varrido[i] = 0;
}

// ---------------------------------------------------------------------------
// Tela
// ---------------------------------------------------------------------------

// Simbolo da grandeza, na abertura de baixo do anel.
//
// O grau nao existe nas fontes da TFT_eSPI: elas cobrem 0x20 a 0x7E e o
// caractere ficaria fora. Desenhado como dois circulos concentricos, que e
// exatamente o que o simbolo e - e assim ele acompanha a cor da serie.
static void desenhaSimbolo(uint8_t i) {
  const Serie &s = SERIES[i];
  const int16_t y = ANEL_CY[i] + DESLOC_ROTULO;

  tft.setTextColor(s.cor, TFT_BLACK);

  if (!s.grau) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString(s.simbolo, ANEL_CX, y, 4);
    return;
  }

  const int16_t largura = tft.textWidth(s.simbolo, 4);
  const int16_t x0 = ANEL_CX - (largura + 9) / 2;

  tft.drawCircle(x0 + 3, y - 7, 3, s.cor);
  tft.drawCircle(x0 + 3, y - 7, 2, s.cor);

  tft.setTextDatum(ML_DATUM);
  tft.drawString(s.simbolo, x0 + 9, y, 4);
}

// Desenha o que nao muda: a trilha dos dois aneis e os simbolos.
static void telaFixa() {
  tft.fillScreen(TFT_BLACK);

  for (uint8_t i = 0; i < 2; i++) {
    tft.drawArc(ANEL_CX, ANEL_CY[i], ANEL_RE, ANEL_RI,
                ANG_INICIO, ANG_INICIO + ANG_VARRIDO, COR_TRILHA, TFT_BLACK);
    varrido[i] = 0;
    desenhaSimbolo(i);
  }
}

// Uma casa decimal, menos a partir de 100: assim o texto nunca passa de
// quatro digitos e cabe com folga dentro do anel.
static void formataValor(float valor, char *buf, size_t n) {
  if (valor >= 100.0f || valor <= -100.0f) snprintf(buf, n, "%.0f", valor);
  else                                     snprintf(buf, n, "%.1f", valor);
}

// Numero em negrito no centro do anel. O padding apaga a area inteira: um
// valor que encurta de 100 para 99.9 nao deixa digito orfao atras.
static void desenhaValor(uint8_t i, const char *texto, uint16_t cor) {
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cor, TFT_BLACK);
  tft.setTextPadding(larguraValor);
  tft.drawString(texto, ANEL_CX, ANEL_CY[i]);
  tft.setTextPadding(0);
}

static void mostraLeitura(float temperatura, float umidade) {
  const float valores[2] = {temperatura, umidade};
  char buf[16];

  for (uint8_t i = 0; i < 2; i++) {
    formataValor(valores[i], buf, sizeof buf);
    desenhaValor(i, buf, COR_TEXTO);
    desenhaAnel(i, valores[i]);
  }

  Serial.printf("temperatura %.2f C | umidade %.2f %%\n", temperatura, umidade);
}

static void mostraSemSensor() {
  for (uint8_t i = 0; i < 2; i++) {
    desenhaValor(i, "--", TFT_RED);
    esvaziaAnel(i);
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(400);  // da tempo ao monitor serial de abrir

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  Etapa 3 - display + BME280");
  Serial.println("==================================================");
  Serial.printf("Driver da tela   : %s\n", DRIVER);
  Serial.printf("Inversao de cor  : %s\n", INVERSAO);
  Serial.printf("Barramento SPI   : %d MHz\n", SPI_FREQUENCY / 1000000);
  Serial.printf("Pinos da tela    : SCK=%d MOSI=%d CS=%d DC=%d RST=%d\n",
                TFT_SCLK, TFT_MOSI, TFT_CS, TFT_DC, TFT_RST);
  Serial.printf("Pinos do sensor  : SDA=%d SCL=%d\n", PIN_SDA, PIN_SCL);

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

  tft.setFreeFont(&FreeSansBold18pt7b);
  larguraValor = tft.textWidth("-99.9");  // o texto mais largo possivel
  Serial.printf("Aneis            : raio %d-%d, %d graus; valor com %d px "
                "dentro de %d px livres\n",
                ANEL_RI, ANEL_RE, ANG_VARRIDO, larguraValor, ANEL_RI * 2);
  Serial.println("--------------------------------------------------");

  telaFixa();
  if (!sensorOk) mostraSemSensor();
}

void loop() {
  const uint32_t agora = millis();

  if (sensorOk) {
    if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
      ultimaLeitura = agora;

      float temperatura, umidade;
      if (leSensor(temperatura, umidade)) {
        mostraLeitura(temperatura, umidade);
      } else {
        // Parou de responder: cabo solto, provavelmente. Volta a procurar
        // em vez de repetir o ultimo valor, que pareceria um sensor travado.
        Serial.println("Sensor parou de responder - voltando a procurar");
        sensorOk = false;
        mostraSemSensor();
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
