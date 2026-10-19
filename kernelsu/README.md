# Sonora Spatial Audio — módulo KernelSU experimental

## Estado atual

Foi gerado o módulo ZIP separado do APK: [`dist/sonora-kernelsu-s23-0.2.0-experimental.zip`](../dist/sonora-kernelsu-s23-0.2.0-experimental.zip). O alvo é Samsung Galaxy S23 SM-S911B (`dm1q`), One UI 9 Beta, Android 17/API 37, `arm64-v8a`, kernel `5.15.207-g35b4d63d`. O usuário reportou que instalou `meta-overlayfs`, reiniciou e o KernelSU-Next passou a mostrar o metamodule como instalado; a versão do metamodule e do core não foi fornecida.

A coleta reportou root (`uid=0`), SELinux `Enforcing`, `/data/vendor/audio` como `audio:audio`, modo `0770`, rótulo `u:object_r:vendor_audio_data_file:s0` e `/data/vendor` como `vendor_data_file`. O caminho do bloco de controle é `/data/vendor/audio/sonora_state.bin`; o helper o cria somente se ainda não existir, como `root:audio`/0640. Não foram incluídas regras que afrouxem SELinux.

**O ZIP é experimental, não é uma confirmação de compatibilidade funcional.** O plugin não foi carregado pelo AudioFlinger do S23, o acesso do domínio `audioserver` ao estado não foi testado e não houve teste de reprodução. A configuração OEM poderá ser ignorada pelo loader, o efeito poderá permanecer em bypass ou poderá haver falha de áudio. O APK não está incluído e ainda deve ser compilado pelo Mobile Dev Dashboard.

## Conteúdo e comportamento

O arquivo contém `module.prop`, `post-fs-data.sh`, `uninstall.sh`, `bin/sonora-ctl`, `system/vendor/lib64/soundfx/libsonora_spatial.so`, `system/vendor/etc/audio_effects_sec.xml` mesclado e `README.md`. A mesclagem parte do XML Samsung coletado, acrescenta o plugin Sonora apenas ao stream `music` após `sa3d` e preserva as entradas OEM; não copia bibliotecas Dolby/Samsung. A configuração XML está na árvore `system/vendor/etc`, que o `meta-overlayfs` deverá montar systemless. A coleta lista também `/vendor/etc/audio_effects_sec.xml`; ainda não foi confirmado qual caminho o loader efetivamente lê.

O build usou Android NDK r30, target API 24 e Clang 23.1.3; ambos os ELF são ARM64 e referenciam as bibliotecas dinâmicas Android `libc.so`, `libm.so` e `libdl.so`. O runtime C++ é estático; o pacote não depende de `libc++_shared.so`. A entrada AOSP `AELI` está exportada pelo plugin. O efeito inicia em bypass e não separa objetos individuais já mixados por outros apps.

SHA-256 do ZIP:

```text
d50a06f127369bdb0abee348383ef86c660dab782b03176a08631b2f505cbf87  sonora-kernelsu-s23-0.2.0-experimental.zip
```

Confira-o no diretório do projeto com `sha256sum -c dist/sonora-kernelsu-s23-0.2.0-experimental.zip.sha256`.

## Instalação e reversão

1. Confirme no KernelSU-Next que `meta-overlayfs` aparece instalado e ativo e que o modelo/build continuam sendo o SM-S911B alvo.
2. No KernelSU Manager, use `+` para escolher o ZIP experimental e reinicie. Não instale simultaneamente outro módulo que sobreponha `system/vendor/etc/audio_effects_sec.xml`.
3. Instale separadamente o APK quando ele estiver disponível. O DSP começa em bypass; faça qualquer teste em volume baixo e habilite-o somente pelo app.
4. Se ocorrer perda de áudio ou instabilidade, desative o módulo Sonora no KernelSU Manager e reinicie. Para remover, desinstale o módulo e reinicie. Não desinstale o metamodule se outros módulos ainda dependerem dele.

## Rebuild do ZIP

O script reproduzível é `scripts/package-kernelsu-module.sh`. Ele precisa do NDK r30, de um Clang/LLVM compatível com a arquitetura do host e do XML OEM coletado:

```sh
ANDROID_NDK_HOME=/caminho/android-ndk-r30 \
LLVM_HOME=/caminho/llvm-23.1.3 \
SONORA_S23_AUDIO_EFFECTS_XML=/caminho/audio_effects_sec.xml \
scripts/package-kernelsu-module.sh dist/sonora-kernelsu-s23-0.2.0-experimental.zip
```

O empacotador mescla o XML em uma cópia de staging, compila os binários Android, valida ELF/arquitetura/símbolo AOSP, testa o ZIP e grava o checksum `.sha256`. A versão do core KernelSU-Next, a configuração XML efetiva, a política SELinux para `audioserver` e a reprodução real continuam pendentes de verificação no telefone. Consulte também o [guia oficial de módulos KernelSU](https://kernelsu.org/guide/module.html).
