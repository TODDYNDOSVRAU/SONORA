# Sonora para KernelSU — integração dependente do firmware

## Estado atual

Ainda não existe um ZIP instalável nem um APK Android compilado. O TAR posterior, coletado pelo Termux, confirma SM-S911B / `dm1q`, ABI `arm64-v8a`, Android 17 / SDK 37, build `S911BXXUAZZHL`, fingerprint `samsung/dm1qxxx/dm1q:17/CP2A.260605.016/S911BXXUAZZHL:user/release-keys`, patch `2026-08-05` e kernel `5.15.207-g35b4d63d`. O SELinux está `Enforcing`.

## Achados dos XMLs do aparelho

Os 31 XMLs do TAR foram parseados com sucesso. `/vendor/etc/audio_effects_sec.xml` declara `dap_proxy` (`libswdap.so`) e a cadeia `postprocess` com `sa3d` para os streams `music`, `ring` e `alarm`. `/vendor/etc/audio_effects_spatializer.xml` registra `libswspatializer.so` com UUID `ccd4cf09-a79d-46c2-9aae-06a1698d6c8f`. O inventário lista essas bibliotecas em `/vendor/lib64/soundfx` com contexto `vendor_file`; a listagem também contém a árvore 32-bit `/vendor/lib/soundfx`. O dump de AudioFlinger mostra uma chain de sessão 0 com dois efeitos e o nome `Dolby_Spatializer_FX`. Isso demonstra recursos OEM presentes/configurados, mas não prova que nosso efeito esteja ativo, que todas as saídas/apps passem por essa cadeia ou que haja uma API pública para controlar o efeito proprietário.

O plano é adicionar somente o plugin Sonora próprio, sem copiar, substituir ou redistribuir `libswdap.so`, `libswspatializer.so` ou outras bibliotecas OEM. A escolha do XML/chain exato precisa preservar os efeitos Samsung já declarados; qualquer sobreposição deve manter o arquivo original completo e ser reversível.

## Dados ainda necessários

O TAR não inclui rótulos de `/data/vendor/audio`, `/data/vendor`, `/data/misc/audio` nem a versão do KernelSU Manager/metamodule. Esses dados ainda são necessários para definir um arquivo de controle que o `audioserver` possa ler sob SELinux enforcing e a montagem `system/` aceita pelo KernelSU. O erro anterior de `su` veio do shell ADB e não é prova de ausência do KernelSU.

## Requisitos antes de empacotar

A sintaxe dos XMLs foi validada, mas ainda não foi comprovado qual arquivo/chain o serviço está carregando como caminho de pós-processamento global, nem que o plugin Sonora seja aceito pelo loader específico desta build. Também faltam os rótulos SELinux do diretório de estado compartilhado para decidir onde `audioserver` poderá ler o bloco `mmap`. Um efeito legado AOSP só será empacotado após validar esse caminho; não substituir a configuração OEM às cegas. O plugin e o helper precisam ser compilados com o mesmo `SONORA_CONTROL_FILE` e ABI compatível com a build real.

O diretório `system/` de um módulo KernelSU só é montado com um metamodule compatível; `module.prop` é obrigatório para o reconhecimento do módulo. Consulte o [guia oficial de módulos KernelSU](https://kernelsu.org/guide/module.html). A implementação deve incluir merge reversível da configuração original, validação de pré-requisitos, scripts de desativação/remoção e uma rota clara de recuperação.

O APK e o módulo são artefatos separados. O APK final será construído pelo fluxo Mobile do Dashboard WebDev; o ZIP depende dos dados pendentes, de build NDK e de validação controlada no aparelho. Não instalar no telefone automaticamente. O callback de áudio lê parâmetros locais via `mmap`; o aplicativo não grava nem transmite o áudio de outros apps.
