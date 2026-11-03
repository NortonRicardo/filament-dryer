// ---------------------------------------------------------------------------
// Etapa 3 - display + BME280 (temperatura, umidade e pressao)
//
// Le o sensor por I2C e mostra na tela junto com o relogio da etapa anterior.
// Os mesmos valores saem no monitor serial, para conferir sem olhar a tela.
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

static const uint8_t PIN_SDA = 21;
static const uint8_t PIN_SCL = 22;
static const uint8_t LED_PLACA = 2;  // LED azul de bordo, prova de vida

// O modulo responde em 0x76 ou 0x77, conforme o resistor de endereco da placa.
static const uint8_t ENDERECOS[] = {0x76, 0x77};

static const uint32_t INTERVALO_LEITURA = 2000;  // ms entre leituras
static const uint32_t INTERVALO_RETENTATIVA = 5000;  // ms entre buscas do sensor

static const char *ROTULOS[] = {"Temperatura", "Umidade", "Pressao"};
static const uint8_t N_LINHAS = 3;

static bool     sensorOk = false;
static uint8_t  enderecoSensor = 0;
static uint32_t ultimaLeitura = 0;
static uint32_t segundoAnterior = UINT32_MAX;
static int16_t  larguraRelogio = 0;  // para apagar o valor antigo sem piscar
static int16_t  larguraValor = 0;

// ---------------------------------------------------------------------------
// Layout
//
// Calculado a partir da altura real da tela em vez de fixo, para continuar
// legivel se a rotacao mudar (retrato 240x320 ou paisagem 320x240).
// ---------------------------------------------------------------------------

static int16_t yLegenda() { return 26; }
static int16_t yRelogio() { return 70; }
static int16_t ySeparador() { return 108; }
static int16_t yRodape() { return tft.height() - 18; }

// As tres linhas dividem igualmente o que sobra entre o separador e o rodape.
static int16_t yLinha(uint8_t i) {
  const int16_t topo = ySeparador() + 12;
  const int16_t passo = (yRodape() - 16 - topo) / N_LINHAS;
  return topo + passo * i + passo / 2;
}

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
                    Adafruit_BME280::SAMPLING_X1,   // pressao
                    Adafruit_BME280::SAMPLING_X1,   // umidade
                    Adafruit_BME280::FILTER_OFF);
    return true;
  }
  return false;
}

static bool leSensor(float &temperatura, float &umidade, float &pressao) {
  if (!bme.takeForcedMeasurement()) return false;

  temperatura = bme.readTemperature();
  umidade     = bme.readHumidity();
  pressao     = bme.readPressure() / 100.0f;  // Pa -> hPa

  return !isnan(temperatura) && !isnan(umidade) && !isnan(pressao);
}

// ---------------------------------------------------------------------------
// Tela
// ---------------------------------------------------------------------------

static void formataRelogio(uint32_t total, char *buf, size_t n) {
  snprintf(buf, n, "%02u:%02u:%02u",
           (unsigned)((total / 3600) % 100),
           (unsigned)((total / 60) % 60),
           (unsigned)(total % 60));
}

// Desenha o que nao muda: moldura, rotulos, separador e rodape.
static void telaFixa() {
  tft.fillScreen(TFT_BLACK);
  tft.drawRoundRect(4, 4, tft.width() - 8, tft.height() - 8, 6, TFT_DARKGREY);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("ligado ha", tft.width() / 2, yLegenda(), 2);

  tft.drawFastHLine(16, ySeparador(), tft.width() - 32, TFT_DARKGREY);

  tft.setTextDatum(ML_DATUM);
  for (uint8_t i = 0; i < N_LINHAS; i++) {
    tft.drawString(ROTULOS[i], 16, yLinha(i), 2);
  }

  char rodape[64];
  if (sensorOk) {
    snprintf(rodape, sizeof rodape, "%s  %dx%d  BME280 0x%02X",
             DRIVER, tft.width(), tft.height(), enderecoSensor);
  } else {
    snprintf(rodape, sizeof rodape, "%s  %dx%d  sem sensor",
             DRIVER, tft.width(), tft.height());
  }

  // Em retrato a tela e estreita: cai para a fonte menor se nao couber.
  const uint8_t fonte = tft.textWidth(rodape, 2) <= tft.width() - 16 ? 2 : 1;
  tft.setTextDatum(MC_DATUM);
  tft.drawString(rodape, tft.width() / 2, yRodape(), fonte);
}

// Redesenha so o relogio. O padding apaga o valor anterior, evitando piscada.
static void desenhaRelogio(const char *texto) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(larguraRelogio);
  tft.drawString(texto, tft.width() / 2, yRelogio(), 7);
  tft.setTextPadding(0);
}

// Valor alinhado a direita, pelo mesmo motivo: numero que encurta de 100.0
// para 99.9 nao deixa digito orfao atras.
static void desenhaValor(uint8_t i, const char *texto, uint16_t cor) {
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(cor, TFT_BLACK);
  tft.setTextPadding(larguraValor);
  tft.drawString(texto, tft.width() - 16, yLinha(i), 4);
  tft.setTextPadding(0);
}

static void mostraLeitura(float temperatura, float umidade, float pressao) {
  char buf[24];

  snprintf(buf, sizeof buf, "%.1f C", temperatura);
  desenhaValor(0, buf, TFT_WHITE);

  snprintf(buf, sizeof buf, "%.1f %%", umidade);
  desenhaValor(1, buf, TFT_CYAN);

  snprintf(buf, sizeof buf, "%.0f hPa", pressao);
  desenhaValor(2, buf, TFT_GREENYELLOW);

  Serial.printf("temperatura %.2f C | umidade %.2f %% | pressao %.2f hPa\n",
                temperatura, umidade, pressao);
}

static void mostraSemSensor() {
  for (uint8_t i = 0; i < N_LINHAS; i++) {
    desenhaValor(i, "--", TFT_RED);
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(LED_PLACA, OUTPUT);
  delay(400);  // da tempo ao monitor serial de abrir

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  Etapa 3 - display + BME280");
  Serial.println("==================================================");
  Serial.printf("Driver da tela   : %s\n", DRIVER);
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
  Serial.println("--------------------------------------------------");

  larguraRelogio = tft.textWidth("00:00:00", 7);
  larguraValor   = tft.textWidth("1013 hPa", 4);  // a maior das tres linhas

  telaFixa();
  if (!sensorOk) mostraSemSensor();
}

void loop() {
  const uint32_t agora = millis();
  const uint32_t segundos = agora / 1000;

  if (segundos != segundoAnterior) {
    segundoAnterior = segundos;

    char relogio[16];
    formataRelogio(segundos, relogio, sizeof relogio);
    desenhaRelogio(relogio);

    digitalWrite(LED_PLACA, segundos % 2);  // pisca a cada segundo
  }

  if (sensorOk) {
    if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
      ultimaLeitura = agora;

      float temperatura, umidade, pressao;
      if (leSensor(temperatura, umidade, pressao)) {
        mostraLeitura(temperatura, umidade, pressao);
      } else {
        // Parou de responder: cabo solto, provavelmente. Volta a procurar
        // em vez de repetir o ultimo valor, que pareceria um sensor travado.
        Serial.println("Sensor parou de responder - voltando a procurar");
        sensorOk = false;
        telaFixa();
        mostraSemSensor();
      }
    }
  } else if (agora - ultimaLeitura >= INTERVALO_RETENTATIVA) {
    ultimaLeitura = agora;

    if (iniciaSensor()) {
      sensorOk = true;
      Serial.printf("Sensor apareceu em 0x%02X\n", enderecoSensor);
      telaFixa();
    }
  }

  delay(20);
}
