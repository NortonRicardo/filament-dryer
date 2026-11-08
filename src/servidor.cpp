#include "servidor.h"
#include "diario.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

#include "pagina.h"
#include "tela.h"
#include "controle.h"

#ifndef WIFI_SSID
#error "WIFI_SSID nao definido. Crie o arquivo .env: cp .env.exemplo .env"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD nao definido. Crie o arquivo .env: cp .env.exemplo .env"
#endif

static const uint16_t PORTA = 80;
static const char *NOME_MDNS = "estufa";  // http://estufa.local

// 5 minutos de historico. O main le o sensor a cada 2 s, entao 150 amostras
// cobrem a janela pedida com 600 bytes de RAM.
static const uint16_t N_HIST = 150;

static WebServer servidor(PORTA);

static float histTemp[N_HIST];
static float histUmid[N_HIST];
static uint16_t nHist = 0;    // quantas entraram, satura em N_HIST
static uint16_t proxima = 0;  // posicao de escrita no anel

static float ultimaTemp = 0, ultimaUmid = 0;
static bool temLeitura = false;

static bool noAr = false;
static uint32_t inicioConexao = 0;
static bool avisouFalha = false;
static char endereco[40] = "conectando...";

// ---------------------------------------------------------------------------
// Historico
// ---------------------------------------------------------------------------

void servidorRegistra(float temperatura, float umidade) {
  histTemp[proxima] = temperatura;
  histUmid[proxima] = umidade;
  proxima = (proxima + 1) % N_HIST;
  if (nHist < N_HIST) nHist++;

  ultimaTemp = temperatura;
  ultimaUmid = umidade;
  temLeitura = true;
}

void servidorSemSensor() { temLeitura = false; }

// Umidade absoluta da ultima leitura, em g/m3. Mesma formula do main.cpp;
// repetida aqui para o servidor nao depender dele.
float umidadeAbsolutaAtual() {
  if (!temLeitura) return 0.0f;
  const float ps = 6.112f * expf((17.67f * ultimaTemp) / (ultimaTemp + 243.5f));
  return ps * ultimaUmid * 2.1674f / (273.15f + ultimaTemp);
}

// Converte "i-esima amostra mais antiga" na posicao dela dentro do anel.
static uint16_t posicao(uint16_t i) {
  return (proxima + N_HIST - nHist + i) % N_HIST;
}

// ---------------------------------------------------------------------------
// Rotas
// ---------------------------------------------------------------------------

static void rotaPagina() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send_P(200, "text/html; charset=utf-8", PAGINA);
}

// Monta o JSON a mao em vez de usar ArduinoJson: sao tres campos e dois
// vetores, e isso dispensa uma dependencia inteira.
static void rotaDados() {
  String json;
  json.reserve(2048);  // 150 amostras x 2 vetores x ~6 caracteres, com folga

  json += "{\"sensor\":";
  json += temLeitura ? "true" : "false";
  json += ",\"t\":";
  json += String(ultimaTemp, 1);
  json += ",\"u\":";
  json += String(ultimaUmid, 1);
  json += ",\"ua\":";
  json += String(umidadeAbsolutaAtual(), 1);
  json += ",\"n\":";
  json += nHist;
  json += ",\"ligado\":";
  json += millis() / 1000;
  json += ",\"tela\":";
  json += telaEstaLigada() ? "true" : "false";
  json += ",\"brilho\":";
  json += telaBrilho();

  // Temperatura maxima: null quando nunca foi definida, para a pagina mostrar
  // "--" em vez de um zero que pareceria configurado.
  float tmax;
  json += ",\"tmax\":";
  json += painelTempMaxima(tmax) ? String(tmax, 1) : String("null");
  json += ",\"tmaxMin\":";  json += String(TMAX_MIN, 0);

  const Config c = controleConfig();
  json += ",\"estado\":\"";    json += controleEstadoTexto();
  json += "\",\"motivo\":\""; json += controleMotivo();
  json += "\",\"autorizado\":"; json += controleAutorizado() ? "true" : "false";
  json += ",\"aquecendo\":";    json += controleAquecedorLigado() ? "true" : "false";
  json += ",\"pot\":";          json += controlePotencia();
  json += ",\"fan1\":";         json += controleVelocidade(0);
  json += ",\"fan2\":";         json += controleVelocidade(1);
  json += ",\"teto\":";         json += String(TETO_ABSOLUTO, 0);
  json += ",\"cfg\":{\"alvo\":";  json += String(c.alvo, 1);
  json += ",\"kp\":";           json += String(c.kp, 2);
  json += ",\"ki\":";           json += String(c.ki, 3);
  json += ",\"kd\":";           json += String(c.kd, 1);
  json += ",\"f2on\":";         json += c.fan2Ligado ? "true" : "false";
  json += ",\"f2max\":";        json += c.fan2Max;
  json += ",\"f2min\":";        json += c.fan2Min;
  json += ",\"pwmin\":";        json += c.pwmMin;
  json += ",\"pos1\":";         json += c.pos1;
  json += ",\"rmp1\":";         json += c.rampa1;
  json += ",\"subir\":";        json += c.semSubirMin;
  json += "}";
  json += ",\"hist\":{\"t\":[";

  for (uint16_t i = 0; i < nHist; i++) {
    if (i) json += ',';
    json += String(histTemp[posicao(i)], 1);
  }
  json += "],\"u\":[";
  for (uint16_t i = 0; i < nHist; i++) {
    if (i) json += ',';
    json += String(histUmid[posicao(i)], 1);
  }
  json += "]}}";

  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8", json);
}

// Liga ou desliga a imagem do painel. Responde o estado novo, para a pagina
// nao precisar supor que deu certo.
static void rotaTela() {
  const String pedido = servidor.arg("estado");

  if (pedido == "on")       telaDefineLigada(true);
  else if (pedido == "off") telaDefineLigada(false);
  else if (pedido == "alterna") telaDefineLigada(!telaEstaLigada());
  else {
    servidor.send(400, "text/plain; charset=utf-8",
                  "use estado=on, off ou alterna\n");
    return;
  }

  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8",
                telaEstaLigada() ? "{\"tela\":true}" : "{\"tela\":false}");
}

// Brilho da luz de fundo, em %. Abaixo de 5 a tela sobe para 5.
static void rotaBrilho() {
  const String txt = servidor.arg("v");
  const long v = txt.toInt();
  if (txt.length() == 0 || v < 0 || v > 100) {
    servidor.send(400, "text/plain; charset=utf-8", "use v=0 a 100\n");
    return;
  }
  telaDefineBrilho((uint8_t)v);
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8",
                String("{\"brilho\":") + telaBrilho() + "}");
}

// Temperatura maxima. Por enquanto so e guardada e mostrada: ainda nao entra
// na regra do controle. A faixa vai ate o teto absoluto do controle.
static void rotaTmax() {
  const String txt = servidor.arg("v");
  const float v = txt.toFloat();

  if (txt.length() == 0 || v < TMAX_MIN || v > TETO_ABSOLUTO) {
    servidor.send(400, "text/plain; charset=utf-8",
                  String("temperatura maxima fora de ") + String(TMAX_MIN, 0) +
                  " a " + String(TETO_ABSOLUTO, 0) + " C\n");
    return;
  }

  painelDefineTempMaxima(v);
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8",
                String("{\"tmax\":") + String(v, 1) + "}");
}

// A autorizacao e o unico caminho para o aquecedor ligar. Nunca e salva:
// religar a placa volta tudo para bloqueado, por regra do projeto.
static void rotaAutorizar() {
  const String v = servidor.arg("v");
  if (v != "0" && v != "1") {
    servidor.send(400, "text/plain; charset=utf-8", "use v=0 ou v=1\n");
    return;
  }
  controleAutoriza(v == "1");
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8",
                String("{\"autorizado\":") +
                (controleAutorizado() ? "true" : "false") + "}");
}

// Le uma porcentagem e recusa o que nao cabe em 0..100 antes de converter:
// 300 num uint8_t viraria 44 e passaria na validacao do controle.
static bool lePct(const char *nome, uint8_t &destino) {
  if (!servidor.hasArg(nome)) return true;
  const long v = servidor.arg(nome).toInt();
  if (v < 0 || v > 100) return false;
  destino = (uint8_t)v;
  return true;
}

// Campos ausentes mantem o valor atual, entao da para mudar so o alvo sem
// reenviar a configuracao inteira.
static void rotaConfig() {
  Config c = controleConfig();
  if (servidor.hasArg("alvo"))  c.alvo        = servidor.arg("alvo").toFloat();
  if (servidor.hasArg("kp"))    c.kp          = servidor.arg("kp").toFloat();
  if (servidor.hasArg("ki"))    c.ki          = servidor.arg("ki").toFloat();
  if (servidor.hasArg("kd"))    c.kd          = servidor.arg("kd").toFloat();
  if (servidor.hasArg("f2on"))  c.fan2Ligado  = servidor.arg("f2on") == "1";
  if (!lePct("f2max", c.fan2Max) || !lePct("f2min", c.fan2Min) ||
      !lePct("pwmin", c.pwmMin)) {
    servidor.send(400, "text/plain; charset=utf-8",
                  "porcentagem fora de 0 a 100\n");
    return;
  }
  if (servidor.hasArg("pos1"))  c.pos1        = servidor.arg("pos1").toInt();
  if (servidor.hasArg("rmp1"))  c.rampa1      = servidor.arg("rmp1").toInt();
  if (servidor.hasArg("subir")) c.semSubirMin = servidor.arg("subir").toInt();

  const char *porque = "";
  if (!controleDefineConfig(c, &porque)) {
    servidor.send(400, "text/plain; charset=utf-8", String(porque) + "\n");
    return;
  }
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

// O diario: o que sairia no monitor serial, para ver com a caixa fechada.
// Texto puro, do mais antigo ao mais novo; o mais novo fica no fim.
static void rotaLog() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "text/plain; charset=utf-8", diario.texto());
}

static void rotaPerdida() {
  servidor.send(404, "text/plain; charset=utf-8", "nao existe\n");
}

// ---------------------------------------------------------------------------
// Conexao
// ---------------------------------------------------------------------------

void servidorInicia() {
  diario.printf("Wi-Fi            : conectando em \"%s\"\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // sem isso a resposta HTTP fica lenta e irregular
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  inicioConexao = millis();
}

// ---------------------------------------------------------------------------
// Atualizacao pelo Wi-Fi (OTA)
//
// Para gravar firmware com a caixa fechada: pio run -e ota -t upload. A
// senha e a mesma do Wi-Fi, que ja fica no .env fora do repositorio; o script
// scripts/senha_ota.py a entrega ao envio.
//
// A placa tem dois espacos de programa: o novo e gravado no que esta livre e
// so vira o ativo no fim, conferido. Se cair no meio, continua o antigo.
//
// Seguranca: o aquecedor e desligado antes de receber o primeiro byte. Durante
// a gravacao o loop fica parado (o controle nao roda), entao o cartucho nao
// pode ficar ligado esperando. Depois a placa reinicia, e como em todo boot
// tudo comeca desligado.
// ---------------------------------------------------------------------------
static void iniciaAtualizacao() {
  ArduinoOTA.setHostname(NOME_MDNS);
  ArduinoOTA.setPassword(WIFI_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);  // o mDNS ja esta de pe, acima

  ArduinoOTA.onStart([]() {
    controleAutoriza(false);
    diario.println("Atualizacao      : comecou - aquecedor desligado");
    telaAtualizando(0);
  });
  ArduinoOTA.onProgress([](unsigned int feito, unsigned int total) {
    static uint8_t ultimo = 255;
    const uint8_t pct = total ? (uint8_t)((uint64_t)feito * 100 / total) : 0;
    if (pct / 5 != ultimo / 5) {  // redesenha a cada 5%, nao a cada pacote
      ultimo = pct;
      telaAtualizando(pct);
    }
  });
  ArduinoOTA.onEnd([]() {
    diario.println("Atualizacao      : concluida - reiniciando");
  });
  ArduinoOTA.onError([](ota_error_t erro) {
    diario.printf("Atualizacao      : FALHOU (erro %u) - segue o firmware atual\n",
                  (unsigned)erro);
    telaFimAtualizacao();
  });

  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);
  diario.println("Atualizacao      : pelo Wi-Fi liberada (porta 3232, com senha)");
}

static void sobe() {
  servidor.on("/", rotaPagina);
  servidor.on("/dados", rotaDados);
  servidor.on("/tela", rotaTela);
  servidor.on("/tmax", rotaTmax);
  servidor.on("/brilho", rotaBrilho);
  servidor.on("/autorizar", rotaAutorizar);
  servidor.on("/config", rotaConfig);
  servidor.on("/log", rotaLog);
  servidor.onNotFound(rotaPerdida);
  servidor.begin();

  const IPAddress ip = WiFi.localIP();
  snprintf(endereco, sizeof endereco, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2],
           ip[3], PORTA);

  diario.printf("Wi-Fi            : conectado, %d dBm\n", WiFi.RSSI());
  diario.printf("Servidor         : http://%s\n", endereco);

  if (MDNS.begin(NOME_MDNS)) {
    MDNS.addService("http", "tcp", PORTA);
    diario.printf("                   http://%s.local tambem funciona\n",
                  NOME_MDNS);
  }

  iniciaAtualizacao();

  noAr = true;
  avisouFalha = false;
}

void servidorAtende() {
  if (noAr) {
    servidor.handleClient();
    ArduinoOTA.handle();

    if (WiFi.status() != WL_CONNECTED) {
      diario.println("Wi-Fi caiu - reconectando");
      snprintf(endereco, sizeof endereco, "reconectando...");
      servidor.stop();
      ArduinoOTA.end();
      noAr = false;
      inicioConexao = millis();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    sobe();
    return;
  }

  // Nao da para travar aqui: sem rede o aparelho ainda mede e mostra na tela.
  // So avisa uma vez e segue tentando, que o autoReconnect faz sozinho.
  if (!avisouFalha && millis() - inicioConexao > 20000) {
    avisouFalha = true;
    snprintf(endereco, sizeof endereco, "sem Wi-Fi");
    diario.println("Wi-Fi            : nao conectou em 20 s, seguindo sem rede");
  }
}

const char *servidorEndereco() { return endereco; }
