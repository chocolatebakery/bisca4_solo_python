#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR_ENV="${BUILD_DIR:-}"
if [[ -n "$BUILD_DIR_ENV" ]]; then
    BUILD_DIR="$BUILD_DIR_ENV"
    if [[ "$BUILD_DIR" != /* ]]; then
        BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
    fi
else
    BUILD_DIR="$ROOT_DIR/build/android"
fi
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-24}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"

if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    echo "ANDROID_NDK_HOME não está definido." >&2
    echo "Defina-o para o diretório do Android NDK antes de correr este script." >&2
    exit 1
fi

cmake -S "$ROOT_DIR" \
      -B "$BUILD_DIR" \
      -G "$GENERATOR" \
      -D CMAKE_BUILD_TYPE=Release \
      -D CMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
      -D ANDROID_ABI="$ANDROID_ABI" \
      -D ANDROID_PLATFORM="$ANDROID_PLATFORM" \
      -D ANDROID_STL=c++_static

cmake --build "$BUILD_DIR" --target bisca4 --config Release
cmake --build "$BUILD_DIR" --target bisca4_mcts --config Release
