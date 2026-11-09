#include "servidor.h"
#include "diario.h"

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
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

#ifndef SENHA_PADRAO
#error "SENHA_PADRAO nao definida. Acrescente no .env (veja .env.exemplo)"
#endif

// Rede propria, aberta quando a rede de casa some por PORTAL_APOS_MS. Serve
// para escolher a rede de novo pelo celular, sem cabo USB.
static const char *NOME_PORTAL = "ESTUFA-SETUP";
static const uint32_t PORTAL_APOS_MS = 120000;

// WPA2 exige de 8 a 63 caracteres. Mais curta, o ESP32 nao cria a rede - e o
// portal, que e justamente o plano B, nao abriria. Melhor falhar no build.
static_assert(sizeof(SENHA_PADRAO) - 1 >= 8,
              "SENHA_PADRAO precisa de 8 caracteres ou mais (exigencia do WPA2)");

// Texto da listra do rodape, alternando com "SEM REDE EXTERNA".
static const char *AVISO_PORTAL = "REDE ESTUFA-SETUP \xC2\xB7 SENHA " SENHA_PADRAO;

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

// Rede de casa: a gravada pela pagina do portal, ou a do .env como fabrica.
static Preferences nvsRede;
static char rede[33];
static char senhaRede[65];

static DNSServer dns;
static bool portal = false;
static uint32_t tentandoDesde = 0;   // tentativa em curso na rede de casa; 0 = nenhuma
static uint32_t esperandoDesde = 0;  // desde quando esta parado entre tentativas

// Redes encontradas na ultima varredura, da mais forte para a mais fraca.
struct RedeVista {
  char ssid[33];
  int8_t rssi;
  bool aberta;
};
static RedeVista redesVistas[16];
static uint8_t nRedes = 0;
static uint32_t ultimaVarredura = 0;

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
  json += ",\"descartes\":";
  json += leiturasDescartadas();
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

// ---------------------------------------------------------------------------
// Portal: a rede propria e a pagina de escolher a rede de casa
// ---------------------------------------------------------------------------

// Pedido chegou pela rede propria? So por ela a pagina de rede existe - e o
// painel nao: quem conecta na ESTUFA-SETUP nao autoriza aquecedor.
static bool pedidoDoPortal() {
  return portal && servidor.client().localIP() == WiFi.softAPIP();
}

static void irParaPortal() {
  servidor.sendHeader("Location",
                      String("http://") + WiFi.softAPIP().toString() + "/");
  servidor.send(302, "text/plain", "");
}

static std::function<void()> soPainel(void (*rota)()) {
  return [rota]() {
    if (pedidoDoPortal()) { irParaPortal(); return; }
    rota();
  };
}

static void pedeVarredura() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanNetworks(true);  // assincrona: nao segura o loop
  ultimaVarredura = millis();
}

// Recolhe a varredura quando ela termina: sem nome repetido, ordenada pelo
// sinal, no maximo 16.
static void colheVarredura() {
  const int n = WiFi.scanComplete();
  if (n < 0) return;

  nRedes = 0;
  for (int i = 0; i < n; i++) {
    const String nome = WiFi.SSID(i);
    if (nome.length() == 0 || nome.length() > 32) continue;
    const int8_t rssi = (int8_t)WiFi.RSSI(i);

    int j = 0;
    while (j < nRedes && strcmp(redesVistas[j].ssid, nome.c_str()) != 0) j++;
    if (j < nRedes) {
      if (rssi > redesVistas[j].rssi) redesVistas[j].rssi = rssi;
      continue;
    }
    if (nRedes == 16) continue;
    RedeVista &r = redesVistas[nRedes++];
    snprintf(r.ssid, sizeof r.ssid, "%s", nome.c_str());
    r.rssi = rssi;
    r.aberta = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  for (int a = 0; a < nRedes; a++)
    for (int b = a + 1; b < nRedes; b++)
      if (redesVistas[b].rssi > redesVistas[a].rssi) {
        const RedeVista t = redesVistas[a];
        redesVistas[a] = redesVistas[b];
        redesVistas[b] = t;
      }
  WiFi.scanDelete();
}

static void rotaPortal() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send_P(200, "text/html; charset=utf-8", PAGINA_PORTAL);
}

static void jsonTexto(String &j, const char *t) {
  j += '"';
  for (const char *c = t; *c; c++) {
    if (*c == '"' || *c == '\\') j += '\\';
    j += *c;
  }
  j += '"';
}

static void rotaRedes() {
  if (!pedidoDoPortal()) { rotaPerdida(); return; }
  if (millis() - ultimaVarredura > 20000) pedeVarredura();

  String j;
  j.reserve(1024);
  j += "{\"atual\":";
  jsonTexto(j, rede);
  j += ",\"procurando\":";
  j += WiFi.scanComplete() == WIFI_SCAN_RUNNING ? "true" : "false";
  j += ",\"redes\":[";
  for (uint8_t i = 0; i < nRedes; i++) {
    if (i) j += ',';
    j += "{\"ssid\":";
    jsonTexto(j, redesVistas[i].ssid);
    j += ",\"rssi\":";
    j += redesVistas[i].rssi;
    j += ",\"aberta\":";
    j += redesVistas[i].aberta ? "true" : "false";
    j += '}';
  }
  j += "]}";
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8", j);
}

static void rotaSalvaRede() {
  if (!pedidoDoPortal()) { rotaPerdida(); return; }

  const String nome = servidor.arg("ssid");
  const String senha = servidor.arg("senha");
  if (nome.length() < 1 || nome.length() > 32) {
    servidor.send(400, "text/plain; charset=utf-8", "nome da rede invalido\n");
    return;
  }
  if (senha.length() != 0 && (senha.length() < 8 || senha.length() > 63)) {
    servidor.send(400, "text/plain; charset=utf-8",
                  "a senha de uma rede WPA2 tem de 8 a 63 caracteres\n");
    return;
  }

  snprintf(rede, sizeof rede, "%s", nome.c_str());
  snprintf(senhaRede, sizeof senhaRede, "%s", senha.c_str());
  nvsRede.putString("ssid", rede);
  nvsRede.putString("senha", senhaRede);
  diario.printf("Wi-Fi            : rede trocada pelo portal para \"%s\"\n", rede);

  servidor.send(200, "application/json; charset=utf-8", "{\"ok\":true}");

  // Tenta na hora. Se der certo, a rede propria fecha sozinha.
  WiFi.disconnect(false);
  WiFi.begin(rede, senhaRede);
  tentandoDesde = millis();
}

static void abrePortal() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(NOME_PORTAL, SENHA_PADRAO);
  dns.start(53, "*", WiFi.softAPIP());  // todo nome aponta para ca: o celular abre a pagina sozinho

  portal = true;
  tentandoDesde = 0;
  esperandoDesde = millis();
  pedeVarredura();

  diario.printf("Wi-Fi            : sem \"%s\" ha %lu s - rede propria %s aberta em http://%s\n",
                rede, (unsigned long)(PORTAL_APOS_MS / 1000), NOME_PORTAL,
                WiFi.softAPIP().toString().c_str());
}

static void fechaPortal() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.scanDelete();
  portal = false;
  diario.println("Wi-Fi            : rede propria fechada");
}

// Com o portal aberto a rede de casa continua sendo tentada, mas em surtos:
// 15 s tentando, 60 s parado. Tentando sem parar, o radio pula de canal em
// canal procurando o roteador e a rede propria fica instavel para o celular.
// Assim um roteador que so reiniciou volta em ate ~1 min e fecha o portal.
static void tentaRedeDeCasa() {
  if (tentandoDesde) {
    if (millis() - tentandoDesde > 15000) {
      WiFi.disconnect(false);
      tentandoDesde = 0;
      esperandoDesde = millis();
    }
  } else if (millis() - esperandoDesde > 60000) {
    WiFi.begin(rede, senhaRede);
    tentandoDesde = millis();
  }
}

// ---------------------------------------------------------------------------
// Conexao
// ---------------------------------------------------------------------------

static void carregaRede() {
  nvsRede.begin("rede", false);
  const String nome = nvsRede.getString("ssid", "");
  if (nome.length() > 0) {
    snprintf(rede, sizeof rede, "%s", nome.c_str());
    snprintf(senhaRede, sizeof senhaRede, "%s",
             nvsRede.getString("senha", "").c_str());
    diario.printf("Wi-Fi            : rede gravada pelo portal\n");
  } else {
    snprintf(rede, sizeof rede, "%s", WIFI_SSID);
    snprintf(senhaRede, sizeof senhaRede, "%s", WIFI_PASSWORD);
  }
}

static void registraRotas() {
  // "/" serve o painel pela rede de casa e a escolha de rede pela propria.
  servidor.on("/", []() {
    if (pedidoDoPortal()) rotaPortal();
    else rotaPagina();
  });
  servidor.on("/dados", soPainel(rotaDados));
  servidor.on("/tela", soPainel(rotaTela));
  servidor.on("/tmax", soPainel(rotaTmax));
  servidor.on("/brilho", soPainel(rotaBrilho));
  servidor.on("/autorizar", soPainel(rotaAutorizar));
  servidor.on("/config", soPainel(rotaConfig));
  servidor.on("/log", soPainel(rotaLog));
  servidor.on("/redes", rotaRedes);
  servidor.on("/rede", HTTP_POST, rotaSalvaRede);

  // Pela rede propria, qualquer endereco desconhecido volta para a pagina de
  // rede. E isso que faz o celular abrir o portal sozinho ao conectar.
  servidor.onNotFound([]() {
    if (pedidoDoPortal()) irParaPortal();
    else rotaPerdida();
  });
}

void servidorInicia() {
  carregaRede();
  diario.printf("Wi-Fi            : conectando em \"%s\"\n", rede);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // sem isso a resposta HTTP fica lenta e irregular
  WiFi.begin(rede, senhaRede);

  // O servidor sobe uma vez so e fica de pe com ou sem rede: ele atende tanto
  // a rede de casa quanto a propria.
  registraRotas();
  servidor.begin();

  inicioConexao = millis();
}

// ---------------------------------------------------------------------------
// Atualizacao pelo Wi-Fi (OTA)
//
// Para gravar firmware com a caixa fechada: pio run -e ota -t upload. A
// senha e a SENHA_PADRAO do .env - fixa, separada da senha do Wi-Fi, para
// continuar valendo quando a rede for trocada pelo portal. O script
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
  ArduinoOTA.setPassword(SENHA_PADRAO);
  ArduinoOTA.setMdnsEnabled(false);  // o mDNS ja esta de pe, acima

  ArduinoOTA.onStart([]() {
    feedLoopWDT();
    controleAutoriza(false);
    diario.println("Atualizacao      : comecou - aquecedor desligado");
    telaAtualizando(0);
  });
  ArduinoOTA.onProgress([](unsigned int feito, unsigned int total) {
    // A gravacao inteira acontece dentro de ArduinoOTA.handle(), que nao
    // devolve o controle ao loop por dezenas de segundos. Sem alimentar
    // aqui, o watchdog reiniciaria a placa no meio da atualizacao.
    feedLoopWDT();
    static uint8_t ultimo = 255;
    const uint8_t pct = total ? (uint8_t)((uint64_t)feito * 100 / total) : 0;
    if (pct / 5 != ultimo / 5) {  // redesenha a cada 5%, nao a cada pacote
      ultimo = pct;
      telaAtualizando(pct);
    }
  });
  ArduinoOTA.onEnd([]() {
    feedLoopWDT();
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

static void conectou() {
  if (portal) fechaPortal();

  const IPAddress ip = WiFi.localIP();
  snprintf(endereco, sizeof endereco, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2],
           ip[3], PORTA);

  diario.printf("Wi-Fi            : conectado em \"%s\", %d dBm\n", rede, WiFi.RSSI());
  diario.printf("Servidor         : http://%s\n", endereco);

  if (MDNS.begin(NOME_MDNS)) {
    MDNS.addService("http", "tcp", PORTA);
    diario.printf("                   http://%s.local tambem funciona\n",
                  NOME_MDNS);
  }

  iniciaAtualizacao();

  // Hora pela internet. Nao bloqueia: o acerto chega sozinho em segundos e o
  // SNTP segue corrigindo de hora em hora. Fuso de Brasilia, sem horario de
  // verao (extinto em 2019). Nunca usar getLocalTime(): ele espera ate 5 s.
  static bool ntpLigado = false;
  if (!ntpLigado) {
    configTzTime("<-03>3", "a.st1.ntp.br", "pool.ntp.org", "time.google.com");
    ntpLigado = true;
  }

  noAr = true;
  avisouFalha = false;
}

void servidorAtende() {
  servidor.handleClient();

  if (portal) {
    dns.processNextRequest();
    colheVarredura();
  }

  if (noAr) {
    ArduinoOTA.handle();

    if (WiFi.status() != WL_CONNECTED) {
      diario.println("Wi-Fi caiu - reconectando");
      snprintf(endereco, sizeof endereco, "reconectando...");
      ArduinoOTA.end();
      MDNS.end();
      noAr = false;
      inicioConexao = millis();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    conectou();
    return;
  }

  if (portal) {
    tentaRedeDeCasa();
    return;
  }

  // Nao da para travar aqui: sem rede o aparelho ainda mede e mostra na tela.
  if (!avisouFalha && millis() - inicioConexao > 20000) {
    avisouFalha = true;
    snprintf(endereco, sizeof endereco, "sem Wi-Fi");
    diario.println("Wi-Fi            : nao conectou em 20 s, seguindo sem rede");
  }

  if (millis() - inicioConexao > PORTAL_APOS_MS) abrePortal();
}

// Com o portal aberto, alterna a cada 3 s entre o aviso e a rede/senha: as
// duas coisas juntas nao cabem nos 300 px do rodape.
const char *servidorEndereco() {
  if (portal) return (millis() / 3000) % 2 ? "SEM REDE EXTERNA" : AVISO_PORTAL;
  return endereco;
}

bool servidorPortalAtivo() { return portal; }
