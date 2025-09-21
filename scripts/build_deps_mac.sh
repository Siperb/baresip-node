#!/usr/bin/env bash
set -euo pipefail

# Static, no-TLS build of: Opus, libsrtp2, libre (re), librem (rem), and baresip
# Targets macOS ARM64 and installs staged files under ./local/*

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="$ROOT/local"
ARCH="arm64"
SDK_MIN="15.0"
CC="gcc-14"
CMAKE_GENERATOR="Ninja"
OPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl@3"
BUILD_TYPE="Release"

export OPENSSL_INCLUDE_DIR="/opt/homebrew/opt/openssl@3/include"
export MACOSX_DEPLOYMENT_TARGET="10.12"
export CC="clang"

mkdir -p "$PREFIX"

CMAKE_COMMON="-DCMAKE_BUILD_TYPE=Release \
 -DBUILD_SHARED_LIBS=OFF \
 -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
 -DCMAKE_OSX_ARCHITECTURES=${ARCH} \
 -DCMAKE_OSX_DEPLOYMENT_TARGET=${SDK_MIN}"

# Dependency build order:
echo "==> Install homebrew packages"
brew install codec2 aom fdk-aac ffmpeg@7 jack mpg123 spandsp sdl2 glib-utils pkg-config

# --- 4) libre / re (static-only) --------------------------------
echo "==> libre / re (static-only)"
cd "$ROOT/deps/re"; rm -rf build
# Configure with OpenSSL support, but build only static lib
cmake -B build \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DCMAKE_C_FLAGS="-Werror" \
  -DCMAKE_CXX_FLAGS="-Werror"
# Build just the static archive
cmake --build build -t retest
# Run tests
./build/test/retest -r -v
# Stage headers + static lib into local/
mkdir -p "$PREFIX/re/lib" "$PREFIX/re/include"
cp -a build/libre.a "$PREFIX/re/lib/"
cp -a include/*   "$PREFIX/re/include/"

# --- 6) baresip -----
echo "==> baresip"
cd "$ROOT/deps/baresip"; rm -rf build
cmake -B build \
  -DSTATIC=ON \
  -DCMAKE_C_FLAGS="-Werror"
cmake --build build -j
mkdir -p "$PREFIX/baresip/lib" "$PREFIX/baresip/include"
cp -a build/libbaresip.a "$PREFIX/baresip/lib/"
cp -a include/*         "$PREFIX/baresip/include/"

# Done
echo "==> Done"

#Linux: same idea; swap CoreAudio module for pulse or alsa, ensure libpulse-dev/libasound2-dev are present, and pass -DMODULES="...;pulse" (or alsa).
# Windows (MSVC): build OpenSSL static (perl Configure VC-WIN64-ARM no-shared or VC-WIN64A for x64), then re/rem/libsrtp/opus/baresip with -DBUILD_SHARED_LIBS=OFF and -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded. Use wasapi module.