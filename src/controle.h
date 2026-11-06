// ---------------------------------------------------------------------------
// Controle do aquecedor e dos dois ventiladores.
//
// Separado do main.cpp porque e a parte com regra, e regra merece ficar junta
// e legível. O main so le o sensor e desenha; quem decide e aqui.
//
// A regra principal vem do projeto original e nao se negocia:
//
//     "o aquecedor nunca liga sozinho. Ao ligar ou reiniciar o ESP32, o
//      aquecedor comeca bloqueado. O aquecimento so comeca depois de
//      autorizado no painel web. Se faltar energia no meio da secagem, ele
//      volta bloqueado."
//
// Por isso a autorizacao NAO e salva na memoria: ela nasce falsa a cada boot,
// de proposito. O mesmo vale para o liga/desliga do FAN 02: ao ligar o
// ESP32, aquecedor e ventiladores comecam todos desligados, e so o painel web
// liga. Tudo o mais (alvo, faixas, tempos) e salvo.
//
// Nesta etapa o aquecedor e o LED azul de bordo (GPIO2), nao o cartucho. A
// logica e a mesma; so a carga e inofensiva.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

// Teto absoluto, do projeto original: o plastico interno da geladeira aguenta
// ate 60 graus. Acima disso o aquecedor corta, independente do alvo.
static const float TETO_ABSOLUTO = 60.0f;

enum EstadoAquecedor {
  EST_BLOQUEADO,   // ninguem ligou - estado de nascimento ("DESLIGADO")
  EST_OCIOSO,      // autorizado, mas a temperatura ja esta no alvo
  EST_AQUECENDO,   // aquecedor ligado
  EST_FALHA,       // protecao disparou; a autorizacao caiu junto
};

struct Config {
  float    alvo;        // graus que se quer atingir
  float    histerese;   // quanto pode cair abaixo do alvo antes de religar
  bool     fan2Ligado;  // circulacao ligada; nasce falso a cada boot, nao e salvo
  uint8_t  fan2Max;     // % da circulacao com o aquecedor ligado
  uint8_t  fan2Min;     // % da circulacao com o aquecedor desligado
  uint8_t  pwmMin;      // % abaixo do qual o ventilador nao parte
  uint16_t pos1;        // s que o ventilador do hotend fica a 100% apos desligar
  uint16_t rampa1;      // s que ele leva descendo de 100% ate parar
  uint16_t semSubirMin; // min no maximo sem a temperatura subir; 0 desliga
};

void controleInicia();

// Chamar a cada leitura do sensor. sensorOk falso ja conta como falha.
void controleNovaLeitura(bool sensorOk, float temperatura);

// Chamar sempre no loop: e quem faz a rampa descer suavemente.
void controleTick();

EstadoAquecedor controleEstado();
const char *controleEstadoTexto();
const char *controleMotivo();      // vazio quando nao ha falha
bool controleAquecedorLigado();
bool controleAutorizado();
uint8_t controleVelocidade(uint8_t ventilador);  // 0 ou 1 -> 0 a 100 %

// So o painel web chama. Autorizar com falha pendente limpa a falha.
void controleAutoriza(bool autorizado);

Config controleConfig();

// Valida antes de salvar. Devolve false e nao grava nada se algum campo
// estiver fora de faixa; motivo recebe o porque.
bool controleDefineConfig(const Config &nova, const char **motivo);
