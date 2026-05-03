#!/usr/bin/env bash
# Compile all game GLSL shaders to SPIR-V (no runtime shaderc on mobile).
# Run from the repository root:
#   bash tools/compile_shaders.sh
#
# Default output: android/app/src/main/assets/shaders/
# Override with OUT_DIR env variable, e.g.:
#   OUT_DIR=ios/assets/shaders bash tools/compile_shaders.sh
#
# Set GLSLC env variable to override the compiler path.
#
# Note: Post-process effect shaders (bloom, celshading, depthviz) are bundled
# inside their respective moviepack .aamvp.zip files and do NOT need separate
# compilation here. Only core engine shaders and the built-in passthrough
# effect are compiled by this script.

set -e

GLSLC=${GLSLC:-glslc}
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/shaders"
OUT="${OUT_DIR:-"$REPO_ROOT/android/app/src/main/assets/shaders"}"

mkdir -p "$OUT"
mkdir -p "$OUT/postprocess/passthrough"

echo "[compile_shaders] GLSLC=$GLSLC"
echo "[compile_shaders] SRC=$SRC"
echo "[compile_shaders] OUT=$OUT"

compile() {
    local src="$1"; shift
    local dst="$1"; shift
    echo "  $src -> $dst"
    "$GLSLC" "$@" "$SRC/$src" -o "$OUT/$dst"
}

# ── Core engine shaders ──────────────────────────────────────────────────────

# Uber shaders (main rendering pipeline)
compile uber.vert            uber.vert.spv            -I "$SRC"
compile uber_instanced.vert  uber_instanced.vert.spv  -I "$SRC"
compile uber.frag            uber.frag.spv            -I "$SRC"
compile uber.frag            uber.frag.emissive.spv   -I "$SRC" -DUSE_EMISSIVE_OUT=1

# Shadow map shaders
compile shadow.vert  shadow.vert.spv  -I "$SRC"
compile shadow.frag  shadow.frag.spv  -I "$SRC"

# GPU compute wall geometry
compile wall_gen.comp  wall_gen.comp.spv  -I "$SRC"

# ── Post-processing (built-in) ───────────────────────────────────────────────

# Fullscreen vertex shader (shared by all post-process effects)
compile postprocess/fullscreen.vert postprocess/fullscreen.vert.spv -I "$SRC/postprocess"

# Passthrough effect (built-in, always available)
PP_PT_INCLUDES="-I $SRC/postprocess/passthrough -I $SRC/postprocess -I $SRC"
compile postprocess/passthrough/passthrough.frag postprocess/passthrough/passthrough.frag.spv $PP_PT_INCLUDES

# ── Lua effect scripts (needed at runtime to define PP pass graphs) ──────────
# Copy .lua scripts alongside the compiled .spv files.
echo "Copying Lua effect scripts..."
for lua in $(find "$SRC/postprocess" -name "*.lua" 2>/dev/null); do
    rel="${lua#$SRC/}"
    mkdir -p "$OUT/$(dirname "$rel")"
    cp "$lua" "$OUT/$rel"
    echo "  $rel"
done

echo "[compile_shaders] Done. SPV files written to $OUT"
