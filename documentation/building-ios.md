# Building for iOS

## Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| Xcode | 15+ | Mac App Store |
| Xcode Command Line Tools | — | `xcode-select --install` |
| CMake | 3.22+ | `brew install cmake` |
| Vulkan SDK (MoltenVK) | Latest | [LunarG SDK](https://vulkan.lunarg.com/sdk/home) |
| Python 3 | 3.x | `brew install python3` (for resource generation) |
| glslc (shader compiler) | — | Included with Vulkan SDK |
| protoc | Latest | `brew install protobuf` |

## External Dependencies

These must be available as source directories on your machine:

| Dependency | Get it | Notes |
|------------|--------|-------|
| **SDL3** | `git clone https://github.com/libsdl-org/SDL.git -b release-3.4.0 ~/dev/src/SDL3` | Built from source by CMake |
| **protobuf** | `git clone --recurse-submodules https://github.com/protocolbuffers/protobuf.git ~/dev/src/protobuf` | Must include `third_party/abseil-src` |
| **LuaJIT** | `git clone https://github.com/LuaJIT/LuaJIT.git ~/dev/src/luajit` | Pre-built to static lib |

GLM and libxml2 are fetched automatically by CMake (FetchContent).

## Build Steps

### 1. Build LuaJIT for iOS

LuaJIT must be pre-built as a static library (JIT disabled on iOS — Apple policy).

```bash
bash tools/build_luajit.sh ios
```

This produces `build_ios/_deps/luajit-ios/libluajit.a` and headers.

If you prefer to build manually:
```bash
cd ~/dev/src/luajit
make TARGET_SYS=iOS \
     CROSS="xcrun -sdk iphoneos " \
     TARGET_FLAGS="-arch arm64 -isysroot $(xcrun --sdk iphoneos --show-sdk-path) -miphoneos-version-min=14.0" \
     BUILDMODE=static
# Copy libluajit.a + headers to build_ios/_deps/luajit-ios/
```

### 2. Compile shaders

Shaders must be pre-compiled to SPIR-V (no runtime shaderc on iOS):

```bash
OUT_DIR=ios/assets/shaders bash tools/compile_shaders.sh
```

### 3. Sync assets

Copy game data (textures, moviepacks, cockpits, etc.) to the iOS asset directory:

```bash
bash tools/sync_assets.sh ios
```

### 4. Configure with CMake

```bash
mkdir -p build_ios && cd build_ios

cmake ../ios/app \
    -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DSDL3_SOURCE_DIR=~/dev/src/SDL3 \
    -DPROTOBUF_SOURCE_DIR=~/dev/src/protobuf \
    -DMOLTENVK_DIR=/usr/local/lib
```

**CMake variables:**

| Variable | Required | Description |
|----------|----------|-------------|
| `SDL3_SOURCE_DIR` | Yes | Path to SDL3 source |
| `PROTOBUF_SOURCE_DIR` | No | Path to protobuf source (default: `~/dev/src/protobuf`) |
| `MOLTENVK_DIR` | No | Parent directory of `MoltenVK.xcframework` (default: `$VULKAN_SDK/..`) |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | No | Minimum iOS version (default: `17.0`) |

### 5. Build

**From command line:**
```bash
cd build_ios
xcodebuild -project armagetronad.xcodeproj \
           -scheme armagetronad \
           -configuration Debug \
           -destination 'generic/platform=iOS' \
           build
```

**From Xcode:**
Open `build_ios/armagetronad.xcodeproj`, select a device, and press Build.

### 6. Install on device

```bash
xcrun devicectl device install app \
    --device <DEVICE_UDID> \
    build_ios/Debug-iphoneos/armagetronad.app
```

Find your device UDID with:
```bash
xcrun devicectl list devices
```

## Quick Reference (all steps)

```bash
# One-time setup
bash tools/build_luajit.sh ios

# Build cycle
OUT_DIR=ios/assets/shaders bash tools/compile_shaders.sh
bash tools/sync_assets.sh ios
mkdir -p build_ios && cd build_ios
cmake ../ios/app -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DSDL3_SOURCE_DIR=~/dev/src/SDL3 \
    -DPROTOBUF_SOURCE_DIR=~/dev/src/protobuf
xcodebuild -project armagetronad.xcodeproj -scheme armagetronad \
    -configuration Debug -destination 'generic/platform=iOS' build
```

## Code Signing

The CMakeLists.txt sets a default development team ID. To use your own:

1. Open `build_ios/armagetronad.xcodeproj` in Xcode
2. Select the `armagetronad` target → Signing & Capabilities
3. Select your development team

Or override at configure time:
```bash
cmake ../ios/app -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DSDL3_SOURCE_DIR=~/dev/src/SDL3 \
    -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=YOUR_TEAM_ID
```

## Troubleshooting

**"SDL3_SOURCE_DIR is not set"**
Pass `-DSDL3_SOURCE_DIR=/path/to/SDL3` to cmake.

**"Abseil source not found"**
Clone protobuf with `--recurse-submodules`, or run:
```bash
cd ~/dev/src/protobuf && git submodule update --init --recursive
```

**"Pre-compiled shaders not found"**
Run the shader compile step: `OUT_DIR=ios/assets/shaders bash tools/compile_shaders.sh`

**MoltenVK not found**
Install the LunarG Vulkan SDK, which places `MoltenVK.xcframework` at `/usr/local/lib/MoltenVK.xcframework`.

**Linker errors with `absl::lts_*` symbols**
This happens when system-installed Abseil headers shadow the local source. The CMakeLists.txt already handles include order — if you see this, ensure you don't have conflicting Abseil in `/usr/local/include`. Clean and re-configure.

**LuaJIT "libluajit.a not found"**
Run `bash tools/build_luajit.sh ios` before configuring CMake.
