#!/usr/bin/env bash
# Build LuaJIT static libraries for iOS and Android.
#
# Usage:
#   bash tools/build_luajit.sh ios          # Build for iOS (arm64, interpreter-only)
#   bash tools/build_luajit.sh android      # Build for Android (arm64-v8a + x86_64)
#   bash tools/build_luajit.sh all          # Build for both
#
# Prerequisites:
#   - LuaJIT source checked out (default: ~/dev/src/luajit)
#     Override with LUAJIT_SRC=/path/to/luajit
#   - iOS: Xcode with command-line tools
#   - Android: Android NDK (set ANDROID_NDK_HOME or use default SDK location)
#
# Output directories (relative to repo root):
#   iOS:     build_ios/_deps/luajit-ios/       (libluajit.a + headers)
#   Android: android/app/src/main/jni/luajit/  (arm64-v8a/ and x86_64/ subdirs + headers)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUAJIT_SRC="${LUAJIT_SRC:-$HOME/dev/src/luajit}"

if [ ! -f "$LUAJIT_SRC/src/lua.h" ]; then
    echo "ERROR: LuaJIT source not found at $LUAJIT_SRC"
    echo "Clone it:  git clone https://github.com/LuaJIT/LuaJIT.git $LUAJIT_SRC"
    echo "Or set:    LUAJIT_SRC=/path/to/luajit bash tools/build_luajit.sh <target>"
    exit 1
fi

# Core headers needed by the game code
HEADERS=(lua.h lauxlib.h lualib.h luaconf.h)

copy_headers() {
    local dest="$1"
    mkdir -p "$dest"
    for h in "${HEADERS[@]}"; do
        cp "$LUAJIT_SRC/src/$h" "$dest/"
    done
    # Copy version header (luajit.h or luajit_rolling.h depending on release)
    if [ -f "$LUAJIT_SRC/src/luajit.h" ]; then
        cp "$LUAJIT_SRC/src/luajit.h" "$dest/"
    elif [ -f "$LUAJIT_SRC/src/luajit_rolling.h" ]; then
        cp "$LUAJIT_SRC/src/luajit_rolling.h" "$dest/"
        # Create luajit.h alias so existing includes work
        cp "$LUAJIT_SRC/src/luajit_rolling.h" "$dest/luajit.h"
    fi
}

build_ios() {
    echo "=== Building LuaJIT for iOS ==="

    # iOS requires interpreter-only (no JIT) — Apple doesn't allow JIT on iOS.
    # TARGET_SYS=iOS disables the JIT engine.

    # --- Device (arm64-iphoneos) ---
    echo "--- Device (arm64) ---"
    local OUT_DEVICE="$REPO_ROOT/build_ios/_deps/luajit-ios"
    mkdir -p "$OUT_DEVICE"

    cd "$LUAJIT_SRC"
    MACOSX_DEPLOYMENT_TARGET=11.0 make clean 2>/dev/null || true

    local IOS_SDK
    IOS_SDK="$(xcrun --sdk iphoneos --show-sdk-path)"

    make -j"$(sysctl -n hw.ncpu)" \
        TARGET_SYS=iOS \
        CROSS="xcrun -sdk iphoneos " \
        TARGET_FLAGS="-arch arm64 -isysroot $IOS_SDK -miphoneos-version-min=14.0" \
        BUILDMODE=static

    cp src/libluajit.a "$OUT_DEVICE/libluajit.a"
    copy_headers "$OUT_DEVICE"
    MACOSX_DEPLOYMENT_TARGET=11.0 make clean

    # --- Simulator (arm64-iphonesimulator) ---
    echo "--- Simulator (arm64) ---"
    local OUT_SIM="$REPO_ROOT/build_ios/_deps/luajit-ios-sim"
    mkdir -p "$OUT_SIM"

    local IOS_SIM_SDK
    IOS_SIM_SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"

    make -j"$(sysctl -n hw.ncpu)" \
        TARGET_SYS=iOS \
        CROSS="xcrun -sdk iphonesimulator " \
        TARGET_FLAGS="-arch arm64 -isysroot $IOS_SIM_SDK -miphonesimulator-version-min=17.0" \
        BUILDMODE=static

    cp src/libluajit.a "$OUT_SIM/libluajit.a"
    MACOSX_DEPLOYMENT_TARGET=11.0 make clean

    echo "=== iOS LuaJIT built ==="
    echo "  Device:    $OUT_DEVICE/libluajit.a"
    echo "  Simulator: $OUT_SIM/libluajit.a"
}

build_android() {
    echo "=== Building LuaJIT for Android (arm64-v8a + x86_64) ==="

    # Find NDK
    local NDK="${ANDROID_NDK_HOME:-}"
    if [ -z "$NDK" ]; then
        # Try default SDK location
        local SDK="${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}"
        # Pick the latest NDK version installed
        if [ -d "$SDK/ndk" ]; then
            NDK="$(ls -d "$SDK/ndk/"* 2>/dev/null | sort -V | tail -1)"
        fi
    fi
    if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
        echo "ERROR: Android NDK not found."
        echo "Set ANDROID_NDK_HOME=/path/to/ndk or install via Android Studio SDK Manager."
        exit 1
    fi
    echo "Using NDK: $NDK"

    local OUT_BASE="$REPO_ROOT/android/app/src/main/jni/luajit"
    local HOST_OS
    case "$(uname -s)" in
        Darwin) HOST_OS="darwin" ;;
        Linux)  HOST_OS="linux" ;;
        *)      echo "ERROR: Unsupported host OS"; exit 1 ;;
    esac
    local TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/${HOST_OS}-x86_64"

    # --- arm64-v8a ---
    echo "--- arm64-v8a ---"
    cd "$LUAJIT_SRC"
    MACOSX_DEPLOYMENT_TARGET=11.0 make clean 2>/dev/null || true

    local API=26
    make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
        HOST_CC="gcc" \
        CROSS="${TOOLCHAIN}/bin/aarch64-linux-android${API}-" \
        STATIC_CC="${TOOLCHAIN}/bin/aarch64-linux-android${API}-clang" \
        DYNAMIC_CC="${TOOLCHAIN}/bin/aarch64-linux-android${API}-clang -fPIC" \
        TARGET_LD="${TOOLCHAIN}/bin/aarch64-linux-android${API}-clang" \
        TARGET_AR="${TOOLCHAIN}/bin/llvm-ar rcus" \
        TARGET_STRIP="${TOOLCHAIN}/bin/llvm-strip" \
        TARGET_SYS=Linux \
        TARGET_FLAGS="-fPIC" \
        BUILDMODE=static \
        XCFLAGS="-DLUAJIT_DISABLE_JIT"

    mkdir -p "$OUT_BASE/arm64-v8a"
    cp src/libluajit.a "$OUT_BASE/arm64-v8a/libluajit.a"
    MACOSX_DEPLOYMENT_TARGET=11.0 make clean

    # --- x86_64 ---
    echo "--- x86_64 ---"
    cd "$LUAJIT_SRC"

    make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
        HOST_CC="gcc" \
        CROSS="${TOOLCHAIN}/bin/x86_64-linux-android${API}-" \
        STATIC_CC="${TOOLCHAIN}/bin/x86_64-linux-android${API}-clang" \
        DYNAMIC_CC="${TOOLCHAIN}/bin/x86_64-linux-android${API}-clang -fPIC" \
        TARGET_LD="${TOOLCHAIN}/bin/x86_64-linux-android${API}-clang" \
        TARGET_AR="${TOOLCHAIN}/bin/llvm-ar rcus" \
        TARGET_STRIP="${TOOLCHAIN}/bin/llvm-strip" \
        TARGET_SYS=Linux \
        TARGET_FLAGS="-fPIC" \
        BUILDMODE=static \
        XCFLAGS="-DLUAJIT_DISABLE_JIT"

    mkdir -p "$OUT_BASE/x86_64"
    cp src/libluajit.a "$OUT_BASE/x86_64/libluajit.a"
    MACOSX_DEPLOYMENT_TARGET=11.0 make clean

    # Headers (shared by all ABIs)
    copy_headers "$OUT_BASE/include"

    echo "=== Android LuaJIT built ==="
    echo "  arm64-v8a: $OUT_BASE/arm64-v8a/libluajit.a"
    echo "  x86_64:    $OUT_BASE/x86_64/libluajit.a"
    echo "  headers:   $OUT_BASE/include/"
}

# ── Main ──────────────────────────────────────────────────────────────────────
TARGET="${1:-}"
case "$TARGET" in
    ios)     build_ios ;;
    android) build_android ;;
    all)     build_ios; build_android ;;
    *)
        echo "Usage: $0 {ios|android|all}"
        echo ""
        echo "Environment variables:"
        echo "  LUAJIT_SRC    Path to LuaJIT source (default: ~/dev/src/luajit)"
        echo "  ANDROID_NDK_HOME  Path to Android NDK (auto-detected from SDK)"
        exit 1
        ;;
esac
