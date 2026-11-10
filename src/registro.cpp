#include "registro.h"

#include <LittleFS.h>
#include <time.h>

#include "controle.h"
#include "diario.h"

static const char *LOG_ATUAL   = "/log.txt";
static const char *LOG_ANTIGO  = "/log.old";
static const char *DADOS_ATUAL  = "/dados.bin";
static const char *DADOS_ANTIGO = "/dados.old";

// Uma medida gravada. "quando" e a hora real (segundos desde 1970) se o
// relogio ja foi acertado; senao, segundos desde o boot. As duas se separam
// sozinhas: hora real e sempre maior que 1,7 bilhao. MARCA_BOOT separa os
// boots no arquivo.
struct Medida {
  uint32_t quando;
  int16_t  temp100;   // graus x 100
  uint16_t umid100;   // % x 100
};
static_assert(sizeof(Medida) == 8, "a medida gravada tem de ter 8 bytes");

static const uint32_t MARCA_BOOT = 0xFFFFFFFF;
static const uint32_t MEDIDA_A_CADA_MS = 60000;

static bool montado = false;
static size_t limiteLog = 0;     // por arquivo: metade da fatia do diario
static size_t limiteDados = 0;   // por arquivo: metade da fatia das medidas

// Acrescenta ao arquivo atual; se nao couber, o anterior vai embora e o atual
// assume o lugar dele. E assim que o mais antigo sai primeiro.
static void anexa(const char *atual, const char *antigo, size_t limite,
                  const uint8_t *buf, size_t n) {
  if (!montado || n == 0) return;

  File f = LittleFS.open(atual, FILE_APPEND);
  if (!f) return;
  if (f.size() + n > limite) {
    f.close();
    LittleFS.remove(antigo);
    LittleFS.rename(atual, antigo);
    f = LittleFS.open(atual, FILE_APPEND);
    if (!f) return;
  }
  f.write(buf, n);
  f.close();
}

void registroInicia() {
  montado = LittleFS.begin(true);  // true: formata se a particao estiver vazia
  if (!montado) return;

  const size_t total = LittleFS.totalBytes();
  limiteLog   = total * 8 / 100 / 2;
  limiteDados = total * 70 / 100 / 2;

  const Medida boot = {MARCA_BOOT, 0, 0};
  anexa(DADOS_ATUAL, DADOS_ANTIGO, limiteDados, (const uint8_t *)&boot,
        sizeof boot);
}

void registroLinha(const char *linha, size_t n) {
  anexa(LOG_ATUAL, LOG_ANTIGO, limiteLog, (const uint8_t *)linha, n);
}

void registroMedida(float temperatura, float umidade) {
  static uint32_t ultima = 0;
  static bool primeira = true;
  if (!primeira && millis() - ultima < MEDIDA_A_CADA_MS) return;
  primeira = false;
  ultima = millis();

  Medida m;
  m.quando  = horaCerta() ? (uint32_t)time(nullptr) : millis() / 1000;
  m.temp100 = (int16_t)lroundf(temperatura * 100.0f);
  m.umid100 = (uint16_t)lroundf(umidade * 100.0f);
  anexa(DADOS_ATUAL, DADOS_ANTIGO, limiteDados, (const uint8_t *)&m, sizeof m);
}

void registroLimpaHistorico() {
  if (!montado) return;
  LittleFS.remove(DADOS_ANTIGO);
  LittleFS.remove(DADOS_ATUAL);
  diario.println("Historico        : apagado pelo painel");
}

void registroLimpaLog() {
  if (!montado) return;
  LittleFS.remove(LOG_ANTIGO);
  LittleFS.remove(LOG_ATUAL);
  // Primeira linha do log novo: diz por que o resto sumiu.
  diario.println("Log              : apagado pelo painel");
}

size_t registroUsado() { return montado ? LittleFS.usedBytes() : 0; }
size_t registroTotal() { return montado ? LittleFS.totalBytes() : 0; }

// ---------------------------------------------------------------------------
// Envio pela rede
//
// Arquivos grandes saem em pedacos. Entre um pedaco e outro o controle do
// cartucho continua rodando e o watchdog e alimentado: baixar 87 dias de
// historico pelo celular nao pode congelar o PWM do aquecedor.
// ---------------------------------------------------------------------------

static void respira() {
  controleTick();
  feedLoopWDT();
}

static void comecaEnvio(WebServer &s, const char *tipo) {
  s.sendHeader("Cache-Control", "no-store");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, tipo, "");
}

// Junta as linhas num bloco antes de mandar: uma sendContent por linha
// seria um pedaco de rede para cada 80 bytes.
struct Saida {
  WebServer &s;
  String bloco;
  explicit Saida(WebServer &srv) : s(srv) { bloco.reserve(1200); }
  void linha(const char *p, size_t n) {
    if (n == 0) return;  // linha vazia (fim do arquivo) nao entra
    bloco.concat(p, n);
    bloco += '\n';
    if (bloco.length() >= 1024) despeja();
  }
  void despeja() {
    if (bloco.length()) s.sendContent(bloco);
    bloco = "";
    respira();
  }
};

// Envia as linhas de um texto da ultima para a primeira.
static void invertePedaco(Saida &out, const char *p, size_t n) {
  size_t fim = n;
  for (size_t i = n; i > 0; i--) {
    if (p[i - 1] == '\n') {
      out.linha(p + i, fim - i);
      fim = i - 1;
    }
  }
  out.linha(p, fim);
}

// Le o arquivo de tras para frente, em blocos, e manda as linhas na ordem
// inversa: a mais nova primeiro. "resto" guarda o comeco de linha que ficou
// cortado no inicio de um bloco, ate o bloco anterior completar a linha.
static void enviaArquivoInvertido(Saida &out, const char *nome) {
  File f = LittleFS.open(nome, FILE_READ);
  if (!f) return;

  String resto;
  char buf[512];
  size_t pos = f.size();

  while (pos > 0) {
    const size_t n = pos > sizeof buf ? sizeof buf : pos;
    pos -= n;
    f.seek(pos);
    if (f.read((uint8_t *)buf, n) != n) break;

    // Linhas completas deste bloco: tudo depois do primeiro '\n'. O resto do
    // bloco seguinte (ja lido) completa a ultima delas.
    size_t primeira = 0;
    while (primeira < n && buf[primeira] != '\n') primeira++;

    if (primeira == n) {        // nenhuma quebra: o bloco todo e meio de linha
      String junto;
      junto.reserve(n + resto.length());
      junto.concat(buf, n);
      junto += resto;
      resto = junto;
      continue;
    }

    String cauda;
    cauda.reserve(n - primeira + resto.length());
    cauda.concat(buf + primeira + 1, n - primeira - 1);
    cauda += resto;
    invertePedaco(out, cauda.c_str(), cauda.length());
    resto = String();
    resto.concat(buf, primeira);
  }
  out.linha(resto.c_str(), resto.length());
  f.close();
}

// A mais nova primeiro: abre o /log e o que importa esta no topo.
void registroEnviaLog(WebServer &s) {
  comecaEnvio(s, "text/plain; charset=utf-8");
  Saida out(s);
  if (montado) {
    enviaArquivoInvertido(out, LOG_ATUAL);
    enviaArquivoInvertido(out, LOG_ANTIGO);
  } else {  // sem flash: o que estiver na RAM
    const String t = diario.texto();
    invertePedaco(out, t.c_str(), t.length());
  }
  out.despeja();
  s.sendContent("");
}

static float umidadeAbsoluta(float t, float ur) {
  const float ps = 6.112f * expf((17.67f * t) / (t + 243.5f));
  return ps * ur * 2.1674f / (273.15f + t);
}

// Virgula decimal: o Excel em portugues le "24,5" como numero e "24.5" como
// texto.
static void numero(char *dst, size_t n, float v, int casas) {
  snprintf(dst, n, "%.*f", casas, v);
  for (char *p = dst; *p; p++) if (*p == '.') *p = ',';
}

static void enviaMedidas(WebServer &s, const char *nome, uint32_t desde) {
  File f = LittleFS.open(nome, FILE_READ);
  if (!f) return;

  Medida lote[64];
  String saida;
  saida.reserve(64 * 48);

  while (f.available()) {
    const size_t lidos = f.read((uint8_t *)lote, sizeof lote) / sizeof(Medida);
    saida = "";
    for (size_t i = 0; i < lidos; i++) {
      const Medida &m = lote[i];
      char quando[24], t[12], u[12], ua[12];

      if (m.quando == MARCA_BOOT) {
        if (desde == 0) saida += "reinicio;;;\n";
        continue;
      }
      const bool real = m.quando > 1700000000UL;
      if (desde && (!real || m.quando < desde)) continue;

      if (real) {
        const time_t tt = (time_t)m.quando;
        struct tm tm;
        localtime_r(&tt, &tm);
        strftime(quando, sizeof quando, "%d/%m/%Y %H:%M", &tm);
      } else {
        snprintf(quando, sizeof quando, "+%02lu:%02lu (desde o boot)",
                 (unsigned long)(m.quando / 3600),
                 (unsigned long)((m.quando / 60) % 60));
      }
      const float temp = m.temp100 / 100.0f, umid = m.umid100 / 100.0f;
      numero(t, sizeof t, temp, 2);
      numero(u, sizeof u, umid, 2);
      numero(ua, sizeof ua, umidadeAbsoluta(temp, umid), 2);

      saida += quando; saida += ';';
      saida += t;      saida += ';';
      saida += u;      saida += ';';
      saida += ua;     saida += '\n';
    }
    if (saida.length()) s.sendContent(saida);
    respira();
  }
  f.close();
}

void registroEnviaHistorico(WebServer &s, uint32_t horas) {
  if (!montado) {
    s.send(503, "text/plain; charset=utf-8", "memoria de registro indisponivel\n");
    return;
  }

  // Com filtro de horas so entram medidas com hora real: as de "desde o
  // boot" nao tem como ser comparadas com o relogio.
  uint32_t desde = 0;
  if (horas && horaCerta()) desde = (uint32_t)time(nullptr) - horas * 3600UL;

  s.sendHeader("Content-Disposition",
               "attachment; filename=\"estufa-historico.csv\"");
  comecaEnvio(s, "text/csv; charset=utf-8");
  // Marca BOM: sem ela o Excel abre os acentos do cabecalho errado.
  s.sendContent("\xEF\xBB\xBF" "data_hora;temperatura_C;umidade_relativa_pct;umidade_absoluta_g_m3\n");
  enviaMedidas(s, DADOS_ANTIGO, desde);
  enviaMedidas(s, DADOS_ATUAL, desde);
  s.sendContent("");
}
