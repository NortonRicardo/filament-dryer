// ---------------------------------------------------------------------------
// Touch resistivo (XPT2046), lido pela TFT_eSPI no mesmo SPI do display.
//
// Cuida de duas coisas que o resto do firmware nao precisa saber:
//   - calibracao: feita uma vez e guardada na memoria do ESP32;
//   - um toque = um evento: segurar o dedo nao repete a tecla.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

// Chamar depois de tft.init() e setRotation(). Carrega a calibracao salva.
// Se nao houver nenhuma, ou se a tela estiver sendo tocada durante o boot,
// abre a tela de calibracao (setas nos cantos). Bloqueia so nesse caso.
void toqueInicia();

// Chamar no loop. Devolve true uma unica vez por toque, no momento em que o
// dedo encosta, com a posicao ja em pixels da tela.
bool toqueNovo(uint16_t &x, uint16_t &y);
