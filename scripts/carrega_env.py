# Le o arquivo .env da raiz do projeto e transforma cada chave num -D para o
# compilador. Serve para as credenciais do Wi-Fi nao ficarem dentro do codigo
# nem do platformio.ini, que sao versionados.
#
# Chamado por extra_scripts no platformio.ini.

import os

Import("env")  # noqa: F821  (injetado pela PlatformIO)

RAIZ = env["PROJECT_DIR"]  # noqa: F821
CAMINHO = os.path.join(RAIZ, ".env")
ESPERADAS = ("WIFI_SSID", "WIFI_PASSWORD", "SENHA_PADRAO")


def falha(mensagem):
    print("\n*** %s\n" % mensagem)
    Exit(1)  # noqa: F821


if not os.path.isfile(CAMINHO):
    falha(
        "Falta o arquivo .env com as credenciais do Wi-Fi.\n"
        "    Crie a partir do modelo:  cp .env.exemplo .env\n"
        "    Ele esta no .gitignore e nao vai para o repositorio."
    )

valores = {}
with open(CAMINHO, encoding="utf-8") as arquivo:
    for numero, linha in enumerate(arquivo, 1):
        linha = linha.strip()
        if not linha or linha.startswith("#"):
            continue
        if "=" not in linha:
            falha("Linha %d do .env nao tem '=': %s" % (numero, linha))
        chave, valor = linha.split("=", 1)
        # A senha pode conter '#', entao nao se corta comentario a direita.
        valores[chave.strip()] = valor.strip()

faltando = [c for c in ESPERADAS if not valores.get(c)]
if faltando:
    falha("O .env nao define: %s" % ", ".join(faltando))

for chave in ESPERADAS:
    # StringifyMacro cuida das aspas e do escape: a senha pode ter @, $ ou \.
    env.Append(CPPDEFINES=[(chave, env.StringifyMacro(valores[chave]))])  # noqa: F821

# A senha nunca e impressa; so a confirmacao de que foi carregada.
print("carrega_env: %s definidas a partir do .env (rede '%s')"
      % (len(ESPERADAS), valores["WIFI_SSID"]))
