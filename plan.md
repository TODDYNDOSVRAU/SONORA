# Plano — Sonora: controle de áudio espacial para Android

## Objetivo e escopo

Criar um APK companheiro Android, integrado ao starter Expo/React Native/TypeScript já inicializado no WebDev, e um módulo separado instalável pelo KernelSU. O conjunto é direcionado ao Samsung Galaxy S23 SM‑S911B, One UI 9 Beta informado pelo usuário. O APK apresentará controles de equalização e cinco modos de posição: centro, frente, atrás, direita e esquerda. O módulo tentará inserir um efeito DSP próprio no caminho global de saída de áudio, para que o processamento alcance o mix estéreo entregue por apps terceiros, condicionado à compatibilidade real da build do aparelho.

O processamento espacial incidirá sobre o mix estéreo já combinado pelo Android como uma única fonte virtual. Não recuperará stems, canais de objetos ou metadados de cada aplicativo; não oferecerá head tracking nem será Dolby Atmos certificado. “Centro” será o modo neutro/centro fantasma; “frente” aplicará uma assinatura frontal; “atrás” aplicará pistas espectrais traseiras; “direita” e “esquerda” usarão pistas binaurais laterais. As diferenças de posição serão implementadas com panorâmica, ITD/ILD, filtros espectrais e ganho, como aproximações genéricas descritas no PDF — não como algoritmo proprietário Dolby.

O APK e o módulo KernelSU são artefatos distintos: o APK não pode instalar ou alterar o módulo sem autorização root. A instalação do módulo e a aprovação de root ficam a cargo do usuário no KernelSU Manager. Nenhum comando será executado no telefone e nenhuma alteração será aplicada ao dispositivo pelo agente.

## Abordagem de implementação

A interface Android será feita na estrutura existente do Expo Router, com componentes React Native e TypeScript. Preferências como posição, ganhos de EQ, preamp e estado pretendido serão locais. O Expo Module local em Kotlin consultará acesso `su`, presença do módulo e se o nome do efeito aparece no AudioFlinger; isso não prova que callbacks estejam processando buffers nem que a alteração seja audível. A enumeração de `AudioManager` não será apresentada como garantia da rota ativa. Para controle, o APK usará apenas `su` com comandos próprios de parâmetros enumerados e limitados, com timeout; não exporá shell arbitrário. O alvo de entrega é Android; as telas iOS não serão tratadas como suportadas.

A engine DSP é dividida em `native/dsp/` (core C++ com EQ biquad, equal-power, pistas ILD/ITD, filtros espectrais e limitador) e `native/android/` (plugin legado Audio Effect C ABI AOSP e `sonora-ctl`). O bloco de 64 bytes em `mmap` usa palavras de 32 bits lidas/escritas por builtins atômicos e um número de geração, para que o protocolo não dependa de acesso concorrente não atômico aos floats. O callback não abre arquivos, não bloqueia nem aloca. As configurações de DSP/áudio usam dois slots de runtime com contador de leitores por slot; comandos atualizam o slot inativo e o publicam atomicamente, e `RESET` é solicitado para o callback em vez de mutar o DSP em uso. Configurações não suportadas são rejeitadas em `SET_CONFIG`, sem alegar pass-through seguro de formatos/layouts desconhecidos. O helper valida arquivo regular, tamanho, owner/grupo e link count, usando `O_NOFOLLOW`; no Android, abre a cadeia fixa `/data/vendor/audio` por diretórios e não modifica metadata de um arquivo preexistente. O esqueleto KernelSU chama `init` em `post-fs-data` e o plugin tenta remapear em `SET_CONFIG`/`ENABLE` se o arquivo ainda não existia. Os testes host cobrem core, helper, flags AOSP e remapeamento tardio. O helper e a branch Android do plugin compilam no host com `__ANDROID__`, mas nenhum deles foi compilado com o NDK nem carregado num telefone. O status da interface usa os termos “efeito listado” e “ativação solicitada”; não declara processamento real até validação de reprodução no aparelho. O APK não chama construtores genéricos ocultos de `android.media.audiofx.AudioEffect`; o Expo Module Kotlin pede root sob toque explícito e encaminha parâmetros limitados ao helper em `MODULE_DIR/bin/sonora-ctl`. A biblioteca será registrada na chain de mídia `music` se a configuração OEM efetiva, montagem KernelSU e política SELinux permitirem; o acesso do `audioserver` ainda precisa ser validado.

O primeiro 7z não continha XMLs, mas o TAR posterior do Termux os fornece; os 31 XMLs extraídos passaram no parser XML. O aparelho reporta SM-S911B / `dm1q`, ABI `arm64-v8a`, Android 17 / SDK 37, build `S911BXXUAZZHL`, fingerprint `samsung/dm1qxxx/dm1q:17/CP2A.260605.016/S911BXXUAZZHL:user/release-keys`, patch `2026-08-05`, kernel `5.15.207-g35b4d63d` e SELinux `Enforcing`. O XML Samsung `/vendor/etc/audio_effects_sec.xml` declara `dap_proxy` e usa `sa3d` nos streams music/ring/alarm; `/vendor/etc/audio_effects_spatializer.xml` registra `libswspatializer.so` (UUID `ccd4cf09-a79d-46c2-9aae-06a1698d6c8f`). O dump do AudioFlinger mostra uma chain de sessão 0 com dois efeitos e o rótulo `Dolby_Spatializer_FX`, mas isso não comprova que um plugin Sonora seria carregado, nem que o efeito OEM possa ser controlado por API pública. `scripts/merge-s23-audio-effects.py` foi testado contra o XML real: acrescenta library/effect/`apply` apenas ao stream `music` após `sa3d`, preservando o texto original fora das três inserções e sem sobrescrever a entrada. Não reutilizar/redistribuir bibliotecas OEM; preservar a cadeia e adicionar apenas o DSP próprio se a integração for confirmada.

O usuário confirmou `uid=0(root)` em Termux, SELinux `Enforcing` e `/data/vendor/audio` como `audio:audio`, modo `0770`, contexto `u:object_r:vendor_audio_data_file:s0`; `/data/vendor` usa `vendor_data_file` e `/data/misc/audio` não existe. A coleta posterior listou `/data/adb/modules/playintegrityfix`, `zygisk_vector` e `zygisksu`; esses metadados identificam módulos/ferramentas relacionados a Play Integrity e Zygisk, mas não confirmam um metamodule de montagem. Não inferir que Vector seja metamodule. A candidata para o estado é `/data/vendor/audio/sonora_state.bin`; o helper cria apenas arquivo novo como `root:audio`/`0640` e rejeita symlink, hardlink e objeto preexistente com owner/tamanho/modo incorreto. O plugin abre em leitura apenas e valida metadados antes de mapear. O esqueleto do módulo chama `sonora-ctl init` em `post-fs-data.sh` e o plugin tenta remapear em `SET_CONFIG`/`ENABLE`. Isso ainda não prova que SELinux permite ao domínio `audioserver` abrir/mapear o arquivo. As versões do KernelSU core e Manager, o nome/versão de um metamodule ativo (ou confirmação de que não há), o XML efetivo selecionado e a validação de reprodução real continuam pendentes; não empacotar módulo instalável antes dessas confirmações.

A rota Bluetooth continuará sob controle do Android. O aplicativo mostrará os dispositivos de saída que o sistema detectar, mas não prometerá força de roteamento nem confundirá dispositivos disponíveis com a rota ativa. O efeito é destinado a acompanhar o mix de saída onde o firmware o carregar; compatibilidade por saída e por aplicativo terá de ser aferida no aparelho físico.

O SDK/renderer Dolby não será integrado nesta etapa: o escopo usa DSP genérico e não reivindica certificação, marca ou equivalência certificada Dolby. O PDF será a referência conceitual para tempo/ganho/panorâmica/HRTF e para deixar explícita a diferença entre espacialização genérica e Atmos.

Na ABI legada AOSP, `buffer_config_t` segue a sequência canônica `buffer`, taxa de amostragem, canais, formato, modo de acesso, máscara e `bufferProvider`. Asserções de compilação para ARM64 fixam os offsets 24/25/26/32 e os tamanhos de 56 bytes (`buffer_config_t`) e 112 bytes (`effect_config_t`); o harness host envia `EFFECT_CMD_SET_CONFIG` usando esse layout.

## Dependências e limites materiais

- React Native/Expo é mantido conforme o starter fixo WebDev; código Kotlin será isolado em módulo Expo Android local e o core/plugin C++ em `native/`.
- A implementação do APK exige build nativo que contenha o módulo local; Expo Go não carrega código nativo personalizado. O build final será solicitado pelo fluxo de build Mobile do Dashboard WebDev, não compilado como release dentro do Sandbox.
- O módulo exige root ativo no KernelSU, aprovação do usuário, suporte de montagem de módulos, ABI e configuração de áudio compatíveis com a imagem do telefone. Atualizações de One UI/Android podem invalidar a sobreposição ou os símbolos do driver.
- Sem acesso ao dispositivo físico, não é possível afirmar compatibilidade funcional do efeito global nem medir latência/qualidade. O projeto será marcado como alvo experimental para esta build, até validação real.
- A modificação de áudio global via plugin de sistema pode causar bootloop, falha do AudioFlinger ou perda de áudio se o pacote não corresponder ao firmware. O módulo deverá incluir caminho de desativação/remoção no KernelSU e validação estática de pré-requisitos antes da instalação; não deve sobrescrever a configuração do fornecedor sem preservar seus efeitos originais.
- Não serão coletadas gravações nem áudio do usuário em rede. O processamento será no dispositivo.

## Estrutura prevista

- `app/(tabs)/index.tsx`: painel principal, estado do motor, cinco modos de posição, equalizador, preamp, bypass e indicadores de rota/compatibilidade.
- `components/audio/`: seletor de posição, sliders de banda, estado/permissão e mensagens de compatibilidade.
- `lib/audio/`: tipos de parâmetros, presets, armazenamento local e interface tipada com o módulo nativo.
- `modules/sonora-audio/`: módulo Expo Android para root/status/controle DSP e detecção de rota; nenhuma implementação iOS será anunciada como funcional.
- `native/dsp/`: DSP estéreo, filtros, pistas aproximadas e protocolo de estado compartilhado.
- `native/android/`: ABI AOSP mínima, plugin C++ legado, helper root e configuração NDK arm64; o caminho de controle é obrigatório por build.
- `kernelsu/README.md`: dependências de build, SELinux e validação; o ZIP do módulo não será gerado sem XMLs e dados KernelSU/metamodule.
- `scripts/`: testes host, coletor shell e coletor PowerShell somente de leitura para propriedades/configurações do S23.
- `assets/images/`: identidade visual e ícone nativo Sonora.
- `plan.md`: este plano e decisões de design. `TODO.md`: critérios de aceite e progresso; mantém APK e ZIP abertos até a compilação/validação pendentes.

## Design

Alternativas de estilo consideradas (probabilidades pequenas de referência): painel de áudio técnico minimalista (0,06); equipamento hi‑fi analógico noturno (0,04); cinema futurista azul-neon (0,03). Estilo escolhido: painel de áudio técnico minimalista.

**Movimento de design:** interface de um medidor de áudio de estúdio moderno, com informação legível e controles precisos em retrato.

**Princípios centrais:** feedback claro de compatibilidade; controles diretos e reversíveis; prevenção de clipping e de falsas alegações; hierarquia que privilegia a posição e o estado do efeito.

**Filosofia de cor:** fundo grafite para reduzir distração; ciano elétrico destaca controles e estados detectados (não prova que há processamento ativo); âmbar/vermelho reservados a avisos de root, incompatibilidade ou clipping.

**Paradigma de layout:** fluxo vertical de sinal — estado do sistema, posição virtual, equalização e saída — em vez de uma grade de cartões. Um arco direcional acompanha o seletor de posição e mostra a fonte virtual.

**Elementos de assinatura:** mapa circular de cinco pontos com um marcador móvel; linha de sinal contínua entre o status e a posição; leitura numérica compacta de ganho/frequência.

**Interação:** controles grandes para uso com uma mão; tocar uma posição aplica-a imediatamente; bypass e restauração ficam visíveis; estado “não suportado” nunca se disfarça de ativo.

**Animação:** transição curta do marcador entre posições e atenuação visual suave nos medidores; sem animação contínua pesada quando o app está em segundo plano.

**Tipografia:** sans-serif de sistema Android para rótulos e títulos; monoespaçada de sistema para dB, Hz e valores de parâmetro.

**Essência da marca:** ferramenta de controle espacial do mix Android para quem quer experimentar localização e EQ global no próprio aparelho; técnica, clara e cautelosa.

**Tom de voz:** curto e transparente. Exemplos: “Ativação solicitada; áudio não confirmado” e “O nome do efeito não apareceu no AudioFlinger nesta inicialização.”

**Wordmark e símbolo:** “Sonora” em caixa mista com um arco circular aberto e cinco pontos de posição, sem usar logotipos ou símbolos Dolby.

**Cor de assinatura:** ciano elétrico sobre grafite.
