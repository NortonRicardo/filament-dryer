#include "servidor.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "pagina.h"
#include "tela.h"

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
  json += ",\"n\":";
  json += nHist;
  json += ",\"ligado\":";
  json += millis() / 1000;
  json += ",\"tela\":";
  json += telaEstaLigada() ? "true" : "false";
  json += ",\"brilho\":";
  json += telaBrilho();
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

// Teste do brilho por registrador. Pode nao surtir efeito nenhum - veja o
// comentario em telaDefineBrilho().
static void rotaBrilho() {
  const long valor = servidor.arg("v").toInt();

  if (valor < 0 || valor > 255) {
    servidor.send(400, "text/plain; charset=utf-8", "use v=0 a 255\n");
    return;
  }

  telaDefineBrilho((uint8_t)valor);
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8",
                String("{\"brilho\":") + telaBrilho() + "}");
}

static void rotaPerdida() {
  servidor.send(404, "text/plain; charset=utf-8", "nao existe\n");
}

// ---------------------------------------------------------------------------
// Conexao
// ---------------------------------------------------------------------------

void servidorInicia() {
  Serial.printf("Wi-Fi            : conectando em \"%s\"\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // sem isso a resposta HTTP fica lenta e irregular
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  inicioConexao = millis();
}

static void sobe() {
  servidor.on("/", rotaPagina);
  servidor.on("/dados", rotaDados);
  servidor.on("/tela", rotaTela);
  servidor.on("/brilho", rotaBrilho);
  servidor.onNotFound(rotaPerdida);
  servidor.begin();

  const IPAddress ip = WiFi.localIP();
  snprintf(endereco, sizeof endereco, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2],
           ip[3], PORTA);

  Serial.printf("Wi-Fi            : conectado, %d dBm\n", WiFi.RSSI());
  Serial.printf("Servidor         : http://%s\n", endereco);

  if (MDNS.begin(NOME_MDNS)) {
    MDNS.addService("http", "tcp", PORTA);
    Serial.printf("                   http://%s.local tambem funciona\n",
                  NOME_MDNS);
  }

  noAr = true;
  avisouFalha = false;
}

void servidorAtende() {
  if (noAr) {
    servidor.handleClient();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi caiu - reconectando");
      snprintf(endereco, sizeof endereco, "reconectando...");
      servidor.stop();
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
    Serial.println("Wi-Fi            : nao conectou em 20 s, seguindo sem rede");
  }
}

const char *servidorEndereco() { return endereco; }
