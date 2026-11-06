#include "controle.h"

#include <Arduino.h>
#include <Preferences.h>

// --- hardware ---------------------------------------------------------------
static const uint8_t PIN_AQUECEDOR = 2;   // LED azul de bordo, no lugar do cartucho
static const uint8_t PIN_FAN1 = 32;       // ventilador do hotend
static const uint8_t PIN_FAN2 = 33;       // ventilador de circulacao

// 25 kHz fica acima do que o ouvido capta. Abaixo disso o ventilador chia,
// porque a bobina vibra no ritmo do chaveamento.
static const uint32_t PWM_FREQ = 25000;
static const uint8_t  PWM_BITS = 8;
static const uint8_t  CANAL_FAN1 = 0;
static const uint8_t  CANAL_FAN2 = 1;

// --- limites de configuracao ------------------------------------------------
// O alvo para em 55 para sobrar margem ate o teto de 60.
static const float ALVO_MIN = 20.0f, ALVO_MAX = 55.0f;
static const float HIST_MIN = 0.2f,  HIST_MAX = 5.0f;

static Preferences nvs;

static Config cfg = {
  55.0f,  // alvo: PETG, o filamento usado aqui. Recomendado 55 a 70 C;
          // 55 e o maximo que a caixa permite (ALVO_MAX)
  1.0f,   // histerese
  false,  // circulacao: desligada no boot, so o painel web liga
  100,    // circulacao maxima, com o aquecedor ligado
  70,     // circulacao minima, com o aquecedor desligado
  30,     // piso de PWM: abaixo disso a helice nao parte
  180,    // 3 min a 100% depois de desligar
  120,    // mais 2 min descendo ate parar
  0,      // guarda de "nao esquenta" desligada enquanto o aquecedor for um LED
};

static EstadoAquecedor estado = EST_BLOQUEADO;
static const char *motivo = "";
static bool autorizado = false;
static bool aquecedorLigado = false;
static uint8_t velocidade[2] = {0, 0};

static uint32_t desdeDesligou = 0;   // millis em que o aquecedor desligou
static bool     jaAqueceu = false;   // houve ao menos um ciclo nesta sessao
static uint32_t ligadoDesde = 0;     // para a guarda de "nao esquenta"
static float    tempAoLigar = 0;

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

static void ligaAquecedor(bool liga, float temperatura) {
  if (liga == aquecedorLigado) return;

  aquecedorLigado = liga;
  digitalWrite(PIN_AQUECEDOR, liga ? HIGH : LOW);

  if (liga) {
    jaAqueceu = true;
    ligadoDesde = millis();
    tempAoLigar = temperatura;
  } else {
    desdeDesligou = millis();
  }
  Serial.printf("Aquecedor        : %s\n", liga ? "LIGADO" : "desligado");
}

static void falha(const char *porque) {
  ligaAquecedor(false, 0);
  autorizado = false;       // queda de protecao derruba a autorizacao junto
  estado = EST_FALHA;
  motivo = porque;
  Serial.printf("PROTECAO         : %s\n", porque);
}

// ---------------------------------------------------------------------------
// Ventiladores
//
// Ventilador do hotend: 100% enquanto aquece, segue 100% por pos1 depois de
// desligar, e so entao desce em rampa ate parar. O bloco continua quente
// quando o aquecedor corta - cortar o ventilador junto deixaria todo esse
// calor parado em cima do metal.
//
// Ventilador de circulacao: comeca desligado a cada boot. Depois de ligado no
// painel, gira direto, independente do aquecedor estar autorizado: na maxima
// enquanto o aquecedor esta ligado, na minima quando desligado.
// ---------------------------------------------------------------------------
static uint8_t velocidadeCirculacao() {
  if (!cfg.fan2Ligado) return 0;
  return aquecedorLigado ? cfg.fan2Max : cfg.fan2Min;
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
  ledcAttachPin(PIN_FAN1, CANAL_FAN1);
  ledcAttachPin(PIN_FAN2, CANAL_FAN2);
  aplica(0, 0);

  nvs.begin("estufa", false);
  // isKey antes dos floats: getFloat de chave ausente (primeiro boot)
  // imprime um erro no console. Os inteiros nao tem esse problema.
  if (nvs.isKey("alvo")) cfg.alvo      = nvs.getFloat("alvo", cfg.alvo);
  if (nvs.isKey("hist")) cfg.histerese = nvs.getFloat("hist", cfg.histerese);
  cfg.fan2Max     = nvs.getUChar("f2max", cfg.fan2Max);
  cfg.fan2Min     = nvs.getUChar("f2min", cfg.fan2Min);
  cfg.pwmMin      = nvs.getUChar("pwmin", cfg.pwmMin);
  cfg.pos1        = nvs.getUShort("pos1", cfg.pos1);
  cfg.rampa1      = nvs.getUShort("rmp1", cfg.rampa1);
  cfg.semSubirMin = nvs.getUShort("subir", cfg.semSubirMin);

  // Nao se le da memoria nem a autorizacao nem o liga/desliga do FAN 02:
  // os dois nascem falsos toda vez, por regra. Tudo comeca desligado.
  autorizado = false;
  cfg.fan2Ligado = false;
  estado = EST_BLOQUEADO;

  Serial.printf("Aquecedor        : DESLIGADO no boot, por regra\n");
  Serial.printf("Alvo             : %.1f C, histerese %.1f C, teto %.0f C\n",
                cfg.alvo, cfg.histerese, TETO_ABSOLUTO);
  Serial.printf("Ventiladores     : P%d e P%d, PWM %lu Hz, piso %u%%\n",
                PIN_FAN1, PIN_FAN2, (unsigned long)PWM_FREQ, cfg.pwmMin);
  Serial.printf("Circulacao       : DESLIGADA no boot, max %u%%, min %u%%\n",
                cfg.fan2Max, cfg.fan2Min);

  atualizaVentiladores();  // tudo em 0% ate o painel web ligar
}

void controleNovaLeitura(bool sensorOk, float temperatura) {
  if (!autorizado) {
    if (estado != EST_FALHA) estado = EST_BLOQUEADO;
    ligaAquecedor(false, temperatura);
    atualizaVentiladores();
    return;
  }

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

  // Guarda de "nao esquenta": aquecedor no maximo por muito tempo sem a
  // temperatura subir significa cartucho solto, sensor fora do lugar ou
  // porta aberta. Desligada por padrao enquanto o aquecedor for um LED,
  // que por motivos obvios nunca vai fazer a temperatura subir.
  if (aquecedorLigado && cfg.semSubirMin > 0) {
    const uint32_t minutos = (millis() - ligadoDesde) / 60000;
    if (minutos >= cfg.semSubirMin && temperatura <= tempAoLigar + 1.0f) {
      falha("aquecedor no maximo sem a temperatura subir");
      atualizaVentiladores();
      return;
    }
  }

  if (aquecedorLigado) {
    if (temperatura >= cfg.alvo) ligaAquecedor(false, temperatura);
  } else {
    if (temperatura <= cfg.alvo - cfg.histerese) ligaAquecedor(true, temperatura);
  }

  estado = aquecedorLigado ? EST_AQUECENDO : EST_OCIOSO;
  motivo = "";
  atualizaVentiladores();
}

void controleTick() { atualizaVentiladores(); }

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
bool controleAutorizado() { return autorizado; }
uint8_t controleVelocidade(uint8_t i) { return i < 2 ? velocidade[i] : 0; }

void controleAutoriza(bool v) {
  autorizado = v;
  motivo = "";
  if (!v) {
    ligaAquecedor(false, 0);
    estado = EST_BLOQUEADO;
  } else {
    estado = EST_OCIOSO;
  }
  Serial.printf("Autorizacao      : %s\n", v ? "concedida pelo painel" : "retirada");
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
  if (n.histerese < HIST_MIN || n.histerese > HIST_MAX) {
    *porque = "histerese fora de 0,2 a 5 C"; return false;
  }
  if (n.pwmMin > 80)      { *porque = "piso de PWM acima de 80%"; return false; }
  // Abaixo do piso a helice nao parte, e aplica() subiria o valor sozinha:
  // o numero salvo deixaria de ser o que o ventilador recebe.
  if (n.fan2Min < n.pwmMin || n.fan2Min > 100) {
    *porque = "circulacao minima fora do piso a 100%"; return false;
  }
  if (n.fan2Max < n.fan2Min || n.fan2Max > 100) {
    *porque = "circulacao maxima fora da minima a 100%"; return false;
  }
  if (n.pos1 > 3600 || n.rampa1 > 3600) {
    *porque = "tempo longo demais"; return false;
  }

  cfg = n;
  nvs.putFloat("alvo", cfg.alvo);
  nvs.putFloat("hist", cfg.histerese);
  nvs.putUChar("f2max", cfg.fan2Max);
  nvs.putUChar("f2min", cfg.fan2Min);
  nvs.putUChar("pwmin", cfg.pwmMin);
  nvs.putUShort("pos1", cfg.pos1);
  nvs.putUShort("rmp1", cfg.rampa1);
  nvs.putUShort("subir", cfg.semSubirMin);

  Serial.printf("Config salva     : alvo %.1f C, hist %.1f C, circulacao %s %u-%u%%\n",
                cfg.alvo, cfg.histerese, cfg.fan2Ligado ? "ligada" : "desligada",
                cfg.fan2Min, cfg.fan2Max);
  // Aplica na hora: ligar, desligar ou mudar a faixa nao espera a proxima
  // leitura do sensor.
  atualizaVentiladores();
  *porque = "";
  return true;
}
