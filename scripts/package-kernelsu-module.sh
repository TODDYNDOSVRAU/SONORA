#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-}"
XML_INPUT="${SONORA_S23_AUDIO_EFFECTS_XML:-}"
OUTPUT="${1:-$ROOT/dist/sonora-kernelsu-s23-0.2.0-experimental.zip}"

if [[ -z "$NDK_HOME" || ! -d "$NDK_HOME/toolchains/llvm/prebuilt" ]]; then
  echo "Set ANDROID_NDK_HOME to an installed Android NDK (r30 was used for this package)." >&2
  exit 2
fi
if [[ -z "$XML_INPUT" || ! -f "$XML_INPUT" ]]; then
  echo "Set SONORA_S23_AUDIO_EFFECTS_XML to the original collected audio_effects_sec.xml." >&2
  exit 2
fi

PREBUILT="${ANDROID_NDK_PREBUILT:-$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64}"
SYSROOT="${ANDROID_NDK_SYSROOT:-$PREBUILT/sysroot}"
if [[ ! -d "$SYSROOT/usr/lib/aarch64-linux-android/24" ]]; then
  echo "NDK sysroot for arm64/API 24 not found: $SYSROOT" >&2
  exit 2
fi

if [[ -n "${LLVM_HOME:-}" && -d "$LLVM_HOME/bin" ]]; then
  PATH="$LLVM_HOME/bin:$PATH"
  export PATH
fi
if [[ "$(uname -m)" == "x86_64" && -d "$PREBUILT/bin" ]]; then
  PATH="$PREBUILT/bin:$PATH"
  export PATH
fi
if [[ -z "${CLANGXX:-}" ]]; then
  if [[ "$(uname -m)" == "x86_64" && -x "$PREBUILT/bin/aarch64-linux-android24-clang++" ]]; then
    CLANGXX="$PREBUILT/bin/aarch64-linux-android24-clang++"
  else
    CLANGXX="$(command -v clang++ || command -v clang++-23 || true)"
  fi
fi
if [[ -z "$CLANGXX" || ! -x "$CLANGXX" ]]; then
  echo "No usable clang++ found. Set CLANGXX to Clang 19+ (NDK r30 libc++ requires it)." >&2
  exit 2
fi
CLANG_MAJOR="$("$CLANGXX" -dumpversion | cut -d. -f1)"
if [[ ! "$CLANG_MAJOR" =~ ^[0-9]+$ || "$CLANG_MAJOR" -lt 19 ]]; then
  echo "Clang 19+ is required by the NDK r30 libc++ headers; found: $CLANG_MAJOR" >&2
  exit 2
fi
if ! command -v ld.lld >/dev/null 2>&1; then
  echo "ld.lld not found on PATH; install LLVM's lld or add its bin directory to PATH." >&2
  exit 2
fi
for tool in zip unzip readelf sha256sum python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing required tool: $tool" >&2; exit 2; }
done

BUILD_DIR="$(mktemp -d)"
STAGE_DIR="$(mktemp -d)"
cleanup() { rm -rf "$BUILD_DIR" "$STAGE_DIR"; }
trap cleanup EXIT
mkdir -p "$(dirname "$OUTPUT")"

NDK_CLANG_RESOURCE="$PREBUILT/lib/clang/21"
if [[ ! -s "$NDK_CLANG_RESOURCE/lib/linux/libclang_rt.builtins-aarch64-android.a" || ! -s "$NDK_CLANG_RESOURCE/lib/linux/aarch64/libunwind.a" ]]; then
  echo "NDK r30 ARM64 compiler runtimes not found under $NDK_CLANG_RESOURCE" >&2
  exit 2
fi
TARGET_ARGS=(--target=aarch64-linux-android24 --sysroot="$SYSROOT" -fuse-ld=lld -stdlib=libc++ -nostdlib++ -rtlib=compiler-rt -std=c++17 -fno-exceptions -fno-rtti -pthread -L"$NDK_CLANG_RESOURCE/lib/linux/aarch64")
if [[ -n "${LLVM_HOME:-}" ]]; then
  LLVM_RESOURCE="$BUILD_DIR/clang-resource"
  LLVM_RESOURCE_VERSION="$CLANG_MAJOR"
  mkdir -p "$LLVM_RESOURCE/lib/aarch64-unknown-linux-android24"
  ln -s "$LLVM_HOME/lib/clang/$LLVM_RESOURCE_VERSION/include" "$LLVM_RESOURCE/include"
  ln -s "$NDK_CLANG_RESOURCE/lib/linux/libclang_rt.builtins-aarch64-android.a" \
    "$LLVM_RESOURCE/lib/aarch64-unknown-linux-android24/libclang_rt.builtins.a"
  TARGET_ARGS+=(-resource-dir="$LLVM_RESOURCE")
fi
CONTROL_DEFINE='-DSONORA_CONTROL_FILE="/data/vendor/audio/sonora_state.bin"'
CXX_LIBS=(-Wl,--start-group "$SYSROOT/usr/lib/aarch64-linux-android/libc++_static.a" "$SYSROOT/usr/lib/aarch64-linux-android/libc++abi.a" "$NDK_CLANG_RESOURCE/lib/linux/aarch64/libunwind.a" -Wl,--end-group)

"$CLANGXX" "${TARGET_ARGS[@]}" -O3 -fPIC -fvisibility=hidden "$CONTROL_DEFINE" \
  -Inative/android/include -Inative/dsp/include -shared \
  native/android/src/sonora_effect.cpp native/dsp/src/sonora_dsp.cpp \
  "${CXX_LIBS[@]}" -Wl,--no-undefined -Wl,-z,relro -Wl,-z,now \
  -Wl,-soname,libsonora_spatial.so -o "$BUILD_DIR/libsonora_spatial.so"

"$CLANGXX" "${TARGET_ARGS[@]}" -O2 -fPIE "$CONTROL_DEFINE" \
  -Inative/android/include -Inative/dsp/include native/android/src/sonora_ctl.cpp \
  "${CXX_LIBS[@]}" -Wl,-z,relro -Wl,-z,now -pie -o "$BUILD_DIR/sonora-ctl"

PLUGIN_HEADER="$(readelf -h "$BUILD_DIR/libsonora_spatial.so")"
HELPER_HEADER="$(readelf -h "$BUILD_DIR/sonora-ctl")"
PLUGIN_SYMBOLS="$(readelf -Ws "$BUILD_DIR/libsonora_spatial.so")"
PLUGIN_DYNAMIC="$(readelf -d "$BUILD_DIR/libsonora_spatial.so")"
HELPER_DYNAMIC="$(readelf -d "$BUILD_DIR/sonora-ctl")"
grep -q 'Machine:.*AArch64' <<< "$PLUGIN_HEADER"
grep -q 'Machine:.*AArch64' <<< "$HELPER_HEADER"
grep -q ' AELI$' <<< "$PLUGIN_SYMBOLS"
grep -q 'Shared library: \[libc.so\]' <<< "$PLUGIN_DYNAMIC"
grep -q 'Shared library: \[libc.so\]' <<< "$HELPER_DYNAMIC"
if grep -q 'libc++_shared' <<< "$PLUGIN_DYNAMIC$HELPER_DYNAMIC"; then
  echo "Unexpected libc++_shared dependency in plugin/helper; this module does not package that runtime." >&2
  exit 1
fi

install -m 0644 "$ROOT/kernelsu/module.prop" "$STAGE_DIR/module.prop"
install -m 0755 "$ROOT/kernelsu/post-fs-data.sh" "$STAGE_DIR/post-fs-data.sh"
install -m 0755 "$ROOT/kernelsu/uninstall.sh" "$STAGE_DIR/uninstall.sh"
install -m 0644 "$ROOT/kernelsu/PACKAGE-README.md" "$STAGE_DIR/README.md"
mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/system/vendor/lib64/soundfx" "$STAGE_DIR/system/vendor/etc"
install -m 0755 "$BUILD_DIR/sonora-ctl" "$STAGE_DIR/bin/sonora-ctl"
install -m 0644 "$BUILD_DIR/libsonora_spatial.so" "$STAGE_DIR/system/vendor/lib64/soundfx/libsonora_spatial.so"
python3 "$ROOT/scripts/merge-s23-audio-effects.py" "$XML_INPUT" "$STAGE_DIR/system/vendor/etc/audio_effects_sec.xml"

{
  printf 'target=Samsung SM-S911B / dm1q; Android 17; ABI arm64-v8a\n'
  printf 'ndk=r30; clang=%s; android_api=24\n' "$("$CLANGXX" --version | sed -n '1p')"
  printf 'control_file=/data/vendor/audio/sonora_state.bin\n'
  printf 'selinux=Enforcing; no policy changes included\n'
  printf 'device_playback_test=not_performed\n'
  printf 'plugin_sha256='; sha256sum "$BUILD_DIR/libsonora_spatial.so" | cut -d' ' -f1
  printf 'helper_sha256='; sha256sum "$BUILD_DIR/sonora-ctl" | cut -d' ' -f1
} > "$STAGE_DIR/build-info.txt"

rm -f "$OUTPUT"
(
  cd "$STAGE_DIR"
  zip -q -X -r -9 "$OUTPUT" .
)
unzip -t "$OUTPUT" >/dev/null
ARCHIVE_LIST="$(unzip -Z -1 "$OUTPUT")"
grep -qx 'module.prop' <<< "$ARCHIVE_LIST"
grep -qx 'system/vendor/etc/audio_effects_sec.xml' <<< "$ARCHIVE_LIST"
grep -qx 'system/vendor/lib64/soundfx/libsonora_spatial.so' <<< "$ARCHIVE_LIST"
sha256sum "$OUTPUT" > "$OUTPUT.sha256"

printf 'ZIP=%s\n' "$OUTPUT"
printf 'SHA256=%s\n' "$(cut -d' ' -f1 "$OUTPUT.sha256")"
printf 'CLANG=%s\n' "$("$CLANGXX" --version | sed -n '1p')"
