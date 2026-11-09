// ---------------------------------------------------------------------------
// Avisos no celular pelo ntfy (https://ntfy.sh).
//
// Notificacao web pelo Safari exige HTTPS com certificado valido, e a pagina
// da estufa e http://estufa.local - o iPhone nao libera. O ntfy contorna isso:
// o celular assina um topico no app, e a estufa publica nele com um POST.
// Chega como notificacao normal, em casa ou na rua.
//
// O topico fica no .env (NTFY_TOPICO): quem souber o nome le os avisos, entao
// ele faz as vezes de senha. Sem ele no .env, os avisos ficam desligados.
// ---------------------------------------------------------------------------

#pragma once

#include <Arduino.h>

void avisoInicia();
bool avisoLigado();

// Enfileira e volta na hora; o envio acontece em segundo plano. Prioridade
// do ntfy: 3 normal, 4 alta, 5 urgente (fura o silencioso so no Android).
// Titulo so em ASCII: vai num cabecalho HTTP.
bool avisa(const char *titulo, const char *texto, uint8_t prioridade = 3);

// Resultado do ultimo envio, uma vez: codigo HTTP (200 = entregue) ou
// negativo se nem chegou ao servidor. Para o loop registrar no diario.
bool avisoResultado(int &codigo);
