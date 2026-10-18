#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-sonora-s23-target}"
mkdir -p "$OUT"

if ! command -v adb >/dev/null 2>&1; then
  echo "ADB não encontrado. Instale Android platform-tools ou execute os comandos equivalentes por ADB." >&2
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

# Every command sent through `su -c` below only reads information from the device.
adb shell su -c 'getenforce' 2>&1 | tr -d '\r' > "$OUT/selinux-mode.txt" || true
adb shell su -c 'for d in /vendor/lib64/soundfx /odm/lib64/soundfx /system/lib64/soundfx /vendor/lib/soundfx /odm/lib/soundfx /system/lib/soundfx; do if [ -e "$d" ]; then ls -laZ "$d"; fi; done' 2>&1 | tr -d '\r' > "$OUT/soundfx-libraries.txt" || true
adb shell su -c 'for d in /vendor/etc /odm/etc /system/etc /system/vendor/etc; do if [ -d "$d" ]; then find "$d" -maxdepth 1 -type f -name "audio_effects*.xml" -print 2>/dev/null; fi; done' 2>&1 | tr -d '\r' | sort -u > "$OUT/audio-effect-config-paths.txt" || true

index=0
while IFS= read -r device_path; do
  [ -n "$device_path" ] || continue
  index=$((index + 1))
  safe_name="${device_path#/}"
  safe_name="${safe_name//\//_}"
  adb shell su -c "cat '$device_path'" 2>&1 | tr -d '\r' > "$OUT/${index}-${safe_name}" || true
done < "$OUT/audio-effect-config-paths.txt"

adb shell su -c 'for d in /data/vendor/audio /data/vendor /data/misc/audio; do if [ -e "$d" ]; then ls -ldZ "$d"; fi; done' 2>&1 | tr -d '\r' > "$OUT/audio-data-dir-labels.txt" || true
adb shell su -c 'dumpsys media.audio_flinger 2>/dev/null | grep -iE "effect|spatial|postproc" | head -100' 2>&1 | tr -d '\r' > "$OUT/audio-effect-runtime-lines.txt" || true

cat > "$OUT/README.txt" <<'EOF'
Coleta somente de leitura para investigar a compatibilidade do módulo Sonora.
Nenhum arquivo de sistema foi alterado; nenhum áudio foi gravado.
Anexe estes arquivos junto com a versão do KernelSU Manager e o nome/versão do metamodule ativo.
Revise o conteúdo antes de compartilhar; remova qualquer dado que não queira enviar.
EOF

if command -v tar >/dev/null 2>&1; then
  tar -czf "${OUT%/}.tar.gz" "$OUT"
  echo "Coleta concluída: $OUT (${OUT%/}.tar.gz)"
else
  echo "Coleta concluída: $OUT"
fi
