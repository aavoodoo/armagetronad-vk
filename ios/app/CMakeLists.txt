cmake_minimum_required(VERSION 3.22)
project(armagetronad VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Paths
# ============================================================================
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SRC "${REPO_ROOT}/src")
set(IOS_CMAKE_DIR "${REPO_ROOT}/ios/cmake")
set(IOS_ASSETS_DIR "${REPO_ROOT}/ios/assets")

# SDL3 source directory — pass via -DSDL3_SOURCE_DIR=... at configure time
if(NOT DEFINED SDL3_SOURCE_DIR)
    message(FATAL_ERROR
        "SDL3_SOURCE_DIR is not set. Pass it with:\n"
        "  cmake -DSDL3_SOURCE_DIR=/path/to/SDL-release-3.4.0 ...")
endif()

# MoltenVK xcframework — pass via -DMOLTENVK_DIR=... at configure time
# Expected layout: <MOLTENVK_DIR>/MoltenVK.xcframework
if(NOT DEFINED MOLTENVK_DIR)
    # Fallback: check the LunarG Vulkan SDK path (installed by VulkanSDK macOS installer)
    set(MOLTENVK_DIR "$ENV{VULKAN_SDK}/..")
endif()

# ============================================================================
# iOS deployment target
# ============================================================================
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum iOS deployment target")
endif()

# ============================================================================
# aa_config.h — generated config header
# ============================================================================
set(VERSION "0.4.0_ios")
configure_file(
    "${IOS_CMAKE_DIR}/aa_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/aa_config.h"
    @ONLY
)

# ============================================================================
# SDL3 (build from source — SDL3 natively supports iOS)
# ============================================================================
# Build SDL3 as static library — avoids dylib embedding issues on iOS device deployment
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
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
# Protobuf (built from source — must match system protoc version for iOS)
# Default source: ~/dev/src/protobuf (v7.35.0 = same version as installed protoc).
# Override with -DPROTOBUF_SOURCE_DIR=/path/to/protobuf at configure time.
# ============================================================================
if(NOT DEFINED PROTOBUF_SOURCE_DIR)
    set(PROTOBUF_SOURCE_DIR "$ENV{HOME}/dev/src/protobuf")
endif()

# Build Abseil and utf8_range as add_subdirectory (same CMake context) so their
# targets are available before protobuf is configured.  Protobuf's cmake checks
# for TARGET absl::strings; if it already exists the find_package / fetch block
# is skipped entirely, and the targets propagate transitively to our link step.
# We build everything as STATIC so the .a files link directly into the iOS app
# bundle without needing embedded dylib/framework wrappers.
# Build everything as STATIC so .a files link directly into the iOS app
# bundle without needing embedded dylib/framework wrappers.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
# Pre-add Abseil as add_subdirectory (same CMake context) so its targets exist
# when protobuf's cmake checks "if (NOT TARGET absl::strings)".  Protobuf then
# skips its own find_package / download step and uses our targets directly,
# ensuring Abseil builds for iOS and links transitively.
# utf8_range is NOT pre-added here — protobuf handles it internally via
# cmake/utf8_range.cmake which also sets the required include_directories.
set(_absl_src "${PROTOBUF_SOURCE_DIR}/third_party/abseil-src")
if(EXISTS "${_absl_src}/CMakeLists.txt")
    message(STATUS "Building Abseil from: ${_absl_src}")
    add_subdirectory("${_absl_src}" "${CMAKE_CURRENT_BINARY_DIR}/absl" EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "Abseil source not found at ${_absl_src}.\n"
        "Set PROTOBUF_SOURCE_DIR to the root of the protobuf checkout that contains\n"
        "third_party/abseil-src.")
endif()

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

# protoc runs on the HOST (macOS) to generate .pb.cc/.pb.h for the iOS target.
find_program(PROTOC_EXECUTABLE protoc REQUIRED)

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
# Pre-compiled SPIR-V shaders
# ============================================================================
# Shaders must be compiled offline — no runtime shaderc on iOS.
# Run from repo root: OUT_DIR=ios/assets/shaders bash tools/compile_shaders.sh
# The compiled .spv files are bundled into the app.
set(SHADER_DIR "${IOS_ASSETS_DIR}/shaders")
if(NOT EXISTS "${SHADER_DIR}/uber.vert.spv")
    message(WARNING
        "Pre-compiled shaders not found at ${SHADER_DIR}.\n"
        "Run: OUT_DIR=ios/assets/shaders bash tools/compile_shaders.sh\n"
        "before building.")
endif()

# ============================================================================
# Game source files (same set as Android build)
# ============================================================================
file(GLOB TOOLS_SRCS
    "${SRC}/tools/*.cpp"
    "${SRC}/tools/values/*.cpp"
    "${SRC}/resource/*.cpp"
    "${SRC}/thirdparty/mathexpr/mathexpr.cpp"
)
list(FILTER TOOLS_SRCS EXCLUDE REGEX "vParser\\.cpp$")
list(FILTER TOOLS_SRCS EXCLUDE REGEX "tSafePTRBase\\.cpp$")
list(FILTER TOOLS_SRCS EXCLUDE REGEX "transfab\\.cpp$")
list(FILTER TOOLS_SRCS EXCLUDE REGEX "memtest\\.cpp$")
# iOS-specific Objective-C helper (Documents directory path, file sharing support)
list(APPEND TOOLS_SRCS "${SRC}/tools/tDirectoriesIOS.mm")
set(BINRELOC_SRCS "${SRC}/thirdparty/binreloc/prefix.c")

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

file(GLOB RENDER_SRCS
    "${SRC}/render/*.cpp"
    "${SRC}/render/*.mm"
    "${SRC}/render/fonts/*.cpp"
    "${SRC}/render/geometry/*.cpp"
    "${SRC}/render/renderers/*.cpp"
    "${SRC}/render/utilities/*.cpp"
    "${SRC}/render/vulkan/*.cpp"
)
list(FILTER RENDER_SRCS EXCLUDE REGEX "rConsoleCout\\.cpp$")

set(UI_SRCS
    "${SRC}/ui/uEventSDL.cpp"
    "${SRC}/ui/uInput.cpp"
    "${SRC}/ui/uInputProvider.cpp"
    "${SRC}/ui/uInputProviderSDL.cpp"
    "${SRC}/ui/uInputQueue.cpp"
    "${SRC}/ui/uMenu.cpp"
    "${SRC}/thirdparty/scrap/scrap.cpp"
)

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
    "${SRC}/tron/gJoystick.cpp"
    "${SRC}/tron/gLanguageMenu.cpp"
    "${SRC}/tron/gLogo.cpp"
    "${SRC}/tron/gMenus.cpp"
    "${SRC}/tron/gMoviepack.cpp"
    "${SRC}/tron/gCockpitPack.cpp"
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

set(VPARSER_SRC "${SRC}/tools/values/vParser.cpp")

# ============================================================================
# App bundle resources — shaders and game data
# ============================================================================
file(GLOB_RECURSE SHADER_RESOURCES "${SHADER_DIR}/*.spv")

# Generate languages.txt from languages.txt.in (substitute @progtitle@)
set(progtitle "Armagetron Advanced")
configure_file(
    "${REPO_ROOT}/language/languages.txt.in"
    "${CMAKE_CURRENT_BINARY_DIR}/language/languages.txt"
    @ONLY
)

# aiplayers.cfg.in has no CMake substitutions — copy it as aiplayers.cfg so the
# game finds it at <bundle>/config/aiplayers.cfg (gAICharacter::LoadAll() path).
configure_file(
    "${REPO_ROOT}/config/aiplayers.cfg.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config/aiplayers.cfg"
    COPYONLY
)

# ============================================================================
# Generate included resources from resource/proto/ at configure time.
# These are the pre-processed resources (cockpit, maps, etc.) that the game
# searches for at runtime under <data_dir>/resource/included/.
# Equivalent to the autotools 'included' target (batch/make/copyresources.py).
# ============================================================================
find_package(Python3 QUIET COMPONENTS Interpreter)
set(INCLUDED_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/resource/included")
if(Python3_FOUND)
    message(STATUS "Generating included resources from resource/proto/ ...")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
                "${REPO_ROOT}/batch/make/copyresources.py"
                "${REPO_ROOT}/resource/proto"
                "${INCLUDED_RESOURCE_DIR}"
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE _copyres_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT _copyres_result EQUAL 0)
        message(WARNING "copyresources.py failed — included resources (cockpit, maps) may be missing from bundle.")
    else()
        message(STATUS "Included resources generated at ${INCLUDED_RESOURCE_DIR}")
    endif()
else()
    message(WARNING "Python3 not found — included resources (cockpit, maps) will not be generated.")
endif()

file(GLOB_RECURSE INCLUDED_RESOURCES "${INCLUDED_RESOURCE_DIR}/*")
foreach(_res ${INCLUDED_RESOURCES})
    file(RELATIVE_PATH _rel "${INCLUDED_RESOURCE_DIR}" "${_res}")
    get_filename_component(_dir "${_rel}" DIRECTORY)
    if(_dir)
        set(_pkg_loc "resource/included/${_dir}")
    else()
        set(_pkg_loc "resource/included")
    endif()
    set_source_files_properties("${_res}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "${_pkg_loc}"
    )
endforeach()

# Bundle pre-built binary resources (textures, PNGs) under resource/included/
# so the runtime resource system finds them at the same path as the desktop build.
# (resource/binary/ in the repo maps to resource/included/ at runtime.)
file(GLOB_RECURSE BINARY_RESOURCES "${REPO_ROOT}/resource/binary/*")
foreach(_res ${BINARY_RESOURCES})
    file(RELATIVE_PATH _rel "${REPO_ROOT}/resource/binary" "${_res}")
    get_filename_component(_dir "${_rel}" DIRECTORY)
    if(_dir)
        set(_pkg_loc "resource/included/${_dir}")
    else()
        set(_pkg_loc "resource/included")
    endif()
    set_source_files_properties("${_res}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "${_pkg_loc}"
    )
    list(APPEND INCLUDED_RESOURCES "${_res}")
endforeach()

# Game data resources to bundle (textures, language, config, models, etc.)
file(GLOB_RECURSE GAME_RESOURCES
    "${REPO_ROOT}/textures/*"
    "${REPO_ROOT}/language/*"
    "${REPO_ROOT}/config/*"
    "${REPO_ROOT}/models/*"
    "${REPO_ROOT}/resource/*"
    "${REPO_ROOT}/sound/*"
    "${REPO_ROOT}/music/*"
)
# Exclude .in templates — we bundle the generated versions instead
list(FILTER GAME_RESOURCES EXCLUDE REGEX "languages\\.txt\\.in$")
list(FILTER GAME_RESOURCES EXCLUDE REGEX "aiplayers\\.cfg\\.in$")
# Exclude resource/binary/ — those files are re-bundled under resource/included/ by BINARY_RESOURCES above
list(FILTER GAME_RESOURCES EXCLUDE REGEX "/resource/binary/")
# Add the generated files
list(APPEND GAME_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/language/languages.txt")
list(APPEND GAME_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/config/aiplayers.cfg")
# Preserve relative directory structure in the bundle:
# e.g. <repo>/textures/foo.png → Bundle/textures/foo.png
foreach(_res ${GAME_RESOURCES})
    # Generated files are under CMAKE_CURRENT_BINARY_DIR; repo files under REPO_ROOT
    if("${_res}" MATCHES "^${CMAKE_CURRENT_BINARY_DIR}")
        file(RELATIVE_PATH _rel "${CMAKE_CURRENT_BINARY_DIR}" "${_res}")
    else()
        file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_res}")
    endif()
    get_filename_component(_dir "${_rel}" DIRECTORY)
    set_source_files_properties("${_res}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "${_dir}"
    )
endforeach()
# Shader .spv files go into the bundle's shaders/ subtree
foreach(_spv ${SHADER_RESOURCES})
    file(RELATIVE_PATH _rel "${IOS_ASSETS_DIR}" "${_spv}")
    get_filename_component(_dir "${_rel}" DIRECTORY)
    set_source_files_properties("${_spv}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "${_dir}"
    )
endforeach()

# ============================================================================
# App icon — bundle PNG icons directly at the root of the app bundle.
# Info.plist CFBundleIcons references these filenames (without extension).
# ============================================================================
file(GLOB APP_ICON_FILES "${REPO_ROOT}/ios/icons/AppIcon-*.png")
foreach(_icon ${APP_ICON_FILES})
    set_source_files_properties("${_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "."
    )
endforeach()

# ============================================================================
# rTouchOverlayIOS.mm uses weak properties and requires ARC.
# The bridge file (rTouchBridgeIOS.mm) stays MRC so it can include C++ headers.
# ============================================================================
set_source_files_properties("${SRC}/render/rTouchOverlayIOS.mm"
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

# ============================================================================
# Launch screen storyboard — required to opt out of iOS legacy 320×480 mode.
# Without UILaunchStoryboardName + a compiled storyboard, iOS treats the app
# as a legacy iPhone app and forces a 320×480 viewport.
# Do NOT set MACOSX_PACKAGE_LOCATION — let Xcode compile it to .storyboardc.
# ============================================================================
set(LAUNCH_SCREEN_STORYBOARD "${REPO_ROOT}/ios/LaunchScreen.storyboard")

# ============================================================================
# Armagetron Advanced iOS executable
# ============================================================================
add_executable(armagetronad MACOSX_BUNDLE
    ${TOOLS_SRCS}
    ${BINRELOC_SRCS}
    ${NETWORK_SRCS}
    ${ENGINE_SRCS}
    ${RENDER_SRCS}
    ${UI_SRCS}
    ${TRON_SRCS}
    ${PROTO_SRCS}
    ${VPARSER_SRC}
    ${SHADER_RESOURCES}
    ${GAME_RESOURCES}
    ${INCLUDED_RESOURCES}
    ${APP_ICON_FILES}
    "${LAUNCH_SCREEN_STORYBOARD}"
)

# ============================================================================
# Xcode app bundle properties
# ============================================================================
set_target_properties(armagetronad PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST    "${REPO_ROOT}/ios/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME   "Armagetron Advanced"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.armagetronad.armagetronad"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    XCODE_ATTRIBUTE_PRODUCT_NAME "armagetronad"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"  # iPhone + iPad
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "9SAQP53S98"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
)

# ============================================================================
# Include directories
# ============================================================================
target_include_directories(armagetronad PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"           # aa_config.h
    "${PROTO_GEN_DIR}"                      # *.pb.h

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
    "${IOS_CMAKE_DIR}"                      # tUniversalVariables.h
)

# ============================================================================
# Compile definitions
# ============================================================================
target_compile_definitions(armagetronad PRIVATE
    HAVE_CONFIG_H
    DONTUSEMEMMANAGER
    BR_PTHREAD
    ENABLE_BINRELOC
    "TOP_SOURCE_DIR=\".\""
)

# ============================================================================
# MoltenVK — Vulkan on iOS via Metal
# ============================================================================
# MoltenVK — locate the xcframework (Xcode handles simulator/device slice selection).
# CMake 3.21+ can link xcframeworks directly; Xcode picks the correct slice at build time.
set(_moltenvk_xcfw_candidates
    "${MOLTENVK_DIR}/MoltenVK.xcframework"
    "$ENV{VULKAN_SDK}/../MoltenVK/MoltenVK.xcframework"
    "/usr/local/lib/MoltenVK.xcframework"
)
set(MOLTENVK_XCFRAMEWORK "")
foreach(_candidate ${_moltenvk_xcfw_candidates})
    if(EXISTS "${_candidate}/Info.plist")
        set(MOLTENVK_XCFRAMEWORK "${_candidate}")
        break()
    endif()
endforeach()

if(NOT MOLTENVK_XCFRAMEWORK)
    message(WARNING
        "MoltenVK.xcframework not found. Set MOLTENVK_DIR to its parent directory.\n"
        "Download from: https://github.com/KhronosGroup/MoltenVK/releases\n"
        "or from the LunarG Vulkan SDK (installs to /usr/local/lib/MoltenVK.xcframework).")
else()
    message(STATUS "Found MoltenVK.xcframework: ${MOLTENVK_XCFRAMEWORK}")
endif()

# ============================================================================
# Link libraries
# ============================================================================
target_link_libraries(armagetronad
    SDL3::SDL3
    LibXml2
    protobuf::libprotobuf
    glm::glm
)

# iOS system frameworks + MoltenVK (SDK-conditional slice selection via Xcode build settings).
# XCODE_ATTRIBUTE_OTHER_LDFLAGS overrides what CMake adds from target_link_libraries for
# -framework flags, so we include everything here together.
set(_ios_frameworks
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework IOSurface"
    "-framework UIKit"
    "-framework Foundation"
    "-framework CoreFoundation"
    "-framework AVFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreMotion"
    "-framework GameController"
    "-framework CoreGraphics"
)
list(JOIN _ios_frameworks " " _ios_frameworks_str)

if(MOLTENVK_XCFRAMEWORK)
    set(_molvk_device    "${MOLTENVK_XCFRAMEWORK}/ios-arm64/libMoltenVK.a")
    set(_molvk_simulator "${MOLTENVK_XCFRAMEWORK}/ios-arm64_x86_64-simulator/libMoltenVK.a")
    set_target_properties(armagetronad PROPERTIES
        "XCODE_ATTRIBUTE_OTHER_LDFLAGS[sdk=iphoneos*]"
            "$(inherited) ${_ios_frameworks_str} ${_molvk_device}"
        "XCODE_ATTRIBUTE_OTHER_LDFLAGS[sdk=iphonesimulator*]"
            "$(inherited) ${_ios_frameworks_str} ${_molvk_simulator}"
    )
else()
    set_target_properties(armagetronad PROPERTIES
        XCODE_ATTRIBUTE_OTHER_LDFLAGS "$(inherited) ${_ios_frameworks_str}"
    )
endif()

# MoltenVK / Vulkan headers from LunarG SDK install.
# IMPORTANT: the local abseil source MUST come before /usr/local/include so
# that absl/base/options.h is picked up from the local source (inline namespace
# disabled) rather than from the system installation (inline namespace enabled
# as lts_20250512).  A mismatch causes "undefined symbol absl::lts_20250512::*"
# linker errors even when the library is correct.
target_include_directories(armagetronad PRIVATE
    "${_absl_src}"               # local abseil — shadows /usr/local/include/absl
    "/usr/local/include"         # Vulkan headers (vulkan/vulkan.h)
)

# ============================================================================
# miniaudio requires Objective-C++ on Apple (includes AVFoundation headers)
# ============================================================================
set_source_files_properties(
    "${SRC}/engine/sound/eAudioMiniaudio.cpp"
    PROPERTIES COMPILE_FLAGS "-x objective-c++"
)

# ============================================================================
# Compiler flags
# ============================================================================
# ============================================================================
# Copy moviepacks and cockpits into the app bundle
# ============================================================================
add_custom_command(TARGET armagetronad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:armagetronad>/moviepacks"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:armagetronad>/cockpits"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO_ROOT}/ios/app/assets/moviepacks" "$<TARGET_BUNDLE_DIR:armagetronad>/moviepacks"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO_ROOT}/ios/app/assets/cockpits" "$<TARGET_BUNDLE_DIR:armagetronad>/cockpits"
    COMMENT "Copying moviepacks and cockpits to app bundle"
)

target_compile_options(armagetronad PRIVATE
    -Wall
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-float-conversion
    -Wno-implicit-float-conversion
    -Wno-implicit-int-float-conversion
    -fexceptions
    -frtti
)
