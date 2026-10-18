# Sonora para KernelSU — integração dependente do firmware

## Estado atual

Ainda não existe um ZIP KernelSU instalável nem um APK Android compilado. O alvo reporta Samsung SM-S911B / `dm1q`, ABI `arm64-v8a`, Android 17 / SDK 37, build `S911BXXUAZZHL`, fingerprint `samsung/dm1qxxx/dm1q:17/CP2A.260605.016/S911BXXUAZZHL:user/release-keys`, patch `2026-08-05` e kernel `5.15.207-g35b4d63d`. A coleta mais recente confirmou SELinux `Enforcing` e root (`uid=0`, domínio `u:r:ksu:s`).

`/data/vendor/audio` existe como `audio:audio`, modo `0770`, contexto `u:object_r:vendor_audio_data_file:s0`; `/data/vendor` usa `vendor_data_file`. `/data/misc/audio` não existe. A tela KernelSU-Next enviada pelo usuário mostra Manager `v3.2.0-21-g5a4a7187-spoofed (33150)`, hook `Tracepoint`, metamodule **Não instalado**, Zygisk Next `1.5.0` ativo, kernel `5.15.207-g35b4d63d (aarch64)`, Android 17 (API 37), ABI arm64-v8a e SELinux impondo. Portanto, não há metamodule de montagem configurado neste momento; Zygisk Next não o substitui. `/data/adb/modules` lista `playintegrityfix`, `zygisk_vector` e `zygisksu`, que não são evidência de um provider de montagem. A versão do core KernelSU-Next não aparece na captura e continua pendente.

## XML e chain de áudio

Os 31 XMLs do TAR foram parseados com sucesso. `/vendor/etc/audio_effects_sec.xml` declara `dap_proxy` (`libswdap.so`) e `sa3d` para os streams `music`, `ring` e `alarm`. `/vendor/etc/audio_effects_spatializer.xml` registra `libswspatializer.so` com UUID `ccd4cf09-a79d-46c2-9aae-06a1698d6c8f`. O dump do AudioFlinger inclui `Dolby_Spatializer_FX`, mas isso não prova que o plugin Sonora será carregado ou que buffers sejam processados.

`audio-config-paths.txt` lista tanto `/vendor/etc/audio_effects_sec.xml` quanto `/system/vendor/etc/audio_effects_sec.xml`; as cópias encontradas são idênticas. Essa lista não prova qual caminho o loader seleciona em runtime nem se são aliases no aparelho. O overlay do módulo ainda não escolhe caminho.

`scripts/merge-s23-audio-effects.py` insere library/effect/`apply` Sonora após `sa3d`, somente no stream `music`, gravando uma cópia nova do XML. Foi testado contra o XML Samsung recebido: valida a estrutura, é idempotente e preserva o texto original fora das três inserções. Não modifica o aparelho, não sobrescreve o original nem copia bibliotecas OEM.

## IPC, permissões e inicialização

O caminho candidato do bloco de controle é `/data/vendor/audio/sonora_state.bin`. O helper cria apenas arquivo novo como `root:audio`, modo `0640`; rejeita symlinks, hardlinks e arquivos preexistentes com owner, grupo, modo ou tamanho incorretos. O plugin abre em leitura e valida tipo, tamanho, owner/grupo e link count antes do `mmap`. A ABI de 64 bytes usa palavras de 32 bits acessadas atomicamente; floats são transmitidos como bits. O callback não abre arquivos, não aloca e não usa mutex. Dois slots de DSP e um contador de leitores deixam `SET_CONFIG` preparar e publicar uma configuração sem mutar o DSP em uso; `RESET` é executado no callback.

`post-fs-data.sh` tenta inicializar o estado antes do carregamento normal dos efeitos. O plugin também tenta remapear em `SET_CONFIG`/`ENABLE` se o arquivo não existia quando a instância foi criada. O helper usa `O_NOFOLLOW` e, na branch Android, fixa a cadeia de diretórios e exige `/data/vendor/audio` com owner/grupo/permissões reportados. Isso ainda não prova que SELinux permita ao domínio `audioserver` abrir/mapear o arquivo.

Os testes host cobrem bypass, posição estéreo, rejeição de configuração mono, symlink, flag AOSP `INSERT_LAST`, remapeamento tardio e reconfiguração concorrente. O helper e a branch Android do plugin/helper compilam no compilador host com `__ANDROID__`; **não** foram compilados com o NDK nem carregados no S23.

## O que falta para empacotar e validar

`kernelsu/` contém somente `module.prop` e hooks experimentais de boot/desinstalação; faltam as bibliotecas e o binário compilados, a cópia XML selecionada, o overlay e as regras de contexto/SELinux. **Não é um módulo instalável.** A captura confirma que não há metamodule instalado, então o overlay XML necessário não está disponível. Antes do ZIP funcional, faltam: core KernelSU-Next (versão); instalação/configuração pelo usuário de um metamodule compatível; confirmação do caminho XML usado pelo loader; build NDK; validação de leitura do arquivo pelo domínio `audioserver`; e teste de reprodução no aparelho. Não instalei nem alterei nada no telefone. Consulte o [guia oficial de módulos KernelSU](https://kernelsu.org/guide/module.html).

O APK e o módulo são artefatos separados; o APK deve ser construído pelo fluxo Mobile do Dashboard WebDev. Não instalar nada automaticamente no telefone. O DSP não grava nem transmite áudio de outros apps; o efeito proposto atende somente à cadeia `music` se o firmware carregar o plugin. O bypass é o padrão e o hook de desinstalação tenta desativá-lo antes da remoção; reinicie depois de remover um efeito que o AudioFlinger tenha carregado.
