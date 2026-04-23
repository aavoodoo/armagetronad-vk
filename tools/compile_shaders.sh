#!/usr/bin/env bash
# Compile all game GLSL shaders to SPIR-V (no runtime shaderc on mobile).
# Run from the repository root:
#   bash tools/compile_shaders.sh
#
# Default output: android/app/src/main/assets/shaders/
# Override with OUT_DIR env variable, e.g.:
#   OUT_DIR=ios/app/assets/shaders bash tools/compile_shaders.sh
#
# Set GLSLC env variable to override the compiler path.

set -e

GLSLC=${GLSLC:-glslc}
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/shaders"
OUT="${OUT_DIR:-"$REPO_ROOT/android/app/src/main/assets/shaders"}"

mkdir -p "$OUT"
mkdir -p "$OUT/postprocess/bloom"
mkdir -p "$OUT/postprocess/celshading"
mkdir -p "$OUT/postprocess/depthviz"
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

# Uber shaders (base include path is shaders/)
compile uber.vert        uber.vert.spv        -I "$SRC"
compile uber.frag        uber.frag.spv        -I "$SRC"
compile uber.frag        uber.frag.emissive.spv -I "$SRC" -DUSE_EMISSIVE_OUT=1

# Fullscreen vertex shader for post-processing
compile postprocess/fullscreen.vert postprocess/fullscreen.vert.spv -I "$SRC/postprocess"

# Bloom effect
PP_BLOOM_INCLUDES="-I $SRC/postprocess/bloom -I $SRC/postprocess -I $SRC"
compile postprocess/bloom/bloom_extract.frag   postprocess/bloom/bloom_extract.frag.spv   $PP_BLOOM_INCLUDES
compile postprocess/bloom/bloom_downsample.frag postprocess/bloom/bloom_downsample.frag.spv $PP_BLOOM_INCLUDES
compile postprocess/bloom/bloom_blur_h.frag    postprocess/bloom/bloom_blur_h.frag.spv    $PP_BLOOM_INCLUDES
compile postprocess/bloom/bloom_blur_v.frag    postprocess/bloom/bloom_blur_v.frag.spv    $PP_BLOOM_INCLUDES
compile postprocess/bloom/bloom_composite.frag postprocess/bloom/bloom_composite.frag.spv $PP_BLOOM_INCLUDES

# Cel shading effect
PP_CEL_INCLUDES="-I $SRC/postprocess/celshading -I $SRC/postprocess -I $SRC"
compile postprocess/celshading/celshading.frag postprocess/celshading/celshading.frag.spv $PP_CEL_INCLUDES

# Depth visualization effect
PP_DEPTH_INCLUDES="-I $SRC/postprocess/depthviz -I $SRC/postprocess -I $SRC"
compile postprocess/depthviz/depthviz.frag postprocess/depthviz/depthviz.frag.spv $PP_DEPTH_INCLUDES

# Passthrough effect
PP_PT_INCLUDES="-I $SRC/postprocess/passthrough -I $SRC/postprocess -I $SRC"
compile postprocess/passthrough/passthrough.frag postprocess/passthrough/passthrough.frag.spv $PP_PT_INCLUDES

echo "[compile_shaders] Done. SPV files written to $OUT"
