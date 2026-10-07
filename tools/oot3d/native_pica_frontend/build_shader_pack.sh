#!/usr/bin/env bash
# ==============================================================================
# Script de Compilação Offline de Shaders PICA para SPIR-V AOT Pack (.o3ps)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"

PROFILES_DIR="$REPO_ROOT/tools/oot3d/native_pica_frontend/profiles"
ASSETS_GAME_DIR="$REPO_ROOT/ports/android/app/src/main/assets/game"
OUTPUT_PACK="$ASSETS_GAME_DIR/oot3d_pica_default.o3ps"
OUTPUT_MANIFEST="$ASSETS_GAME_DIR/oot3d_pica_default_manifest.json"

if [ -f "$OUTPUT_PACK" ] && [ -f "$OUTPUT_MANIFEST" ] && [ "${FORCE_REBUILD_SHADERS:-0}" -eq 0 ]; then
    echo "ℹ️  Pacote AOT de shaders já existe: $(du -h "$OUTPUT_PACK" | cut -f1) ($OUTPUT_PACK) - reutilizando."
    exit 0
fi

COMPILER_BIN="/tmp/oot3d_native_pica_aot_compiler_glslang"

echo ">>> Compilando/Verificando ferramenta de compilação AOT de shaders..."
if [ ! -f "$COMPILER_BIN" ]; then
    g++ -std=c++20 -O2 \
      -I"$REPO_ROOT/runtime/three_ds_recomp/include" \
      -I"/home/windroid/triaevum-build/runtime/_deps/nlohmann_json-src/include" \
      "$REPO_ROOT/tools/oot3d/native_pica_frontend/oot3d_native_pica_aot_compiler_glslang.cpp" \
      /tmp/pica_aot_shader_pack.o \
      /tmp/pica_scanout_effects.o \
      "$REPO_ROOT/toon_outline_shader.o" \
      "$REPO_ROOT/spatial_aa.o" \
      "$REPO_ROOT/reflection_debug.o" \
      "$REPO_ROOT/ambient_occlusion_composite.o" \
      -o "$COMPILER_BIN"
fi

echo ">>> Compilando shaders offline para $OUTPUT_PACK..."
mkdir -p "$ASSETS_GAME_DIR"
"$COMPILER_BIN" \
  --inventory "$PROFILES_DIR/oot3d_pica_boot_title_kokiri_pipeline.json" \
  --inventory "$PROFILES_DIR/oot3d_pica_boot_title_kokiri_effective.json" \
  --pack "$OUTPUT_PACK" \
  --manifest "$OUTPUT_MANIFEST" \
  --threads "$(nproc 2>/dev/null || echo 4)"

echo "✅ Shaders offline compilados com sucesso: $(du -h "$OUTPUT_PACK" | cut -f1) ($OUTPUT_PACK)"
