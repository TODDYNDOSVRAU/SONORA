# TODO — Sonora

> Progresso parcial: painel, persistência local, bridge Kotlin e DSP genérico existem no código; TypeScript, Vitest e testes host do DSP/plugin passaram. A tela distingue ativação solicitada de nome do efeito listado e não afirma processamento confirmado. Ainda não foi criado/compilado um APK Android nem um ZIP KernelSU instalável, e a integração não foi validada no Galaxy S23; por isso os resultados finais abaixo permanecem abertos.

## APK Android companheiro: controles funcionais de áudio
- [ ] Criar um APK Android próprio, com interface simples e funcional para controlar o processamento de áudio ao vivo do telefone e testar a reprodução em Bluetooth, alto-falantes e fones.
- [ ] O aplicativo não será uma demo: controles e estados devem ligar-se ao módulo de áudio quando ele estiver disponível; a interface deve distinguir ativação solicitada, bypass solicitado, root ausente e nome do efeito não listado. Não declarar processamento real como ativo apenas por encontrar o nome do efeito no AudioFlinger; essa confirmação depende de teste do áudio no aparelho.
- [ ] Incluir controles de equalização com frequências/ganhos e proteção contra clipping, usando conhecimentos gerais de equalização e controle de áudio, e guardar as configurações localmente.
- [ ] Incluir seleção de posição do áudio com os modos Centro, Frente, Atrás, Direita e Esquerda; a mudança deve aplicar-se ao mix de saída como uma única fonte virtual, e não afirmar que separa objetos de áudio já misturados por outros aplicativos.
- [ ] Permitir experimentar os modos sobre áudio de outros apps no fluxo global onde o módulo for compatível, à maneira de um equalizador de sistema como Poweramp Equalizer; mostrar a rota de saída identificada sem alegar que o app força a rota Bluetooth.

## Engine DSP global e integração KernelSU para o aparelho-alvo
- [ ] O usuário escolheu a solução root + APK para Android e especificou Samsung Galaxy S23 SM-S911B com root KernelSU na One UI 9 Beta.
- [ ] Criar um módulo KernelSU separado do APK para inserir um DSP próprio no caminho global de saída, com processamento estéreo espacial genérico, controles de posição e equalização; usar o PDF técnico enviado como base conceitual e não afirmar certificação ou processamento Dolby Atmos.
- [ ] Implementar as posições Centro (neutro/centro fantasma), Frente, Atrás, Direita e Esquerda usando controles/coeficientes genéricos de panorâmica, diferenças de tempo e nível entre canais, filtros espectrais e ganho; aplicar ao mix final como uma fonte virtual.
- [ ] Suavizar alterações de parâmetros e limitar ganho para reduzir cliques e clipping; processar localmente, sem enviar ou guardar gravações do áudio dos apps.

## Compatibilidade, segurança e entrega do APK/módulo
- [ ] Direcionar o módulo ao fingerprint e à configuração real da build One UI 9 Beta do SM-S911B, respeitando a ABI, os arquivos `audio_effects*.xml`, as bibliotecas de efeitos, as regras de segurança e a infraestrutura de montagem KernelSU/metamodule disponível.
- [ ] Preservar a configuração de efeitos do fabricante; não substituir arquivos originais às cegas. Incluir instruções de bypass/remoção pelo KernelSU e pré-requisitos explícitos para reduzir risco de bootloop ou falha do áudio.
- [ ] Manter o APK e o módulo KernelSU como artefatos distintos; o usuário concederá root e instalará o módulo pelo KernelSU Manager. Não executar instalação, comandos ou alterações no telefone.
- [ ] Só anunciar compatibilidade real com o Galaxy S23/One UI 9 Beta depois de validar no aparelho; o APK Android final deve ser construído pelo fluxo Mobile do Dashboard WebDev, não por build de release no Sandbox.
