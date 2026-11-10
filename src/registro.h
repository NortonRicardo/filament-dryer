// ---------------------------------------------------------------------------
// Registro em flash: o diario e as medidas sobrevivem a reinicio e a falta de
// energia.
//
// Usa a particao "spiffs" (1,4 MB), que estava vazia, com LittleFS. Cada
// registro e um par de arquivos, o atual e o anterior: quando o atual enche,
// o anterior e apagado e o atual passa a ser o anterior. O mais antigo sai
// primeiro, e o espaco ocupado nunca passa do limite.
//
//   diario  :  8% da particao  (~115 KB, umas 1900 linhas)
//   medidas : 70% da particao  (~1 MB, 1 por minuto, ~87 dias)
//   livre   : 22%, de folga - o LittleFS precisa de blocos livres para
//             gravar com seguranca, e encher 100% faz a gravacao falhar.
//
// Desgaste: uma medida por minuto sao ~12 KB por dia. A flash aguenta ~100 mil
// apagamentos por bloco e o LittleFS espalha a escrita pela particao toda -
// decadas nesse ritmo.
// ---------------------------------------------------------------------------

#pragma once

#include <Arduino.h>
#include <WebServer.h>

// Monta a particao (formata na primeira vez). Chamar antes de qualquer
// linha do diario, para o boot tambem ficar gravado.
void registroInicia();

// O diario chama a cada linha completa.
void registroLinha(const char *linha, size_t n);

// O main chama a cada leitura boa; grava no maximo uma por minuto.
void registroMedida(float temperatura, float umidade);

// /log: o diario gravado, do mais novo (no topo) ao mais antigo.
void registroEnviaLog(WebServer &s);

// /historico: as medidas em CSV (separador ";" e virgula decimal, para o
// Excel em portugues abrir direto). horas = 0 envia tudo.
void registroEnviaHistorico(WebServer &s, uint32_t horas);

// Apaga todas as medidas gravadas (o diario fica).
void registroLimpaHistorico();

// Quanto da particao esta em uso, para o cabecalho do boot.
size_t registroUsado();
size_t registroTotal();
