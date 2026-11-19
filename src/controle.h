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
// O aquecedor e o PTC de 12 V / 180 W (~15 A), chaveado por um MOSFET de
// nivel logico (IRLB3034 ou IRLB8721) no GPIO25. A potencia sai de um PID
// (0 a 100%) e chega ao aquecedor por PWM
// lento: janela de 2 s, ligado a fracao da janela que o PID pedir. Mesmo
// principio do Dry Fila Box do Marlon Nardi, que usa PID com analogWrite a
// ~490 Hz; aqui a janela e longa porque o aquecedor tem inercia de sobra e
// assim o MOSFET e a fonte chaveiam centenas de vezes menos.
//
// Protecoes no firmware: teto absoluto de 60 C, sensor sem leitura, e PID
// pedindo potencia maxima por semSubirMin minutos sem a temperatura subir
// 1 C. Protecao de hardware, independente do firmware: fusivel de 20 A e um
// rele cortado pelo termostato KSD301 (70 C) no +12 V do aquecedor.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

// Teto absoluto, do projeto original: o plastico interno da geladeira aguenta
// ate 60 graus. Acima disso o aquecedor corta, independente do alvo.
static const float TETO_ABSOLUTO = 60.0f;

enum EstadoAquecedor {
  EST_BLOQUEADO,   // ninguem ligou - estado de nascimento ("DESLIGADO")
  EST_OCIOSO,      // autorizado, PID pedindo 0% (acima do alvo)
  EST_AQUECENDO,   // autorizado, PID pedindo mais que 0%
  EST_FALHA,       // protecao disparou; a autorizacao caiu junto
};

struct Config {
  float    alvo;        // graus que se quer atingir
  float    kp;          // PID: % de potencia por grau de erro
  float    ki;          // PID: % por grau por segundo de erro acumulado
  float    kd;          // PID: % por grau/s de subida (freia a chegada)
  // Fan 1 (fan2*) e Fan 2 (fan3*), de circulacao, cada um com o seu
  // liga/desliga e a sua velocidade. Nascem desligados a cada boot e nao sao
  // salvos. Ligar um deles, ou o aquecedor, o poe a 100%; da para baixar
  // depois.
  bool     fan2Ligado;
  uint8_t  fan2Vel;
  bool     fan3Ligado;
  uint8_t  fan3Vel;
  uint8_t  pwmMin;      // % abaixo do qual o ventilador nao parte
  uint16_t pos1;        // s que o ventilador do aquecedor fica a 100% apos desligar
  uint16_t rampa1;      // s que ele leva descendo de 100% ate parar
  uint16_t semSubirMin; // min ligado sem a temperatura subir 1 C; 5 a 60
};

void controleInicia();

// Chamar a cada leitura do sensor. sensorOk falso ja conta como falha.
void controleNovaLeitura(bool sensorOk, float temperatura);

// Chamar sempre no loop: e quem faz a rampa descer suavemente.
void controleTick();

EstadoAquecedor controleEstado();
const char *controleEstadoTexto();
const char *controleMotivo();      // vazio quando nao ha falha
bool controleAquecedorLigado();    // PID pedindo mais que 0%
uint8_t controlePotencia();        // 0 a 100 %, o que o PID pediu
bool controleAutorizado();
uint8_t controleVelocidade(uint8_t ventilador);  // 0 aquecedor, 1 Fan 1, 2 Fan 2 -> 0 a 100 %

// So o painel web chama. Autorizar com falha pendente limpa a falha.
void controleAutoriza(bool autorizado);

Config controleConfig();

// Valida antes de salvar. Devolve false e nao grava nada se algum campo
// estiver fora de faixa; motivo recebe o porque.
bool controleDefineConfig(const Config &nova, const char **motivo);
