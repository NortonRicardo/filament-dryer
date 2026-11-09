#include "diario.h"

#include <time.h>

// 6 KB: umas 100 linhas. Cabe com folga numa RAM de 320 KB e cobre o boot
// mais um bom tempo de eventos.
static const size_t TAMANHO = 6144;

static char anel[TAMANHO];
static size_t posicao = 0;     // onde entra o proximo caractere
static bool cheio = false;     // ja deu a volta pelo menos uma vez
static bool inicioDeLinha = true;

Diario diario;

static void guarda(char c) {
  anel[posicao++] = c;
  if (posicao == TAMANHO) {
    posicao = 0;
    cheio = true;
  }
}

size_t Diario::write(uint8_t c) {
  Serial.write(c);

  if (inicioDeLinha && c != '\n') {
    // Com a hora acertada pela internet: dia/mes e hora de Brasilia. Antes
    // disso (boot, sem rede): tempo desde o boot, marcado com "+" para nao se
    // confundir com hora real.
    char marca[24];
    if (horaCerta()) {
      const time_t agora = time(nullptr);
      struct tm t;
      localtime_r(&agora, &t);
      strftime(marca, sizeof marca, "[%d/%m %H:%M:%S] ", &t);
    } else {
      const uint32_t s = millis() / 1000;
      snprintf(marca, sizeof marca, "[+%02lu:%02lu:%02lu] ",
               (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60),
               (unsigned long)(s % 60));
    }
    for (const char *p = marca; *p; p++) guarda(*p);
    inicioDeLinha = false;
  }

  guarda((char)c);
  if (c == '\n') inicioDeLinha = true;
  return 1;
}

size_t Diario::write(const uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) write(buf[i]);
  return n;
}

String Diario::texto() const {
  String s;
  s.reserve(TAMANHO + 1);

  if (!cheio) {
    for (size_t i = 0; i < posicao; i++) s += anel[i];
    return s;
  }

  // Depois da volta, o mais antigo comeca em "posicao". A primeira linha
  // pode ter sido cortada ao meio pela sobrescrita: descarta ate o primeiro
  // fim de linha.
  size_t i = posicao;
  size_t lidos = 0;
  while (lidos < TAMANHO && anel[i] != '\n') {
    i = (i + 1) % TAMANHO;
    lidos++;
  }
  i = (i + 1) % TAMANHO;
  lidos++;
  for (; lidos < TAMANHO; lidos++) {
    s += anel[i];
    i = (i + 1) % TAMANHO;
  }
  return s;
}

// Qualquer instante depois de nov/2023 conta como acertado: sem NTP o relogio
// do ESP32 comeca em 1970.
bool horaCerta() { return time(nullptr) > 1700000000; }
