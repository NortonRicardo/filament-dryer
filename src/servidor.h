// ---------------------------------------------------------------------------
// Servidor HTTP: serve a pagina de medicoes na rede local.
//
// Separado do main.cpp de proposito - rede e uma preocupacao inteira, e o
// loop principal nao precisa conhecer nada dela alem destas funcoes.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

// Comeca a conexao e volta na hora. Nao bloqueia: o sensor e a tela
// continuam funcionando enquanto o Wi-Fi sobe, ou se ele nunca subir.
void servidorInicia();

// Chamar no loop. Cuida da conexao e atende os pedidos.
void servidorAtende();

// Guarda uma leitura no historico dos ultimos 5 minutos.
void servidorRegistra(float temperatura, float umidade);

// Marca que o sensor sumiu, para a pagina dizer isso em vez de mostrar o
// ultimo valor como se fosse atual.
void servidorSemSensor();

// O que mostrar na ultima linha do display: "192.168.0.12:80",
// "conectando...", "sem Wi-Fi" ou "estufa.local". Nunca nulo.
const char *servidorEndereco();

// Umidade absoluta da ultima leitura, em g/m3.
float umidadeAbsolutaAtual();

// Quantas leituras o filtro de plausibilidade jogou fora desde o boot.
// Implementada no main.cpp. Numero subindo = ruido no barramento I2C,
// tipicamente o cartucho chaveando perto do cabo do sensor.
uint32_t leiturasDescartadas();

// Rede propria aberta (a de casa sumiu ha mais de 2 min). O rodape do display
// vira uma listra de aviso com o nome e a senha dela.
bool servidorPortalAtivo();
