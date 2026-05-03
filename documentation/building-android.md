# Building for Android

## Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| Android Studio | Latest | [developer.android.com](https://developer.android.com/studio) |
| Android SDK | API 35 | Via Android Studio SDK Manager |
| Android NDK | 30.x | Via Android Studio SDK Manager → SDK Tools → NDK |
| CMake | 3.22.1 | Via Android Studio SDK Manager → SDK Tools → CMake |
| Java JDK | 17+ | `brew install openjdk@17` or bundled with Android Studio |
| glslc (shader compiler) | — | Vulkan SDK or `brew install shaderc` |
| protoc | Latest | `brew install protobuf` |
| Python 3 | 3.x | `brew install python3` (for resource generation) |

## External Dependencies

These must be available as source directories on your machine:

| Dependency | Get it | Notes |
|------------|--------|-------|
| **SDL3** | `git clone https://github.com/libsdl-org/SDL.git -b release-3.4.0 ~/dev/src/SDL3` | Built from source by CMake |
| **protobuf** | `git clone --recurse-submodules https://github.com/protocolbuffers/protobuf.git ~/dev/src/protobuf` | Must include `third_party/abseil-src` |
| **LuaJIT** | `git clone https://github.com/LuaJIT/LuaJIT.git ~/dev/src/luajit` | Pre-built to static lib |

GLM and libxml2 are fetched automatically by CMake (FetchContent).

## Build Steps

### 1. Configure local.properties

Create or edit `android/local.properties`:

```properties
sdk.dir=/path/to/Android/sdk
sdl3.dir=/path/to/SDL3
protobuf.dir=/path/to/protobuf
```

Example (macOS):
```properties
sdk.dir=/Users/you/Library/Android/sdk
sdl3.dir=/Users/you/dev/src/SDL3
protobuf.dir=/Users/you/dev/src/protobuf
```

### 2. Build LuaJIT for Android

LuaJIT must be pre-built as static libraries for each target ABI:

```bash
bash tools/build_luajit.sh android
```

This produces:
- `android/app/src/main/jni/luajit/arm64-v8a/libluajit.a`
- `android/app/src/main/jni/luajit/x86_64/libluajit.a`
- `android/app/src/main/jni/luajit/include/` (headers)

### 3. Compile shaders

Shaders must be pre-compiled to SPIR-V (no runtime shaderc on Android):

```bash
bash tools/compile_shaders.sh
```

Default output goes to `android/app/src/main/assets/shaders/`.

### 4. Sync assets

Copy game data (textures, moviepacks, cockpits, etc.) to the Android asset directory:

```bash
bash tools/sync_assets.sh android
```

### 5. Build

**From command line:**
```bash
cd android
./gradlew assembleDebug
```

The APK will be at `android/app/build/outputs/apk/debug/app-debug.apk`.

**For release:**
```bash
cd android
./gradlew assembleRelease
```

**From Android Studio:**
Open the `android/` directory as a project, let Gradle sync, then Build → Make Project.

### 6. Install on device

```bash
adb install android/app/build/outputs/apk/debug/app-debug.apk
```

Or use Android Studio's Run button to build + install + launch.

## Quick Reference (all steps)

```bash
# One-time setup
bash tools/build_luajit.sh android

# Build cycle
bash tools/compile_shaders.sh
bash tools/sync_assets.sh android
cd android && ./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Build Configuration

### Supported ABIs

The build targets `arm64-v8a` and `x86_64` by default. To change, edit `android/app/build.gradle`:

```groovy
abiFilters 'arm64-v8a'  // arm64 only (smaller APK)
```

### NDK Version

The default NDK version is `30.0.14904198`. Install it via Android Studio → Settings → SDK Manager → SDK Tools → NDK (Side by side).

### Minimum SDK

The minimum API level is 26 (Android 8.0), which guarantees Vulkan 1.0 support.

## Project Structure

```
android/
├── app/
│   ├── build.gradle                    # App module config (ABIs, SDK versions)
│   └── src/main/
│       ├── AndroidManifest.xml         # App permissions, Vulkan requirement
│       ├── assets/                     # Game data (synced by tools/sync_assets.sh)
│       │   ├── shaders/               # Pre-compiled SPIR-V (tools/compile_shaders.sh)
│       │   ├── textures/
│       │   ├── moviepacks/
│       │   ├── cockpits/
│       │   └── ...
│       ├── java/org/armagetronad/game/ # Java activity + touch overlay
│       ├── jni/
│       │   ├── CMakeLists.txt          # NDK build configuration
│       │   └── luajit/                 # Pre-built LuaJIT (tools/build_luajit.sh)
│       │       ├── arm64-v8a/libluajit.a
│       │       ├── x86_64/libluajit.a
│       │       └── include/
│       └── res/                        # Android resources (icons, styles)
├── build.gradle                        # Root Gradle config
├── local.properties                    # Local paths (sdk.dir, sdl3.dir, protobuf.dir)
└── gradle/                             # Gradle wrapper
```

## Troubleshooting

**"SDL3_SOURCE_DIR is not set"**
Add `sdl3.dir=/path/to/SDL3` to `android/local.properties`.

**"Abseil source not found"**
Clone protobuf with `--recurse-submodules`:
```bash
cd ~/dev/src/protobuf && git submodule update --init --recursive
```

**"libluajit.a not found" or LuaJIT linker errors**
Run `bash tools/build_luajit.sh android` before building.

**"Unable to locate a Java Runtime"**
Set `JAVA_HOME` to Android Studio's bundled JDK:
```bash
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
```

**NDK not found**
Install the NDK via Android Studio → Settings → SDK Manager → SDK Tools → NDK (Side by side). Make sure the version matches `ndkVersion` in `app/build.gradle`.

**CMake version mismatch**
Install CMake 3.22.1 via Android Studio → Settings → SDK Manager → SDK Tools → CMake.

**Shader errors at runtime**
Re-run `bash tools/compile_shaders.sh` after any shader source changes.

**Missing textures/assets at runtime**
Re-run `bash tools/sync_assets.sh android` to sync game data.
