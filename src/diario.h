// ---------------------------------------------------------------------------
// Diario: tudo o que o firmware escreve no monitor serial, guardado tambem
// na memoria para a rota /log do servidor.
//
// Com a caixa fechada nao ha cabo USB, e o monitor serial deixa de existir.
// O diario e o monitor serial que continua visivel pela rede: proteções,
// falhas do sensor, Wi-Fi, atualizacoes.
//
// Uso: diario.printf(...) e diario.println(...) no lugar de Serial.*. Sai nos
// dois lugares. Cada linha guardada ganha o tempo desde o boot na frente,
// [hh:mm:ss]. Guarda as ultimas ~6 KB; o mais antigo vai saindo.
// ---------------------------------------------------------------------------

#pragma once

#include <Arduino.h>

class Diario : public Print {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buf, size_t n) override;

  // O que esta guardado, do mais antigo ao mais novo.
  String texto() const;
};

extern Diario diario;

// A hora do relogio interno ja foi acertada pela internet (NTP)? Antes disso
// ela marca 1970, e o diario carimba o tempo desde o boot.
bool horaCerta();
