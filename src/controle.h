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
// O aquecedor e o cartucho de 12 V / 50 W, chaveado por um IRLB8721 no
// GPIO25. A potencia sai de um PID (0 a 100%) e chega ao cartucho por PWM
// lento: janela de 2 s, ligado a fracao da janela que o PID pedir. Mesmo
// principio do Dry Fila Box do Marlon Nardi, que usa PID com analogWrite a
// ~490 Hz; aqui a janela e longa porque o aquecedor tem inercia de sobra e
// assim o MOSFET e a fonte chaveiam centenas de vezes menos.
//
// Protecoes no firmware: teto absoluto de 60 C, sensor sem leitura, e PID
// pedindo potencia maxima por semSubirMin minutos sem a temperatura subir
// 1 C. A unica protecao de hardware e o fusivel de 7,5 A na linha do
// cartucho (o termostato KSD301 do projeto original e recomendado).
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
  bool     fan2Ligado;  // circulacao ligada; nasce falso a cada boot, nao e salvo
  uint8_t  fan2Max;     // % da circulacao com o aquecedor ligado
  uint8_t  fan2Min;     // % da circulacao com o aquecedor desligado
  uint8_t  pwmMin;      // % abaixo do qual o ventilador nao parte
  uint16_t pos1;        // s que o ventilador do hotend fica a 100% apos desligar
  uint16_t rampa1;      // s que ele leva descendo de 100% ate parar
  uint16_t semSubirMin; // min ligado sem a temperatura subir 1 C; 5 a 60
};

void controleInicia();

// Chamar a cada leitura do sensor. sensorOk falso ja conta como falha.
void controleNovaLeitura(bool sensorOk, float temperatura);

// Chamar sempre no loop: e quem faz a rampa descer suavemente.
void controleTick();

// Desliga o cartucho por um instante para a leitura do sensor acontecer com
// o barramento quieto. O I2C deste projeto nao sobrevive aos 4,2 A passando;
// como o bloco aquecedor tem inercia de minutos e a pausa e de dezenas de
// milissegundos, o custo termico e nulo.
//
// controleTick() respeita o silencio: com ele ligado, o pino fica baixo
// independente do que o PID pediu.
void controleSilenciaSaida(bool silenciar);

// Para ler o sensor no trecho em que o cartucho ja esta desligado pelo
// proprio PWM, sem pausa extra.
//   controleSaidaQuieta(ms): o cartucho esta desligado agora e continua
//     desligado pelos proximos ms (ou nem esta ligado).
//   controleTemTrechoQuieto(ms): a potencia esta entre 0 e 100% e a parte
//     desligada de cada janela de 2 s tem pelo menos ms - vale esperar.
bool controleSaidaQuieta(uint32_t ms);
bool controleTemTrechoQuieto(uint32_t ms);

EstadoAquecedor controleEstado();
const char *controleEstadoTexto();
const char *controleMotivo();      // vazio quando nao ha falha
bool controleAquecedorLigado();    // PID pedindo mais que 0%
uint8_t controlePotencia();        // 0 a 100 %, o que o PID pediu
bool controleAutorizado();
uint8_t controleVelocidade(uint8_t ventilador);  // 0 ou 1 -> 0 a 100 %

// So o painel web chama. Autorizar com falha pendente limpa a falha.
void controleAutoriza(bool autorizado);

Config controleConfig();

// Valida antes de salvar. Devolve false e nao grava nada se algum campo
// estiver fora de faixa; motivo recebe o porque.
bool controleDefineConfig(const Config &nova, const char **motivo);
