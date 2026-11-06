// ---------------------------------------------------------------------------
// Tela de configuracao do FAN 02, o ventilador de circulacao.
//
// Abre ao tocar no cartao FAN 02 do painel. Mostra e deixa mudar:
//   - ligado / desligado (toque alterna e salva na hora);
//   - velocidade maxima, usada com o aquecedor ligado;
//   - velocidade minima, usada com o aquecedor desligado.
//
// Maxima e minima nao sao digitadas aqui: o toque devolve um pedido de
// edicao, e quem chama abre o teclado. Assim esta tela nao precisa saber
// nada do teclado.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

enum ResultadoFan2 {
  FAN2_SEGUE,       // continua nesta tela
  FAN2_VOLTA,       // voltar ao painel
  FAN2_EDITA_MAX,   // abrir o teclado para a maxima
  FAN2_EDITA_MIN,   // abrir o teclado para a minima
};

// Desenha a tela inteira.
void fan2Abre();

// Redesenha so o que muda sozinho: qual faixa esta valendo e a velocidade
// atual. Chamar quando a velocidade do ventilador mudar.
void fan2Atualiza();

ResultadoFan2 fan2Toque(uint16_t x, uint16_t y);
