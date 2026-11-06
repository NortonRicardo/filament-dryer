// ---------------------------------------------------------------------------
// Etapa 2 - teste do display 4" SPI 480x320 (ST7796S, header de 14 pinos)
//
// Mostra "Hello World" e um relogio que anda de segundo em segundo, tanto na
// tela quanto no monitor serial. Serve para provar tres coisas de uma vez:
// a ligacao esta certa, o driver escolhido e o correto, e o ESP32 esta vivo.
//
// Ligacao (configurada em platformio.ini, nao aqui):
//   VCC       -> 5V          SCK       -> GPIO18      RESET -> GPIO26
//   GND       -> GND         SDI(MOSI) -> GPIO23      CS    -> GPIO13
//   LED       -> 3V3                                  DC/RS -> GPIO27
//
// SDO(MISO) e os cinco T_ do touch ficam sem ligar neste teste.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// Qual driver foi compilado. Se a tela nao acender, troque em platformio.ini.
#if defined(ST7796_DRIVER)
static const char *DRIVER = "ST7796S";
#elif defined(ILI9488_DRIVER)
static const char *DRIVER = "ILI9488";
#else
static const char *DRIVER = "nao definido";
#endif

static const uint8_t LED_PLACA = 2;  // LED azul de bordo, prova de vida extra

static uint32_t segundoAnterior = UINT32_MAX;
static int16_t  larguraRelogio = 0;   // para apagar o valor antigo sem piscar

// Converte segundos desde o boot em HH:MM:SS.
static void formataRelogio(uint32_t total, char *buf, size_t n) {
  snprintf(buf, n, "%02u:%02u:%02u",
           (unsigned)((total / 3600) % 100),
           (unsigned)((total / 60) % 60),
           (unsigned)(total % 60));
}

// Alturas calculadas a partir da altura real da tela, nao fixas, para o
// layout continuar centrado se a rotacao mudar (retrato 320x480 ou
// paisagem 480x320).
static int16_t yTitulo()  { return tft.height() * 22 / 100; }
static int16_t yLegenda() { return tft.height() * 40 / 100; }
static int16_t yRelogio() { return tft.height() * 52 / 100; }
static int16_t yRodape()  { return tft.height() - 28; }

// Desenha o que nao muda: moldura, titulo e rodape.
static void telaFixa() {
  tft.fillScreen(TFT_BLACK);
  tft.drawRoundRect(4, 4, tft.width() - 8, tft.height() - 8, 6, TFT_DARKGREY);

  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Hello World", tft.width() / 2, yTitulo(), 4);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("ligado ha", tft.width() / 2, yLegenda(), 2);

  char rodape[64];
  snprintf(rodape, sizeof rodape, "%s  %dx%d  SPI %d MHz",
           DRIVER, tft.width(), tft.height(), SPI_FREQUENCY / 1000000);

  // Em retrato a tela e estreita: cai para a fonte menor se nao couber.
  const uint8_t fonte = tft.textWidth(rodape, 2) <= tft.width() - 16 ? 2 : 1;
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

void setup() {
  Serial.begin(115200);
  pinMode(LED_PLACA, OUTPUT);
  delay(400);  // da tempo ao monitor serial de abrir

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  Etapa 2 - teste do display 4\" SPI 480x320");
  Serial.println("==================================================");
  Serial.printf("Driver compilado : %s\n", DRIVER);
  Serial.printf("Barramento SPI   : %d MHz\n", SPI_FREQUENCY / 1000000);
  Serial.printf("Pinos            : SCK=%d MOSI=%d CS=%d DC=%d RST=%d\n",
                TFT_SCLK, TFT_MOSI, TFT_CS, TFT_DC, TFT_RST);

  tft.init();
  // 0 = retrato 320x480. Se a imagem sair de cabeca para baixo, troque por 2
  // (o mesmo retrato girado 180 graus). 1 e 3 sao as duas paisagens.
  tft.setRotation(0);

  Serial.printf("Resolucao em uso : %d x %d\n", tft.width(), tft.height());
  Serial.println("Hello World");
  Serial.println("--------------------------------------------------");
  Serial.println("Se a tela ficou branca ou embaralhada, troque o");
  Serial.println("driver em platformio.ini (ST7796 <-> ILI9488).");
  Serial.println("--------------------------------------------------");

  larguraRelogio = tft.textWidth("00:00:00", 7);
  telaFixa();
}

void loop() {
  const uint32_t segundos = millis() / 1000;

  if (segundos != segundoAnterior) {
    segundoAnterior = segundos;

    char relogio[16];
    formataRelogio(segundos, relogio, sizeof relogio);

    desenhaRelogio(relogio);
    digitalWrite(LED_PLACA, segundos % 2);  // pisca a cada segundo

    Serial.printf("[%s] rodando · heap livre %u bytes\n",
                  relogio, (unsigned)ESP.getFreeHeap());
  }

  delay(20);
}
