# Sonora para KernelSU — integração dependente do firmware

A árvore de fontes do plugin AOSP e do helper privilegiado está em `../native/android/`. Ainda não existe um ZIP instalável: o alvo informado é um Galaxy S23 SM-S911B com KernelSU e One UI 9 Beta, mas a configuração real da build não foi fornecida.

Antes de gerar ou instalar o módulo, é necessário verificar no aparelho o `ro.build.fingerprint`, a ABI, a versão/target SDK, a configuração efetiva de efeitos de áudio, o caminho do `audio_effects*.xml`, a localização permitida para a biblioteca e o contexto SELinux que permita ao `audioserver` ler o bloco de controle compartilhado. Um efeito legado só será usado se a build realmente carregar esse formato de plugin; caso contrário, a integração deve parar, não trocar o XML do fabricante às cegas.

O arquivo de controle é compartilhado por `mmap`. O callback em tempo real apenas lê uma estrutura de 64 bytes protegida por sequence lock; o APK nunca grava áudio nem lê os fluxos de outros apps. O helper e o plugin precisam ser compilados com o mesmo `SONORA_CONTROL_FILE`. Essa constante é intencionalmente obrigatória na compilação Android para impedir que um caminho presumido seja empacotado.

Quando a evidência da build chegar, o pacote será montado como módulo KernelSU separado do APK, com cópia mesclada e reversível da configuração original e scripts de instalação/remoção apropriados. Não instalar no aparelho automaticamente.
