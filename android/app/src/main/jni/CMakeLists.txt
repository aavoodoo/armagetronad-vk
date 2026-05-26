cmake_minimum_required(VERSION 3.22)
project(armagetronad VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Paths
# ============================================================================
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(SRC "${REPO_ROOT}/src")
set(ANDROID_CMAKE_DIR "${REPO_ROOT}/android/cmake")

# SDL3 source directory — pass via -DSDL3_SOURCE_DIR=... from build.gradle
# or set sdl3.dir in local.properties
if(NOT DEFINED SDL3_SOURCE_DIR)
    message(FATAL_ERROR
        "SDL3_SOURCE_DIR is not set. Pass it with:\n"
        "  sdl3.dir=/path/to/SDL3 in android/local.properties\n"
        "  or -DSDL3_SOURCE_DIR=/path/to/SDL3 in build.gradle cmake arguments")
endif()

# ============================================================================
# aa_config.h — generated config header
# ============================================================================
set(VERSION "0.4.0_android")
configure_file(
    "${ANDROID_CMAKE_DIR}/aa_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/aa_config.h"
    @ONLY
)

# ============================================================================
# SDL3
# ============================================================================
add_subdirectory("${SDL3_SOURCE_DIR}" SDL3 EXCLUDE_FROM_ALL)

# ============================================================================
# GLM (header-only)
# ============================================================================
include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ============================================================================
# Protobuf (built from source — must match system protoc version)
# Default source: ~/dev/src/protobuf (must match installed protoc version).
# Override with -DPROTOBUF_SOURCE_DIR=/path/to/protobuf at configure time.
# ============================================================================
if(NOT DEFINED PROTOBUF_SOURCE_DIR)
    set(PROTOBUF_SOURCE_DIR "$ENV{HOME}/dev/src/protobuf")
endif()

set(_absl_src "${PROTOBUF_SOURCE_DIR}/third_party/abseil-src")
if(EXISTS "${_absl_src}/CMakeLists.txt")
    message(STATUS "Building Abseil from: ${_absl_src}")
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    add_subdirectory("${_absl_src}" "${CMAKE_CURRENT_BINARY_DIR}/absl" EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "Abseil source not found at ${_absl_src}.\n"
        "Set PROTOBUF_SOURCE_DIR to the root of the protobuf checkout that contains\n"
        "third_party/abseil-src.")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    protobuf
    SOURCE_DIR "${PROTOBUF_SOURCE_DIR}"
)
FetchContent_MakeAvailable(protobuf)

# protoc (host tool) for generating .pb.cc/.pb.h at build time.
# CMake cross-compilation: protoc runs on the HOST, not the Android target.
find_program(PROTOC_EXECUTABLE protoc REQUIRED)

# Generate .pb.cc and .pb.h from .proto files
file(GLOB PROTO_FILES "${SRC}/protobuf/*.proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf_gen")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

set(PROTO_SRCS "")
set(PROTO_HDRS "")
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_STEM "${PROTO_FILE}" NAME_WE)
    list(APPEND PROTO_SRCS "${PROTO_GEN_DIR}/${PROTO_STEM}.pb.cc")
    list(APPEND PROTO_HDRS "${PROTO_GEN_DIR}/${PROTO_STEM}.pb.h")
    add_custom_command(
        OUTPUT "${PROTO_GEN_DIR}/${PROTO_STEM}.pb.cc"
               "${PROTO_GEN_DIR}/${PROTO_STEM}.pb.h"
        COMMAND ${PROTOC_EXECUTABLE}
                --proto_path=${SRC}/protobuf
                --cpp_out=${PROTO_GEN_DIR}
                ${PROTO_FILE}
        DEPENDS "${PROTO_FILE}"
        COMMENT "Generating protobuf: ${PROTO_STEM}"
        VERBATIM
    )
endforeach()

# ============================================================================
# libxml2 (built from source via FetchContent)
# ============================================================================
set(LIBXML2_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_PROGRAMS OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_PYTHON OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_LZMA OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_ICONV OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_ICU OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_THREADS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libxml2
    GIT_REPOSITORY https://github.com/GNOME/libxml2.git
    GIT_TAG        v2.12.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libxml2)

# ============================================================================
# LuaJIT (interpreter-only on Android — JIT disabled for compatibility)
# Pre-built static libs per ABI live in android/app/src/main/jni/luajit/
# Build them with: bash tools/build_luajit.sh android
# ============================================================================
set(LUAJIT_ANDROID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luajit")
add_library(lua_static STATIC IMPORTED)
set_target_properties(lua_static PROPERTIES
    IMPORTED_LOCATION "${LUAJIT_ANDROID_DIR}/${ANDROID_ABI}/libluajit.a"
    INTERFACE_INCLUDE_DIRECTORIES "${LUAJIT_ANDROID_DIR}/include"
)

# ============================================================================
# Game source files
# ============================================================================

# Collect sources from each directory, excluding platform-specific files.
# Headers are not listed (not needed for compilation).

# tools (exclude vParser.cpp — added explicitly below to avoid duplication)
file(GLOB TOOLS_SRCS
    "${SRC}/tools/*.cpp"
    "${SRC}/tools/values/*.cpp"
    "${SRC}/resource/*.cpp"
    "${SRC}/thirdparty/mathexpr/mathexpr.cpp"
    # mathexpr_c.cpp duplicates symbols from mathexpr.cpp — not in desktop build either
    # stb_vorbis.c is NOT compiled separately — eAudioMiniaudio.cpp #includes it directly
    # miniz.c is NOT compiled separately — gMoviepack.cpp #includes it directly
)
list(FILTER TOOLS_SRCS EXCLUDE REGEX "vParser\\.cpp$")
# tSafePTRBase.cpp is not part of the desktop build (not in Makefile.am) and uses undefined 'List<>'
list(FILTER TOOLS_SRCS EXCLUDE REGEX "tSafePTRBase\\.cpp$")
# Standalone tool programs that define their own main() — not part of the game binary
list(FILTER TOOLS_SRCS EXCLUDE REGEX "transfab\\.cpp$")
list(FILTER TOOLS_SRCS EXCLUDE REGEX "memtest\\.cpp$")
# binreloc needs special handling (C file, not C++)
set(BINRELOC_SRCS "${SRC}/thirdparty/binreloc/prefix.c")

# network
file(GLOB NETWORK_SRCS
    "${SRC}/network/nAuthentication.cpp"
    "${SRC}/network/nBinary.cpp"
    "${SRC}/network/nConfig.cpp"
    "${SRC}/network/nKrawall.cpp"
    "${SRC}/network/nKrawallPrivate.cpp"
    "${SRC}/network/nNetObject.cpp"
    "${SRC}/network/nNetwork.cpp"
    "${SRC}/network/nObserver.cpp"
    "${SRC}/network/nPriorizing.cpp"
    "${SRC}/network/nProtoBuf.cpp"
    "${SRC}/network/nServerInfo.cpp"
    "${SRC}/network/nSocket.cpp"
    "${SRC}/network/nSpamProtection.cpp"
    "${SRC}/network/nStreamMessage.cpp"
)

# engine
file(GLOB ENGINE_SRCS
    "${SRC}/engine/eAdvWall.cpp"
    "${SRC}/engine/eAuthentication.cpp"
    "${SRC}/engine/eAxis.cpp"
    "${SRC}/engine/eCamera.cpp"
    "${SRC}/engine/eChat.cpp"
    "${SRC}/engine/eDebugLine.cpp"
    "${SRC}/engine/eDisplay.cpp"
    "${SRC}/engine/eEventNotification.cpp"
    "${SRC}/engine/eFloor.cpp"
    "${SRC}/engine/eGameObject.cpp"
    "${SRC}/engine/eGrid.cpp"
    "${SRC}/engine/eKrawall.cpp"
    "${SRC}/engine/eLadderLog.cpp"
    "${SRC}/engine/eLagCompensation.cpp"
    "${SRC}/engine/eNetGameObject.cpp"
    "${SRC}/engine/ePath.cpp"
    "${SRC}/engine/ePlayer.cpp"
    "${SRC}/engine/eSensor.cpp"
    "${SRC}/engine/eSoundMixer.cpp"
    "${SRC}/engine/eSpawn.cpp"
    "${SRC}/engine/eTeam.cpp"
    "${SRC}/engine/eTimer.cpp"
    "${SRC}/engine/eVoter.cpp"
    "${SRC}/engine/eWall.cpp"
    "${SRC}/engine/eWarmup.cpp"
    "${SRC}/engine/sound/eAudioMiniaudio.cpp"
    "${SRC}/engine/sound/eChannelMiniaudio.cpp"
)

# render
file(GLOB RENDER_SRCS
    "${SRC}/render/*.cpp"
    "${SRC}/render/fonts/*.cpp"
    "${SRC}/render/geometry/*.cpp"
    "${SRC}/render/renderers/*.cpp"
    "${SRC}/render/utilities/*.cpp"
    "${SRC}/render/vulkan/*.cpp"
)
# Exclude the old GL3/cout file if present
list(FILTER RENDER_SRCS EXCLUDE REGEX "rConsoleCout\\.cpp$")

# ui
set(UI_SRCS
    "${SRC}/ui/uEventSDL.cpp"
    "${SRC}/ui/uInput.cpp"
    "${SRC}/ui/uInputProvider.cpp"
    "${SRC}/ui/uInputProviderSDL.cpp"
    "${SRC}/ui/uInputQueue.cpp"
    "${SRC}/ui/uMenu.cpp"
    "${SRC}/thirdparty/scrap/scrap.cpp"
)

# tron
file(GLOB TRON_SRCS
    "${SRC}/tron/gAIBase.cpp"
    "${SRC}/tron/gAICharacter.cpp"
    "${SRC}/tron/gArena.cpp"
    "${SRC}/tron/gArmagetron.cpp"
    "${SRC}/tron/gCamera.cpp"
    "${SRC}/tron/gCommandLineJumpStart.cpp"
    "${SRC}/tron/gCycle.cpp"
    "${SRC}/tron/gCycleMovement.cpp"
    "${SRC}/tron/gExplosion.cpp"
    "${SRC}/tron/gFloor.cpp"
    "${SRC}/tron/gFriends.cpp"
    "${SRC}/tron/gGame.cpp"
    "${SRC}/tron/gLuaBindings.cpp"
    "${SRC}/tron/gCockpitPack.cpp"
    "${SRC}/tron/gJoystick.cpp"
    "${SRC}/tron/gLanguageMenu.cpp"
    "${SRC}/tron/gLogo.cpp"
    "${SRC}/tron/gMenus.cpp"
    "${SRC}/tron/gMoviepack.cpp"
    "${SRC}/tron/gParser.cpp"
    "${SRC}/tron/gParticles.cpp"
    "${SRC}/tron/gRotation.cpp"
    "${SRC}/tron/gSensor.cpp"
    "${SRC}/tron/gServerBrowser.cpp"
    "${SRC}/tron/gServerFavorites.cpp"
    "${SRC}/tron/gSparks.cpp"
    "${SRC}/tron/gStatistics.cpp"
    "${SRC}/tron/gStatList.cpp"
    "${SRC}/tron/gStuff.cpp"
    "${SRC}/tron/gTeam.cpp"
    "${SRC}/tron/gTutorial.cpp"
    "${SRC}/tron/gWall.cpp"
    # gWinZone.cpp is zonesv1 only — excluded because ENABLE_ZONESV2 is defined and ENABLE_ZONESV1 is not
    "${SRC}/tron/cockpit/cCamview.cpp"
    "${SRC}/tron/cockpit/cCockpit.cpp"
    "${SRC}/tron/cockpit/cGauges.cpp"
    "${SRC}/tron/cockpit/cLabel.cpp"
    "${SRC}/tron/cockpit/cMap.cpp"
    "${SRC}/tron/cockpit/cRectangle.cpp"
    "${SRC}/tron/cockpit/cTouchButton.cpp"
    "${SRC}/tron/cockpit/cChatArea.cpp"
    "${SRC}/tron/cockpit/cWidgetBase.cpp"
    "${SRC}/tron/zone/zEffectGroup.cpp"
    "${SRC}/tron/zone/zEffector.cpp"
    "${SRC}/tron/zone/zMonitor.cpp"
    "${SRC}/tron/zone/zSelector.cpp"
    "${SRC}/tron/zone/zShape.cpp"
    "${SRC}/tron/zone/zTimedZone.cpp"
    "${SRC}/tron/zone/zValidator.cpp"
    "${SRC}/tron/zone/zZone.cpp"
    "${SRC}/tron/zone/zZoneInfluence.cpp"
)

# value parser — vParser.cpp is pre-generated (committed to src/) from vParser.ypp via bison.
set(VPARSER_SRC "${SRC}/tools/values/vParser.cpp")

# ============================================================================
# Armagetron shared library (loaded by SDLActivity)
# ============================================================================
add_library(armagetronad SHARED
    ${TOOLS_SRCS}
    ${BINRELOC_SRCS}
    ${NETWORK_SRCS}
    ${ENGINE_SRCS}
    ${RENDER_SRCS}
    ${UI_SRCS}
    ${TRON_SRCS}
    ${PROTO_SRCS}
    ${VPARSER_SRC}
)

# ============================================================================
# Include directories
# ============================================================================
target_include_directories(armagetronad PRIVATE
    # Generated headers
    "${CMAKE_CURRENT_BINARY_DIR}"           # aa_config.h
    "${PROTO_GEN_DIR}"                      # *.pb.h

    # Game source tree
    "${SRC}"
    "${SRC}/tools"
    "${SRC}/tools/values"
    "${SRC}/engine"
    "${SRC}/engine/sound"
    "${SRC}/network"
    "${SRC}/render"
    "${SRC}/render/utilities"
    "${SRC}/render/renderers"
    "${SRC}/render/geometry"
    "${SRC}/render/fonts"
    "${SRC}/render/vulkan"
    "${SRC}/ui"
    "${SRC}/tron"
    "${SRC}/tron/zone"
    "${SRC}/tron/cockpit"
    "${SRC}/resource"
    "${SRC}/protobuf"
    "${SRC}/thirdparty/mathexpr"
    "${SRC}/thirdparty/utf8"
    "${SRC}/thirdparty/stb"
    "${SRC}/thirdparty/miniaudio"
    "${SRC}/thirdparty/miniz"
    "${SRC}/thirdparty/scrap"
    "${SRC}/thirdparty/binreloc"
    "${SRC}/thirdparty/vma"
    "${SRC}/thirdparty/luapp"
    "${LUAJIT_ANDROID_DIR}/include"        # lua.h, lauxlib.h, lualib.h (LuaJIT)
    "${ANDROID_CMAKE_DIR}"                 # tUniversalVariables.h (Android-specific)
)

# ============================================================================
# Compile definitions
# ============================================================================
target_compile_definitions(armagetronad PRIVATE
    HAVE_CONFIG_H
    # No dedicated server
    # DEDICATED
    DONTUSEMEMMANAGER
    BR_PTHREAD
    # Silence binreloc portability macros
    ENABLE_BINRELOC
    # ENABLE_ZONESV2, KRAWALL_SERVER, BUILDMAIN defined in aa_config.h
    # Needed to trigger inclusion of tUniversalVariables.h in tDirectories.cpp
    # Also used as a string: static const char * s_topSourceDir = TOP_SOURCE_DIR;
    "TOP_SOURCE_DIR=\".\""
    # VMA: Android NDK API 26 libvulkan.so only exports Vulkan 1.0 symbols.
    # Vulkan 1.1+ functions must be resolved dynamically via vkGetDeviceProcAddr.
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

# ============================================================================
# Link libraries
# ============================================================================
target_link_libraries(armagetronad
    SDL3::SDL3
    LibXml2
    protobuf::libprotobuf
    glm::glm
    lua_static
    android
    log
    vulkan
)

# ============================================================================
# Compiler flags
# ============================================================================
target_compile_options(armagetronad PRIVATE
    -Wall
    -Wno-unused-parameter
    -Wno-sign-compare
    -fexceptions
    -frtti
)
