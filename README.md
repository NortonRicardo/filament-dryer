# Estufa de Filamento — ESP32

Secadora de filamento com ESP32: lê temperatura e umidade com um BME280,
mostra tudo num display de 4", controla o cartucho aquecedor por PID e dois
ventiladores por PWM, e serve um painel na rede local.

Depois que a caixa está fechada não há mais cabo USB. **Toda atualização a
partir daí é pelo Wi-Fi.**

---

## Antes de qualquer coisa: o arquivo `.env`

Nada compila sem ele. As credenciais da rede ficam fora do repositório.

```bash
cp .env.exemplo .env
```

Abra e preencha:

```
WIFI_SSID=nome-da-sua-rede
WIFI_PASSWORD=senha-da-sua-rede
SENHA_PADRAO=8-ou-mais-caracteres
```

O `.env` está no `.gitignore` e nunca é versionado. O build o converte em
`-DWIFI_SSID` e `-DWIFI_PASSWORD`; sem o arquivo, ele para com uma instrução
em vez de um erro de compilador.

> **`SENHA_PADRAO` é a senha da atualização e da rede própria `ESTUFA-SETUP`.**
> Fica separada da senha do Wi-Fi, que pode ser trocada pela própria placa.
> Precisa de 8 caracteres ou mais (exigência do WPA2) — o build recusa menos.

## Se a rede de casa mudar de nome ou senha

Não precisa de cabo. Depois de 2 min sem a rede, a placa abre a própria rede
**`ESTUFA-SETUP`**, com a senha `SENHA_PADRAO`, e o rodapé do display vira uma
listra laranja mostrando os dois.

Conecte o celular nela: a página de escolher rede abre sozinha. Toque na rede,
digite a senha, **CONECTAR**. Se der certo, a `ESTUFA-SETUP` some e a estufa
volta em `http://estufa.local`.

Enquanto isso ela continua medindo e controlando normalmente. Por essa rede só
se escolhe rede — o painel, com a autorização do aquecedor, não é servido ali.

---

## Primeira gravação: tem que ser pelo cabo

A atualização pelo Wi-Fi só existe depois que um firmware que a contém já
está rodando na placa. Então a primeira vez é sempre pelo USB.

```bash
pio run -t upload
```

Confirme no monitor que a rede subiu:

```bash
pio device monitor
```

Procure por estas três linhas:

```
Wi-Fi            : conectado, -60 dBm
Servidor         : http://192.168.1.35:80
Atualizacao      : pelo Wi-Fi liberada (porta 3232, com senha)
```

**Anote o IP.** Ele também aparece no rodapé do display, e serve de plano B
se o nome `estufa.local` não resolver.

A partir daqui o cabo pode sair.

---

## Atualizar pelo Wi-Fi

Um comando:

```bash
pio run -e ota -t upload
```

O `-e ota` não é opcional — sem ele a PlatformIO usa o ambiente do USB. Essa
separação é proposital: você nunca grava pela rede por engano.

Se o nome não resolver, use o IP diretamente:

```bash
pio run -e ota -t upload --upload-port 192.168.1.35
```

### O que precisa estar valendo

| Condição | Por quê |
|---|---|
| A placa está ligada | A atualização é recebida pelo firmware que está rodando |
| O computador está na mesma rede | É tráfego local, na porta 3232 |
| O `.env` tem a `SENHA_PADRAO` certa | Ela autentica a atualização |
| O firmware atual tem OTA | Caso contrário, só pelo cabo |

### A senha nunca aparece

O `scripts/senha_ota.py` lê a senha do `.env` e entrega ao `espota` no
momento do upload. Ela não está no `platformio.ini`, não é impressa no
terminal e não vai para o repositório.

O script também remove o `--debug` que a PlatformIO liga por padrão — ele
imprimiria todas as opções do `espota`, senha inclusive.

---

## O que acontece durante a atualização

1. **O aquecedor é desligado na hora.** Antes de receber o primeiro byte, o
   firmware retira a autorização. Não existe atualizar com o cartucho ligado.
2. **O display mostra o progresso**, redesenhado a cada 5%.
3. **Se falhar, nada se perde.** O firmware atual continua rodando e o erro
   aparece no diário. A placa só reinicia quando a gravação termina inteira.
4. **Depois de reiniciar, tudo volta desligado.** Aquecedor e ventiladores
   nascem parados por regra, e você reautoriza pelo painel.

---

## Acompanhar sem cabo

Com a caixa fechada o monitor serial deixa de existir. O mesmo conteúdo fica
disponível pela rede:

```
http://estufa.local/log
```

São as últimas ~6 KB do que o firmware escreveu, cada linha com o tempo desde
o boot. É lá que aparecem as proteções que dispararam, as quedas do sensor e
as reconexões de Wi-Fi.

O painel em si fica em `http://estufa.local` — ou no IP, se preferir.

---

## Quando não funciona

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| `Host ... not found` | O `.local` não resolveu | Use `--upload-port <IP>` |
| Trava em `Authenticating...` | Senha diferente da que está gravada | O `.env` precisa ter a senha que a placa usou quando foi gravada pelo cabo |
| `No response from device` | Placa desligada, fora da rede, ou firmware sem OTA | Confira o rodapé do display; se não houver IP ali, grave pelo cabo |
| Resolve o nome mas não conecta | Duas interfaces de rede na mesma faixa | No macOS, cabo **e** Wi-Fi no mesmo roteador quebra o roteamento. Desligue um dos dois |
| `espota` pede senha no terminal | O `extra_scripts` não rodou | Confira se o `post:scripts/senha_ota.py` está no `platformio.ini` |

Para descobrir o IP sem o display, com a placa na rede:

```bash
ping estufa.local
```

---

## Voltar para o cabo

Sempre possível, e é o caminho de recuperação quando a rede não coopera:

```bash
pio run -t upload
```

Sem o `-e ota`, volta a gravar pela USB.
