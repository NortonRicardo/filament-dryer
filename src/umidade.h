// ---------------------------------------------------------------------------
// Umidade absoluta - quanta agua existe mesmo no ar, em gramas por metro
// cubico. Formula de Magnus, a mesma do projeto DryFilaBox do Marlon Nardi.
//
// A umidade RELATIVA cai so de aquecer o ar, sem uma gota sair. A ABSOLUTA
// so cai quando a agua realmente deixa a caixa: e ela que diz se a secagem
// esta funcionando.
//
// Conferida contra tabela: 20 C e 50% dao 8,64 g/m3 (tabela: 8,65).
// ---------------------------------------------------------------------------

#pragma once

#include <math.h>

static inline float umidadeAbsoluta(float tempC, float umidadeRel) {
  const float pressaoSat = 6.112f * expf((17.67f * tempC) / (tempC + 243.5f));
  return pressaoSat * umidadeRel * 2.1674f / (273.15f + tempC);
}
