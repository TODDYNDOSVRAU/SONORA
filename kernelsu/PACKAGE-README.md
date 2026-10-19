# Sonora Spatial Audio — pacote KernelSU experimental

**Alvo:** Samsung Galaxy S23 SM-S911B (`dm1q`), Android 17 / API 37, ABI `arm64-v8a`, KernelSU-Next com metamodule `meta-overlayfs`. Pacote experimental feito a partir dos arquivos coletados do aparelho. Não é um produto validado pela Samsung e não é certificado pela Dolby.

## O que este módulo instala

O overlay contém uma cópia mesclada de `audio_effects_sec.xml` sob `system/vendor/etc/` e o plugin `libsonora_spatial.so` em `system/vendor/lib64/soundfx/`. A entrada Sonora foi acrescentada somente ao stream `music`, depois de `sa3d`; a configuração Samsung original coletada foi preservada no XML mesclado. O hook `post-fs-data.sh` inicializa `/data/vendor/audio/sonora_state.bin` como estado compartilhado. O APK Sonora é separado e não está incluído neste ZIP.

## Instalação

1. Confira se o `meta-overlayfs` aparece instalado/ativo no KernelSU-Next e se o telefone é o SM-S911B alvo.
2. No KernelSU Manager, instale este ZIP pela opção `+` e reinicie o telefone.
3. Instale separadamente o APK Sonora. Ao abrir o app, conceda root somente ao pedido explícito do Sonora. O efeito começa em **bypass**; habilite-o pelo app apenas durante o teste.
4. Faça o primeiro teste com volume baixo. O nome “efeito listado” no AudioFlinger, se aparecer, não confirma sozinho que o DSP esteja processando áudio audível.

## Limites e resolução de problemas

O plugin foi compilado para `arm64-v8a`, API mínima 24, usando o Android NDK r30. O pacote ainda não foi carregado no aparelho nem validado com o domínio SELinux `audioserver`; a integração pode permanecer em bypass se a política SELinux impedir o acesso ao bloco de controle ou se o AudioFlinger não carregar a configuração alterada. Esse efeito afeta somente a chain `music` em que o loader OEM o aplicar. Não separa objetos individuais já mixados por outros apps e não força a rota Bluetooth.

O overlay substitui a visão systemless de `system/vendor/etc/audio_effects_sec.xml` pelo XML coletado e mesclado. Não instale outro módulo que altere esse mesmo arquivo ao mesmo tempo. O original do sistema não é sobrescrito diretamente.

Para reverter, desative ou desinstale **Sonora Spatial Audio** no KernelSU Manager e reinicie. Não remova o metamodule se quiser continuar usando outros módulos que dependem da montagem systemless. Se houver falha de áudio, desative este módulo e reinicie; o estado inicial do DSP é bypass.

**Build do pacote:** NDK r30, Clang 23.1.3, `arm64-v8a`, target Android API 24. O módulo é independente do APK.
