#!/usr/bin/env bash
# Sync game assets from the repository to iOS and Android asset directories.
#
# Usage:
#   bash tools/sync_assets.sh ios       # Sync to iOS assets
#   bash tools/sync_assets.sh android   # Sync to Android assets
#   bash tools/sync_assets.sh all       # Sync to both
#
# This copies: textures, language, config, models, sound, music, moviepacks,
# cockpits, and resource data. Shaders are NOT synced here — use
# tools/compile_shaders.sh for that.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IOS_ASSETS="$REPO_ROOT/ios/app/assets"
ANDROID_ASSETS="$REPO_ROOT/android/app/src/main/assets"

sync_common_assets() {
    local DEST="$1"
    echo "Syncing assets to $DEST ..."

    # Game data directories (rsync for incremental copy, --delete to remove stale files)
    for dir in textures language config models sound music; do
        if [ -d "$REPO_ROOT/$dir" ]; then
            echo "  $dir/"
            rsync -a --delete "$REPO_ROOT/$dir/" "$DEST/$dir/"
        fi
    done

    # Moviepacks
    echo "  moviepacks/"
    mkdir -p "$DEST/moviepacks"
    rsync -a --delete "$REPO_ROOT/moviepacks/" "$DEST/moviepacks/"

    # Cockpits
    echo "  cockpits/"
    mkdir -p "$DEST/cockpits"
    rsync -a --delete "$REPO_ROOT/cockpits/" "$DEST/cockpits/"

    # Resource data (proto → included, binary → included)
    echo "  resource/"
    mkdir -p "$DEST/resource/included"
    # Copy binary resources
    if [ -d "$REPO_ROOT/resource/binary" ]; then
        rsync -a "$REPO_ROOT/resource/binary/" "$DEST/resource/included/"
    fi
    # Generate included resources from proto/ if Python3 is available
    if command -v python3 &>/dev/null; then
        python3 "$REPO_ROOT/batch/make/copyresources.py" \
            "$REPO_ROOT/resource/proto" "$DEST/resource/included" 2>/dev/null || \
            echo "  WARNING: copyresources.py failed — included resources may be incomplete"
    else
        echo "  WARNING: python3 not found — skipping resource/proto generation"
    fi
}

sync_ios() {
    echo "=== Syncing iOS assets ==="
    sync_common_assets "$IOS_ASSETS"
    echo "=== iOS assets synced ==="
}

sync_android() {
    echo "=== Syncing Android assets ==="
    sync_common_assets "$ANDROID_ASSETS"
    echo "=== Android assets synced ==="
}

TARGET="${1:-}"
case "$TARGET" in
    ios)     sync_ios ;;
    android) sync_android ;;
    all)     sync_ios; sync_android ;;
    *)
        echo "Usage: $0 {ios|android|all}"
        exit 1
        ;;
esac
