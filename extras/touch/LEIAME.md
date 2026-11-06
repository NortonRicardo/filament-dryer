# Touch guardado

O display de 4" (ST7796S) veio sem o chip do touch, o XPT2046 no lugar do U2.
Este código funcionava compilado e fica aqui até chegar um módulo com o chip.
A PlatformIO não compila `extras/`.

| Arquivo | O que faz |
|---|---|
| `toque.cpp/.h` | calibração salva na memória, um evento por toque, diagnóstico cru |
| `teclado.cpp/.h` | teclado numérico de tela cheia, para °C ou % |
| `tela_fan2.cpp/.h` | tela do FAN 02: liga/desliga, máxima, mínima |
| `main_com_touch.cpp` | o `main.cpp` como estava com o touch, para referência |

## Para religar

1. Mover os seis arquivos de volta para `src/`.
2. Em `platformio.ini`, nos `build_flags`:
   ```
   -DTFT_MISO=19
   -DTOUCH_CS=16
   -DSPI_TOUCH_FREQUENCY=2500000
   ```
3. No `main.cpp`: chamar `toqueInicia()` depois de `tft.setRotation()`, e no
   loop repassar `toqueNovo(x, y)` para as telas. O `main.cpp` inteiro da
   versão com touch está em `main_com_touch.cpp`: o roteamento (cartão de
   temperatura abre o teclado da temperatura máxima, cartão FAN 02 abre a
   tela dele) fica em `trataToque()`. Ele é anterior à volta do servidor,
   então serve de referência, não para copiar por cima.

Fiação do touch: T_CLK no P18 e T_DIN no P23 (junto com o display), T_DO no
P19, T_CS no P16. O SDO(MISO) do display fica solto: no ST7796 ele prende a
linha do P19.
