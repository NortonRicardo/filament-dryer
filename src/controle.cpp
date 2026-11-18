#include "controle.h"
#include "aviso.h"
#include "diario.h"

#include <Arduino.h>
#include <Preferences.h>

// --- hardware ---------------------------------------------------------------
// Gate do IRLB8721 do cartucho (100 R em serie, 10 k do gate ao GND). Nao e
// o GPIO2: aquele e pino de strapping, e com o MOSFET pendurado podia
// atrapalhar o boot. O 25 e um pino comum, em nivel baixo no boot.
static const uint8_t PIN_AQUECEDOR = 25;
// Ventilador preso no aquecedor PTC (segue o aquecedor) e os dois de
// circulacao. Os dois de circulacao saem do mesmo canal de PWM, ligado nos
// dois pinos: giram sempre juntos, na mesma velocidade. Cada pino tem o seu
// transistor, borne e fio de comando.
static const uint8_t PIN_FAN_AQUEC = 16;  // ventilador do aquecedor
static const uint8_t PIN_CIRC_A = 32;     // circulacao
static const uint8_t PIN_CIRC_B = 33;     // circulacao

// 25 kHz fica acima do que o ouvido capta. Abaixo disso o ventilador chia,
// porque a bobina vibra no ritmo do chaveamento.
static const uint32_t PWM_FREQ = 25000;
static const uint8_t  PWM_BITS = 8;
static const uint8_t  CANAL_FAN1 = 0;
static const uint8_t  CANAL_FAN2 = 1;

// --- limites de configuracao ------------------------------------------------
// O alvo para em 55 para sobrar margem ate o teto de 60.
static const float ALVO_MIN = 20.0f, ALVO_MAX = 55.0f;
static const float KP_MAX = 100.0f, KI_MAX = 5.0f, KD_MAX = 1000.0f;

// --- PWM do cartucho ----------------------------------------------------------
// Janela de 2 s: com 60% de potencia, 1,2 s ligado e 0,8 s desligado. O
// cartucho e o bloco tem inercia de sobra, entao o resultado termico e o mesmo
// de um PWM rapido, com muito menos chaveamento de ~15 A. Pulsos curtos demais
// nao valem o tranco: abaixo de 2% vira 0, acima de 98% vira 100.
static const uint32_t JANELA_MS = 2000;

// Potencia a partir da qual o PID conta como "no maximo" para a guarda de
// "aquecendo sem subir".
static const float POT_SATURADA = 95.0f;

// Guarda de "aquecendo sem a temperatura subir", em minutos. Nao aceita 0:
// com o cartucho de verdade ela e a protecao contra cartucho solto, sensor
// fora do lugar ou porta aberta, e nao pode ser desligada pela pagina.
static const uint16_t SUBIR_MIN = 5, SUBIR_MAX = 60;

// Chave nova na memoria. A antiga ("subir") foi gravada com 0 - guarda
// desligada, da epoca em que o aquecedor era o LED - toda vez que qualquer
// configuracao era salva. Lendo dela, a guarda continuaria desligada.
static const char *NVS_SUBIR = "subirmin";

static Preferences nvs;

static Config cfg = {
  55.0f,  // alvo: PETG, o filamento usado aqui. Recomendado 55 a 70 C;
          // 55 e o maximo que a caixa permite (ALVO_MAX)
  // Ganhos para o PTC de 180 W. Medido em 10/11/2026: a geladeira perde
  // ~5 W por grau acima do ambiente, entao manter 55 C com ~27 C fora pede
  // ~140 W, uns 78% da potencia. Com 4 %/C, 10 C abaixo do alvo ja da 40%;
  // a integral cobre o que falta para segurar a temperatura. Ganhos maiores
  // (os 10/0,03 da epoca do cartucho de 50 W) passariam do alvo com o
  // triplo da potencia - e o teto esta so 5 C acima. Afinar olhando o
  // grafico: passou do alvo e oscila, baixe o Ki; demora, suba o Kp.
  4.0f,   // kp
  0.01f,  // ki
  0.0f,   // kd
  false,  // circulacao: desligada no boot, so o painel web liga
  100,    // circulacao: 100% ao ligar
  30,     // piso de PWM: abaixo disso a helice nao parte
  180,    // 3 min a 100% depois de desligar
  120,    // mais 2 min descendo ate parar
  10,     // guarda de "nao esquenta": 10 min no maximo sem subir 1 C corta.
          // Com 180 W a caixa sobe varios graus nesse tempo; se nao subir,
          // e aquecedor solto, sensor fora do lugar ou porta aberta.
};

// Marca na memoria de que os ganhos e a protecao ja foram trocados pelos do
// PTC de 180 W. Sem ela, o primeiro boot com este firmware descarta os
// valores salvos na epoca do cartucho de 50 W, que passariam do alvo.
static const char *NVS_PTC = "ptc180";

static EstadoAquecedor estado = EST_BLOQUEADO;
static const char *motivo = "";
static bool autorizado = false;
static bool aquecedorLigado = false;  // PID pedindo mais que 0%
static float potencia = 0;            // 0 a 100 %, saida do PID
static uint8_t velocidade[2] = {0, 0};

static uint32_t desdeDesligou = 0;   // millis em que a potencia foi a 0
static bool     jaAqueceu = false;   // houve ao menos um ciclo nesta sessao

// Estado do PID. "primeira" zera tudo na primeira leitura depois de ligar,
// para nao herdar integral nem derivada de outra sessao.
static float    integral = 0;
static float    tempAnterior = 0;
static uint32_t leituraAnterior = 0;
static bool     primeira = true;

// Guarda de "nao esquenta": desde quando o PID esta no maximo e com que
// temperatura comecou.
static bool     saturado = false;
static uint32_t desdeSaturado = 0;
static float    tempAoSaturar = 0;

static float    ultimaTemp = NAN;   // so para o registro da protecao

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

// Zero e zero de verdade. Entre 1 e o piso, sobe para o piso: ventilador sem
// escovas nao gira abaixo de uma tensao minima - fica zumbindo parado, e
// parado ele esquenta. Nao existe "10% de velocidade".
static void aplica(uint8_t i, uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct > 0 && pct < cfg.pwmMin) pct = cfg.pwmMin;

  velocidade[i] = pct;
  ledcWrite(i == 0 ? CANAL_FAN1 : CANAL_FAN2, (uint32_t)pct * 255 / 100);
}

// Define a potencia pedida ao cartucho. O pino em si e chaveado em
// controleTick(), pela janela de PWM. Aqui so se registra a passagem por
// zero, que e o que os ventiladores acompanham.
static void definePotencia(float pct) {
  if (pct < 2.0f) pct = 0;
  if (pct > 98.0f) pct = 100.0f;
  potencia = pct;

  const bool liga = pct > 0;
  if (liga == aquecedorLigado) return;

  aquecedorLigado = liga;
  if (liga) {
    jaAqueceu = true;
  } else {
    desdeDesligou = millis();
    digitalWrite(PIN_AQUECEDOR, LOW);  // nao espera o fim da janela
  }
  diario.printf("Aquecedor        : %s\n", liga ? "aquecendo" : "parado (0%)");
}

static void zeraPid() {
  integral = 0;
  primeira = true;
  saturado = false;
}

static void falha(const char *porque) {
  const float potenciaAntes = potencia;
  definePotencia(0);
  zeraPid();
  autorizado = false;       // queda de protecao derruba a autorizacao junto
  estado = EST_FALHA;
  motivo = porque;
  diario.printf("PROTECAO         : %s (ultima leitura %.1f C, potencia era %.0f%%)\n",
                porque, ultimaTemp, potenciaAntes);

  // Aviso urgente no celular (no Android fura o modo silencioso; no iPhone
  // nao - isso exige autorizacao da Apple que o ntfy nao tem). A estufa pode
  // estar secando sozinha de madrugada.
  char texto[160];
  snprintf(texto, sizeof texto,
           "%s. Ultima leitura %.1f C, potencia %.0f%%. Aquecedor desligado - "
           "autorize de novo pelo painel.",
           porque, ultimaTemp, potenciaAntes);
  avisa("Estufa: PROTECAO", texto, 5);
}

// ---------------------------------------------------------------------------
// Ventiladores
//
// Ventilador do aquecedor: 100% enquanto o PID pede potencia, segue 100% por
// pos1 depois que ela vai a zero, e so entao desce em rampa ate parar. O bloco continua quente
// quando o aquecedor corta - cortar o ventilador junto deixaria todo esse
// calor parado em cima do metal.
//
// Ventilador de circulacao: comeca desligado a cada boot. Depois de ligado no
// painel, gira direto, independente do aquecedor: liga a 100% para o ar
// circular rapido, e ligar o aquecedor tambem a poe a 100% (e liga, se
// estiver desligada). Da para baixar a velocidade no painel depois.
// ---------------------------------------------------------------------------
static uint8_t velocidadeCirculacao() {
  return cfg.fan2Ligado ? cfg.fan2Vel : 0;
}

static void atualizaVentiladores() {
  aplica(1, velocidadeCirculacao());

  if (estado == EST_BLOQUEADO || estado == EST_FALHA) {
    aplica(0, 0);
    return;
  }

  if (aquecedorLigado) {
    aplica(0, 100);
    return;
  }

  if (!jaAqueceu) {          // autorizado, mas ainda nao aqueceu nada
    aplica(0, 0);
    return;
  }

  const uint32_t decorrido = (millis() - desdeDesligou) / 1000;

  if (decorrido < cfg.pos1) {
    aplica(0, 100);
  } else if (cfg.rampa1 > 0 && decorrido < (uint32_t)cfg.pos1 + cfg.rampa1) {
    const uint32_t naRampa = decorrido - cfg.pos1;
    aplica(0, (uint8_t)(100 - (naRampa * 100 / cfg.rampa1)));
  } else {
    aplica(0, 0);
  }
}

// ---------------------------------------------------------------------------

void controleInicia() {
  pinMode(PIN_AQUECEDOR, OUTPUT);
  digitalWrite(PIN_AQUECEDOR, LOW);

  ledcSetup(CANAL_FAN1, PWM_FREQ, PWM_BITS);
  ledcSetup(CANAL_FAN2, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_FAN_AQUEC, CANAL_FAN1);
  ledcAttachPin(PIN_CIRC_A, CANAL_FAN2);
  ledcAttachPin(PIN_CIRC_B, CANAL_FAN2);
  aplica(0, 0);

  nvs.begin("estufa", false);
  // isKey antes dos floats: getFloat de chave ausente (primeiro boot)
  // imprime um erro no console. Os inteiros nao tem esse problema.
  if (nvs.isKey("alvo")) cfg.alvo      = nvs.getFloat("alvo", cfg.alvo);
  if (nvs.isKey("kp")) cfg.kp = nvs.getFloat("kp", cfg.kp);
  if (nvs.isKey("ki")) cfg.ki = nvs.getFloat("ki", cfg.ki);
  if (nvs.isKey("kd")) cfg.kd = nvs.getFloat("kd", cfg.kd);
  cfg.pwmMin      = nvs.getUChar("pwmin", cfg.pwmMin);
  cfg.pos1        = nvs.getUShort("pos1", cfg.pos1);
  cfg.rampa1      = nvs.getUShort("rmp1", cfg.rampa1);
  cfg.semSubirMin = nvs.getUShort(NVS_SUBIR, cfg.semSubirMin);
  if (cfg.semSubirMin < SUBIR_MIN || cfg.semSubirMin > SUBIR_MAX) cfg.semSubirMin = 10;

  if (!nvs.isKey(NVS_PTC)) {   // primeiro boot com o PTC de 180 W
    cfg.kp = 4.0f; cfg.ki = 0.01f; cfg.kd = 0.0f; cfg.semSubirMin = 10;
    nvs.putFloat("kp", cfg.kp);
    nvs.putFloat("ki", cfg.ki);
    nvs.putFloat("kd", cfg.kd);
    nvs.putUShort(NVS_SUBIR, cfg.semSubirMin);
    nvs.putBool(NVS_PTC, true);
    diario.println("PID              : ganhos trocados pelos do PTC de 180 W");
  }

  // Nao se le da memoria nem a autorizacao nem o liga/desliga do FAN 02:
  // os dois nascem falsos toda vez, por regra. Tudo comeca desligado.
  autorizado = false;
  cfg.fan2Ligado = false;
  estado = EST_BLOQUEADO;

  diario.printf("Aquecedor        : DESLIGADO no boot, por regra\n");
  diario.printf("Aquecedor PTC    : GPIO%u, PWM em janela de %lu ms, alvo %.1f C, teto %.0f C\n",
                PIN_AQUECEDOR, (unsigned long)JANELA_MS, cfg.alvo, TETO_ABSOLUTO);
  diario.printf("PID              : kp %.2f  ki %.3f  kd %.1f\n", cfg.kp, cfg.ki, cfg.kd);
  diario.printf("Protecao         : corta se %u min no maximo sem subir 1 C\n",
                cfg.semSubirMin);
  diario.printf("Ventiladores     : aquecedor P%d, circulacao P%d+P%d, PWM %lu Hz, piso %u%%\n",
                PIN_FAN_AQUEC, PIN_CIRC_A, PIN_CIRC_B, (unsigned long)PWM_FREQ, cfg.pwmMin);
  diario.printf("Circulacao       : DESLIGADA no boot, liga a 100%%\n");

  atualizaVentiladores();  // tudo em 0% ate o painel web ligar
}

void controleNovaLeitura(bool sensorOk, float temperatura) {
  if (!autorizado) {
    if (estado != EST_FALHA) estado = EST_BLOQUEADO;
    definePotencia(0);
    zeraPid();
    atualizaVentiladores();
    return;
  }

  if (sensorOk) ultimaTemp = temperatura;

  if (!sensorOk) {
    falha("sensor sem leitura valida");
    atualizaVentiladores();
    return;
  }

  if (temperatura >= TETO_ABSOLUTO) {
    falha("temperatura acima do teto de 60 C");
    atualizaVentiladores();
    return;
  }

  // --- PID -----------------------------------------------------------------
  // Roda a cada leitura do sensor (2 s). dt medido, com teto de 10 s para uma
  // pausa longa (sensor sumiu e voltou) nao virar um degrau na integral.
  const uint32_t agora = millis();
  if (primeira) {
    tempAnterior = temperatura;
    leituraAnterior = agora;
    primeira = false;
  }
  float dt = (agora - leituraAnterior) / 1000.0f;
  if (dt <= 0) dt = 0.001f;
  if (dt > 10.0f) dt = 10.0f;

  const float erro = cfg.alvo - temperatura;
  const float p = cfg.kp * erro;
  // Derivada na medida, nao no erro: mudar o alvo pela pagina nao gera um
  // pico de potencia. Sinal negativo: temperatura subindo freia.
  const float d = -cfg.kd * (temperatura - tempAnterior) / dt;

  // Integral com anti-windup: so acumula se isso nao empurrar a saida para
  // alem do limite no mesmo sentido do erro. Sem isso, durante o aquecimento
  // inicial (100% por muito tempo) ela incharia e a temperatura passaria do
  // alvo bem antes de desinchar - e o teto esta so 5 C acima.
  const float candidata = integral + cfg.ki * erro * dt;
  const float bruta = p + candidata + d;
  const bool empurraAcima = bruta > 100.0f && erro > 0;
  const bool empurraAbaixo = bruta < 0.0f && erro < 0;
  if (!empurraAcima && !empurraAbaixo) integral = candidata;
  integral = constrain(integral, 0.0f, 100.0f);

  definePotencia(constrain(p + integral + d, 0.0f, 100.0f));

  tempAnterior = temperatura;
  leituraAnterior = agora;

  // --- guarda de "nao esquenta" -----------------------------------------------
  // PID no maximo por semSubirMin minutos sem a temperatura subir 1 C: cartucho
  // solto, sensor fora do lugar ou porta aberta. A referencia desliza - cada
  // grau ganho recomeca a contagem -, entao ela vale a sessao inteira, nao so
  // o primeiro aquecimento.
  if (potencia >= POT_SATURADA) {
    if (!saturado || temperatura >= tempAoSaturar + 1.0f) {
      saturado = true;
      desdeSaturado = agora;
      tempAoSaturar = temperatura;
    } else if ((agora - desdeSaturado) / 60000 >= cfg.semSubirMin) {
      falha("aquecedor no maximo sem a temperatura subir");
      atualizaVentiladores();
      return;
    }
  } else {
    saturado = false;
  }

  estado = aquecedorLigado ? EST_AQUECENDO : EST_OCIOSO;
  motivo = "";
  atualizaVentiladores();
}


// Quanto de cada janela o cartucho fica ligado, em ms.
static uint32_t ligadoNaJanela() {
  return (uint32_t)(potencia * JANELA_MS / 100.0f);
}

static bool podeLigar() {
  return autorizado && estado != EST_FALHA && potencia > 0;
}

// Chamar sempre no loop: faz a janela de PWM do cartucho e a rampa dos
// ventiladores. O cartucho fica ligado na fracao inicial de cada janela de
// 2 s igual a potencia pedida.
void controleTick() {
  const bool pode = podeLigar();
  const uint32_t fase = millis() % JANELA_MS;
  const bool ligado = pode && fase < ligadoNaJanela();
  digitalWrite(PIN_AQUECEDOR, ligado ? HIGH : LOW);

  atualizaVentiladores();
}

EstadoAquecedor controleEstado() { return estado; }

const char *controleEstadoTexto() {
  switch (estado) {
    case EST_BLOQUEADO: return "DESLIGADO";
    case EST_OCIOSO:    return "NO ALVO";
    case EST_AQUECENDO: return "AQUECENDO";
    case EST_FALHA:     return "PROTECAO";
  }
  return "?";
}

const char *controleMotivo() { return motivo; }
bool controleAquecedorLigado() { return aquecedorLigado; }
uint8_t controlePotencia() { return (uint8_t)(potencia + 0.5f); }
bool controleAutorizado() { return autorizado; }
uint8_t controleVelocidade(uint8_t i) { return i < 2 ? velocidade[i] : 0; }

void controleAutoriza(bool v) {
  autorizado = v;
  motivo = "";
  definePotencia(0);
  zeraPid();
  estado = v ? EST_OCIOSO : EST_BLOQUEADO;
  diario.printf("Autorizacao      : %s\n", v ? "concedida pelo painel" : "retirada");
  if (v) {           // ligou o aquecedor: circulacao ligada e a 100%
    cfg.fan2Ligado = true;
    cfg.fan2Vel = 100;
  }
  atualizaVentiladores();
}

Config controleConfig() { return cfg; }

bool controleDefineConfig(const Config &n, const char **porque) {
  if (n.alvo < ALVO_MIN || n.alvo > ALVO_MAX) {
    *porque = "alvo fora de 20 a 55 C"; return false;
  }
  if (n.alvo >= TETO_ABSOLUTO) {
    *porque = "alvo precisa ficar abaixo do teto"; return false;
  }
  if (n.kp < 0 || n.kp > KP_MAX || n.ki < 0 || n.ki > KI_MAX ||
      n.kd < 0 || n.kd > KD_MAX) {
    *porque = "ganho do PID fora da faixa (kp 0-100, ki 0-5, kd 0-1000)"; return false;
  }
  if (n.pwmMin > 80)      { *porque = "piso de PWM acima de 80%"; return false; }
  // Abaixo do piso a helice nao parte, e aplica() subiria o valor sozinha:
  // o numero pedido deixaria de ser o que o ventilador recebe.
  if (n.fan2Vel < n.pwmMin || n.fan2Vel > 100) {
    *porque = "velocidade da circulacao fora do piso a 100%"; return false;
  }
  if (n.pos1 > 3600 || n.rampa1 > 3600) {
    *porque = "tempo longo demais"; return false;
  }
  if (n.semSubirMin < SUBIR_MIN || n.semSubirMin > SUBIR_MAX) {
    *porque = "protecao sem subir fora de 5 a 60 min"; return false;
  }

  const bool ligouCirculacao = n.fan2Ligado && !cfg.fan2Ligado;
  cfg = n;
  if (ligouCirculacao) cfg.fan2Vel = 100;   // ligar e sempre a 100%
  nvs.putFloat("alvo", cfg.alvo);
  nvs.putFloat("kp", cfg.kp);
  nvs.putFloat("ki", cfg.ki);
  nvs.putFloat("kd", cfg.kd);
  nvs.putUChar("pwmin", cfg.pwmMin);
  nvs.putUShort("pos1", cfg.pos1);
  nvs.putUShort("rmp1", cfg.rampa1);
  nvs.putUShort(NVS_SUBIR, cfg.semSubirMin);

  diario.printf("Config salva     : alvo %.1f C, PID %.2f/%.3f/%.1f, circulacao %s %u%%\n",
                cfg.alvo, cfg.kp, cfg.ki, cfg.kd, cfg.fan2Ligado ? "ligada" : "desligada",
                cfg.fan2Vel);
  // Aplica na hora: ligar, desligar ou mudar a faixa nao espera a proxima
  // leitura do sensor.
  atualizaVentiladores();
  *porque = "";
  return true;
}
