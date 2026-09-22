#!/usr/bin/env bash
# ==============================================================================
# TriAevum - Script Mestre de Build do Android (ARM64)
# Compila o runtime nativo, gera o plugin AOT a partir da ROM, empacota o APK
# completo com as bibliotecas nativas integradas e opcionalmente instala via ADB.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

# Configurações padrão
ROM_PATH=""
BUILD_DIR="${TRIAEVUM_BUILD_DIR:-/home/windroid/triaevum-build}"
TITLE_SOURCES="${TRIAEVUM_TRANSLATED_TITLE_DIR:-$BUILD_DIR/translated-sources}"
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
INSTALL_ADB=0
SKIP_TITLE_GEN=0

usage() {
    cat << EOF
Uso: $0 [opções]

Opções:
  --rom <caminho>          Caminho para a ROM 3DS (.3ds / .cci) (auto-detectado se omitido)
  --build-dir <caminho>    Diretório de build (padrão: $BUILD_DIR)
  --title-sources <dir>    Diretório de fontes C++ traduzidos (padrão: $TITLE_SOURCES)
  --jobs <N>               Número de tarefas de compilação em paralelo (padrão: $JOBS)
  --skip-title-gen         Reutilizar fontes C++ já gerados sem re-extrair a ROM
  --install                Instalar automaticamente no dispositivo via ADB após a compilação
  -h, --help               Exibe esta mensagem de ajuda
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rom)
            ROM_PATH="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --title-sources)
            TITLE_SOURCES="$2"
            shift 2
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --skip-title-gen)
            SKIP_TITLE_GEN=1
            shift
            ;;
        --install)
            INSTALL_ADB=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Opção desconhecida: $1"
            usage
            ;;
    esac
done

echo "======================================================================"
echo "          TriAevum Android ARM64 - Pipeline de Build Mestre           "
echo "======================================================================"

# 1. Localização do Android SDK e NDK
NDK="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-${NDK:-}}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    for candidate in \
        /home/windroid/Android/Sdk/ndk/28.2.13676358 \
        /home/windroid/Android/Sdk/ndk/* \
        ${ANDROID_HOME:-/home/windroid/Android/Sdk}/ndk/* \
        $HOME/Android/Sdk/ndk/*; do
        if [ -f "$candidate/build/cmake/android.toolchain.cmake" ]; then
            NDK="$candidate"
            break
        fi
    done
fi

if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
    echo "ERRO: Android NDK não encontrado! Configure ANDROID_NDK_ROOT ou ANDROID_NDK_HOME."
    exit 1
fi

TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
LLVM_BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
echo "✅ Android NDK localizado em: $NDK"

mkdir -p "$BUILD_DIR"
mkdir -p "$REPO_ROOT/dist"

# 2. Geração dos Shards C++ da ROM (se necessário)
if [ "$SKIP_TITLE_GEN" -eq 0 ]; then
    if [ ! -f "$TITLE_SOURCES/TITLE_SOURCE_MANIFEST.json" ]; then
        echo ""
        echo ">>> [1/5] Gerando fontes C++ AOT a partir da ROM..."
        ROM_ARG=""
        if [ -n "$ROM_PATH" ]; then
            ROM_ARG="--rom $ROM_PATH"
        fi
        python3 "$REPO_ROOT/tools/android/generate_title_from_rom.py" \
            $ROM_ARG \
            --work-dir "$BUILD_DIR/work-ir" \
            --output "$TITLE_SOURCES"
    else
        echo "ℹ️  Fontes C++ já existem em $TITLE_SOURCES (pulando extração. Use sem --skip-title-gen ou apague a pasta para forçar re-extração)."
    fi
else
    echo "ℹ️  Geração de fontes ignorada pelo usuário (--skip-title-gen)."
fi

if [ ! -f "$TITLE_SOURCES/TITLE_SOURCE_MANIFEST.json" ]; then
    echo "ERRO: TITLE_SOURCE_MANIFEST.json não encontrado em $TITLE_SOURCES!"
    exit 1
fi

# 3. Compilação do Runtime Nativo (libTriAevum.so e oot3d_native_game)
echo ""
echo ">>> [2/5] Compilando Runtime Nativo Android (ARM64)..."
cmake -S "$REPO_ROOT/ports/android/runtime" -B "$BUILD_DIR/runtime" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
ninja -C "$BUILD_DIR/runtime" -j "$JOBS" oot3d_native_game

# 4. Compilação da Biblioteca do Título AOT (libtriaevum_title_aot.so)
echo ""
echo ">>> [3/5] Compilando biblioteca compartilhada do jogo (libtriaevum_title_aot.so)..."
cmake -S "$REPO_ROOT/ports/android/native" -B "$BUILD_DIR/native_title" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRIAEVUM_ANDROID_BUILD_NRI=OFF \
  -DTRIAEVUM_TRANSLATED_TITLE_DIR="$TITLE_SOURCES" \
  -DTRIAEVUM_ANDROID_TITLE_OPTIMIZATION=1 \
  -DTRIAEVUM_ANDROID_TITLE_DEBUG_INFO=OFF
ninja -C "$BUILD_DIR/native_title" -j "$JOBS" triaevum_android_title

TITLE_SO="$BUILD_DIR/native_title/libtriaevum_title_aot.so"
if [ ! -f "$TITLE_SO" ]; then
    echo "ERRO: Falha ao compilar $TITLE_SO!"
    exit 1
fi

# Validação estrita da ABI-v2
echo ">>> Validando símbolos da ABI-v2 em libtriaevum_title_aot.so..."
if ! "$LLVM_BIN/llvm-readelf" --dyn-syms "$TITLE_SO" | grep "triaevum_title_whole_aot_query" > /dev/null; then
    echo "ERRO FATAL: O arquivo compilado não exporta triaevum_title_whole_aot_query! ABI-v2 ausente."
    exit 1
fi
echo "✅ Símbolo triaevum_title_whole_aot_query validado com sucesso!"

# 5. Staging e Strip das Bibliotecas Nativas
echo ""
echo ">>> [4/5] Preparando e otimizando bibliotecas nativas (staging)..."
STAGED_DIR="$BUILD_DIR/staged_native_libs"
python3 "$REPO_ROOT/tools/android/stage_native_app.py" \
  --runtime-build "$BUILD_DIR/runtime" \
  --title-module "$TITLE_SO" \
  --ndk "$NDK" \
  --output "$STAGED_DIR"

# 6. Compilação do APK com Gradle
echo ""
echo ">>> [5/5] Compilando APK via Gradle..."
SDL_SRC=""
if [ -d "$BUILD_DIR/runtime/_deps/sdl2-src" ]; then
    SDL_SRC="-PtriaevumSdlSource=$BUILD_DIR/runtime/_deps/sdl2-src"
fi

chmod +x "$REPO_ROOT/ports/android/gradlew"
"$REPO_ROOT/ports/android/gradlew" -p "$REPO_ROOT/ports/android" :app:assembleDebug \
  -PtriaevumNativeStage="$STAGED_DIR" \
  $SDL_SRC \
  --no-daemon \
  --max-workers=2

APK_PATH="$REPO_ROOT/ports/android/app/build/outputs/apk/debug/app-debug.apk"
DIST_APK="$REPO_ROOT/dist/TriAevum-Oot3D-Android-arm64-v8a.apk"

if [ -f "$APK_PATH" ]; then
    cp "$APK_PATH" "$DIST_APK"
    echo ""
    echo "======================================================================"
    echo "🎉 Build concluído com sucesso!"
    echo "APK gerado em: $DIST_APK ($(du -h "$DIST_APK" | cut -f1))"
    echo "======================================================================"
else
    echo "ERRO: APK não encontrado em $APK_PATH!"
    exit 1
fi

# 7. Instalação opcional via ADB
if [ "$INSTALL_ADB" -eq 1 ]; then
    echo ""
    echo ">>> Instalando APK no dispositivo conectado via ADB..."
    adb install -r "$DIST_APK"
    echo "✅ Aplicativo instalado com sucesso no aparelho!"
fi
