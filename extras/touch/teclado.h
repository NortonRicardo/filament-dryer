// ---------------------------------------------------------------------------
// Teclado numerico de tela cheia, para digitar um valor pelo touch.
//
// Nao bloqueia: o loop continua lendo o sensor e rodando o controle enquanto
// o teclado esta aberto. Quem chama so precisa nao desenhar o painel por
// cima dele e repassar os toques.
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>

enum ResultadoTeclado {
  TECLADO_SEGUE,    // continua aberto
  TECLADO_OK,       // valor digitado e dentro da faixa
  TECLADO_CANCELA,  // fechou sem mudar nada
};

// Desenha o teclado por cima de tudo. O valor atual aparece apagado no
// visor, como referencia, ate a primeira tecla.
//
// unidade vai ao lado do numero ("C", "%"); grau desenha o anelzinho antes
// dela. Sem decimal, a tecla do ponto fica apagada e nao responde. O numero
// de digitos inteiros sai do maximo: ate 60 sao dois, ate 100 sao tres.
void tecladoAbre(const char *titulo, float valorAtual, bool temValor,
                 float minimo, float maximo,
                 const char *unidade, bool grau, bool decimal);

// Repassa um toque. Em TECLADO_OK, valor recebe o numero digitado.
ResultadoTeclado tecladoToque(uint16_t x, uint16_t y, float &valor);
