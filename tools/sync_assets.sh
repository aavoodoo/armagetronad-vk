#!/usr/bin/env bash
# Sync game assets from the repository to iOS and Android asset directories.
#
# Usage:
#   bash tools/sync_assets.sh ios               # Sync all assets to ios/app/assets/
#   bash tools/sync_assets.sh android           # Sync all assets to Android (+ manifest)
#   bash tools/sync_assets.sh android-resources # Sync only resource/included + manifest
#   bash tools/sync_assets.sh all               # Sync to both
#
# android-resources is the lightweight target used by the Gradle preBuild task:
#   it generates resource/included/ from resource/proto/ + resource/binary/
#   and regenerates _manifest.txt.  Does NOT touch other asset directories.
#
# Shaders are NOT synced here — use tools/compile_shaders.sh for that.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IOS_ASSETS="$REPO_ROOT/ios/app/assets"
ANDROID_ASSETS="$REPO_ROOT/android/app/src/main/assets"

# ---------------------------------------------------------------------------
# sync_resource_included DEST
#   Populate DEST/resource/included/ from resource/proto/ and resource/binary/.
#   Correct order: proto first (copyresources.py recreates the dir from scratch),
#   then binary on top.
# ---------------------------------------------------------------------------
sync_resource_included() {
    local DEST="$1"
    echo "  resource/included/"
    mkdir -p "$DEST/resource/included"

    # Step 1: generate versioned layout from proto/ (deletes + recreates the dir)
    if command -v python3 &>/dev/null; then
        python3 "$REPO_ROOT/batch/make/copyresources.py" \
            "$REPO_ROOT/resource/proto" "$DEST/resource/included" 2>/dev/null || \
            echo "  WARNING: copyresources.py failed — resource/proto/ may be incomplete"
    else
        echo "  WARNING: python3 not found — skipping resource/proto/ generation"
    fi

    # Step 2: copy binary resources on top (textures, SDF icons, etc.)
    if [ -d "$REPO_ROOT/resource/binary" ]; then
        rsync -a --exclude='.DS_Store' \
            "$REPO_ROOT/resource/binary/" "$DEST/resource/included/"
    fi
}

# ---------------------------------------------------------------------------
# generate_manifest DEST
#   Write DEST/_manifest.txt listing every file under DEST (relative paths).
#   Excludes hidden files (dot-files) and _manifest.txt itself.
# ---------------------------------------------------------------------------
generate_manifest() {
    local DEST="$1"
    echo "  Generating _manifest.txt ..."
    find "$DEST" -type f \
        | sed "s|$DEST/||" \
        | grep -v '^_manifest' \
        | grep -v '^\.' \
        | sort \
        > "$DEST/_manifest.txt"
    local count
    count=$(wc -l < "$DEST/_manifest.txt" | tr -d ' ')
    echo "  _manifest.txt: $count entries"
}

# ---------------------------------------------------------------------------
# sync_common_assets DEST
#   Full asset sync used by ios and android targets.
# ---------------------------------------------------------------------------
sync_common_assets() {
    local DEST="$1"
    echo "Syncing assets to $DEST ..."

    # Game data directories (rsync for incremental copy, --delete removes stale files)
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

    # resource/included/ (proto first, binary on top)
    sync_resource_included "$DEST"
}

# ---------------------------------------------------------------------------
# Platform targets
# ---------------------------------------------------------------------------
sync_ios() {
    echo "=== Syncing iOS assets ==="
    sync_common_assets "$IOS_ASSETS"
    echo "=== iOS assets synced ==="
}

sync_android() {
    echo "=== Syncing Android assets ==="
    sync_common_assets "$ANDROID_ASSETS"
    generate_manifest "$ANDROID_ASSETS"
    echo "=== Android assets synced ==="
}

# Lightweight variant: only resource/included/ + _manifest.txt.
# Called automatically by the Gradle preBuild task on every build.
sync_android_resources() {
    echo "=== Syncing Android resource/included/ ==="
    sync_resource_included "$ANDROID_ASSETS"
    generate_manifest "$ANDROID_ASSETS"
    echo "=== Android resource/included/ synced ==="
}

TARGET="${1:-}"
case "$TARGET" in
    ios)                sync_ios ;;
    android)            sync_android ;;
    android-resources)  sync_android_resources ;;
    all)                sync_ios; sync_android ;;
    *)
        echo "Usage: $0 {ios|android|android-resources|all}"
        exit 1
        ;;
esac
