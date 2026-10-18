# Sonora para KernelSU — integração dependente do firmware

## Estado atual

Ainda não existe um ZIP instalável nem um APK Android compilado. O 7z enviado reporta Samsung SM-S911B / `dm1q`, ABI `arm64-v8a`, Android 17 / SDK 37, build `S911BXXUAZZHL`, fingerprint `samsung/dm1qxxx/dm1q:17/CP2A.260605.016/S911BXXUAZZHL:user/release-keys`, patch de segurança `2026-08-05` e kernel `5.15.207-g35b4d63d`.

O 7z não contém os XMLs de efeitos. `audio-effect-config-paths.txt` e os relatórios de diretórios trazem erros de shell; `selinux-mode.txt` informa `su: inaccessible or not found`. Isso significa que a consulta root via ADB não foi bem-sucedida, não que o KernelSU esteja ausente. O acesso `su` por ADB depende da versão/configuração do gerenciador e o projeto não deve presumir o provedor root. Um issue upstream do KernelSU registra problemas de `su` em certos shells e a evolução desse comportamento: https://github.com/tiann/KernelSU/issues/2647

## Próxima coleta

No Windows, use [`scripts/collect-s23-audio-target.ps1`](../scripts/collect-s23-audio-target.ps1). Esse coletor lê propriedades de build, procura `audio_effects*.xml` nos diretórios padrão e tenta copiá-los por ADB sem `su`; não altera arquivos do telefone nem grava áudio. Se algum XML continuar inacessível, anexe os arquivos diretamente, obtidos por um gerenciador de arquivos com root. Informe separadamente a versão do KernelSU Manager e o nome/versão do metamodule ativo (ou “nenhum”).

## Requisitos antes de empacotar

A configuração efetiva de áudio, as bibliotecas de efeitos e o contexto SELinux que permita ao `audioserver` acessar o bloco compartilhado ainda precisam ser verificados. Um efeito legado AOSP só será usado se a build realmente carregar esse formato de plugin; caso contrário, a integração deve parar, não trocar o XML do fabricante às cegas. O plugin e o helper precisam ser compilados com o mesmo `SONORA_CONTROL_FILE` e com headers/ABI compatíveis com a build real.

O diretório `system/` de um módulo KernelSU só é montado com um metamodule compatível; `module.prop` é obrigatório para o reconhecimento do módulo. Consulte o [guia oficial de módulos KernelSU](https://kernelsu.org/guide/module.html). A implementação deve incluir merge reversível da configuração original, validação de pré-requisitos, scripts de desativação/remoção e uma rota clara de recuperação.

O APK e o módulo são artefatos separados. O APK final será construído pelo fluxo Mobile do Dashboard WebDev; o ZIP depende dos dados pendentes, de build NDK e de validação controlada no aparelho. Não instalar no telefone automaticamente. O callback de áudio lê parâmetros locais via `mmap`; o aplicativo não grava nem transmite o áudio de outros apps.
