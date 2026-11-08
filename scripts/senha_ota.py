# Entrega a senha da atualizacao pelo Wi-Fi ao espota.py, sem ela aparecer no
# platformio.ini (versionado). A senha e a mesma do Wi-Fi, lida do .env.
#
# Roda como "post:" porque a plataforma monta os parametros do espota DEPOIS
# dos scripts "pre:" e os substitui inteiros - um --auth posto antes seria
# apagado. So age quando upload_protocol = espota (o ambiente "ota").

import os

Import("env")  # noqa: F821

if env.GetProjectOption("upload_protocol", "") == "espota":  # noqa: F821
    caminho = os.path.join(env["PROJECT_DIR"], ".env")  # noqa: F821
    senha = ""
    with open(caminho, encoding="utf-8") as arquivo:
        for linha in arquivo:
            linha = linha.strip()
            if linha.startswith("WIFI_PASSWORD="):
                senha = linha.split("=", 1)[1].strip()

    if not senha:
        print("\n*** senha_ota: WIFI_PASSWORD nao encontrada no .env\n")
        Exit(1)  # noqa: F821

    # A plataforma liga o --debug do espota, que imprime todas as opcoes -
    # inclusive a senha. Tira o --debug antes de acrescentar a senha.
    flags = [f for f in env.get("UPLOADERFLAGS", []) if f not in ("--debug", "-d")]  # noqa: F821
    env.Replace(UPLOADERFLAGS=flags + ["--auth=" + senha])  # noqa: F821
    print("senha_ota: senha da atualizacao entregue ao espota (nao impressa)")
