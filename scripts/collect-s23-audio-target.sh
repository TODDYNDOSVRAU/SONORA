#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-sonora-s23-target-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

if ! command -v adb >/dev/null 2>&1; then
  echo "ADB não encontrado. Instale Android platform-tools ou execute o coletor PowerShell no Windows." >&2
  exit 2
fi
if [ "$(adb get-state 2>/dev/null || true)" != "device" ]; then
  echo "Nenhum dispositivo ADB autorizado/conectado. Ative Depuração USB e autorize este computador." >&2
  exit 2
fi

props=(
  ro.build.fingerprint
  ro.vendor.build.fingerprint
  ro.product.model
  ro.product.device
  ro.product.cpu.abi
  ro.product.cpu.abilist
  ro.build.version.release
  ro.build.version.sdk
  ro.build.version.incremental
  ro.build.version.security_patch
)
{
  for prop in "${props[@]}"; do
    printf '%s=' "$prop"
    adb shell getprop "$prop" | tr -d '\r'
  done
  printf 'kernel='
  adb shell uname -a | tr -d '\r'
} > "$OUT/build-properties.txt"

# All commands below are read-only and run as the ordinary ADB shell user.
# Pass each compound command as one argument so the device shell parses it intact.
find_cmd="find /vendor/etc /odm/etc /system/etc /system/vendor/etc /product/etc /system/product/etc /system_ext/etc -maxdepth 3 -type f -name 'audio_effects*.xml' -print 2>/dev/null"
adb shell "$find_cmd" 2>&1 | tr -d '\r' | grep -E '^/(vendor|odm|system|product|system_ext)/' | sort -u > "$OUT/audio-effect-config-paths.txt" || true

index=0
while IFS= read -r device_path; do
  [ -n "$device_path" ] || continue
  if [[ ! "$device_path" =~ ^/(vendor|odm|system|product|system_ext)/[A-Za-z0-9._/-]+$ || "$device_path" == *"/../"* ]]; then
    printf 'Caminho ignorado por formato inesperado: %s\n' "$device_path" >> "$OUT/collection-errors.txt"
    continue
  fi
  index=$((index + 1))
  safe_name="${device_path#/}"
  safe_name="${safe_name//\//_}"
  if ! adb pull "$device_path" "$OUT/$(printf '%02d' "$index")-$safe_name" >/dev/null 2>>"$OUT/collection-errors.txt"; then
    printf 'Não foi possível ler %s sem root.\n' "$device_path" >> "$OUT/collection-errors.txt"
  fi
done < "$OUT/audio-effect-config-paths.txt"

if [ "$index" -eq 0 ]; then
  echo "Nenhum arquivo audio_effects*.xml foi listado nos diretórios padrão." > "$OUT/xml-status.txt"
fi

adb shell getenforce 2>&1 | tr -d '\r' > "$OUT/selinux-mode.txt" || true
adb shell "ls -laZ /vendor/lib64/soundfx /odm/lib64/soundfx /system/lib64/soundfx /product/lib64/soundfx /system_ext/lib64/soundfx /vendor/lib/soundfx /odm/lib/soundfx /system/lib/soundfx 2>/dev/null" 2>&1 | tr -d '\r' > "$OUT/soundfx-libraries.txt" || true
adb shell "ls -ldZ /data/vendor/audio /data/vendor /data/misc/audio 2>/dev/null" 2>&1 | tr -d '\r' > "$OUT/audio-data-dir-labels.txt" || true
adb shell "dumpsys media.audio_flinger 2>/dev/null | grep -iE 'effect|spatial|postproc' | head -100" 2>&1 | tr -d '\r' > "$OUT/audio-effect-runtime-lines.txt" || true

cat > "$OUT/README.txt" <<'EOF'
Coleta somente de leitura para investigar a compatibilidade do módulo Sonora.
Não usa su, não altera arquivos do sistema e não grava áudio.
Revise o conteúdo antes de compartilhar; informe separadamente a versão do KernelSU Manager e o nome/versão do metamodule ativo.
EOF

if command -v tar >/dev/null 2>&1; then
  tar -czf "${OUT%/}.tar.gz" "$OUT"
  echo "Coleta concluída: $OUT (${OUT%/}.tar.gz); XMLs encontrados: $index"
else
  echo "Coleta concluída: $OUT; XMLs encontrados: $index"
fi
