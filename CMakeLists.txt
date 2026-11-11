cmake_minimum_required(VERSION 3.24)

# On the Mac (GitHub Actions) this is configured with -DCMAKE_SYSTEM_NAME=iOS.
# On Linux it builds a desktop test version in a phone-sized window.
project(crimmusic VERSION 1.0 LANGUAGES C CXX)
if(IOS)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ---------------------------------------------------------------- SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.20
    GIT_SHALLOW TRUE)

# ---------------------------------------------------------------- Dear ImGui (no CMake of its own)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)

# ---------------------------------------------------------------- TagLib (reads MP3 tags and covers)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(taglib
    GIT_REPOSITORY https://github.com/taglib/taglib.git
    GIT_TAG v2.3.2
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(SDL3 imgui taglib)

# TagLib's headers live in many sub-folders while it's being built, but the
# code includes them as <taglib/xyz.h> (the installed layout). Copy them into
# that layout once, at configure time.
set(TAGLIB_FLAT "${CMAKE_BINARY_DIR}/taglib-headers/taglib")
file(GLOB_RECURSE TAGLIB_HEADERS "${taglib_SOURCE_DIR}/taglib/*.h" "${taglib_SOURCE_DIR}/taglib/*.tcc" "${taglib_BINARY_DIR}/*.h")
file(MAKE_DIRECTORY "${TAGLIB_FLAT}")
file(COPY ${TAGLIB_HEADERS} DESTINATION "${TAGLIB_FLAT}")

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# ---------------------------------------------------------------- the app
set(APP_SOURCES
    src/main.cpp
    src/UI.cpp
    src/Player.cpp
    src/Library.cpp
    src/CoverCache.cpp
    src/Lyrics.cpp
    src/miniaudio.cpp
    src/stb_impl.cpp)

if(IOS)
    list(APPEND APP_SOURCES
        src/platform/ios/Platform.mm
        src/platform/ios/NowPlaying.mm)
    # miniaudio talks to AVAudioSession on iOS, so it must be compiled as Objective-C++.
    set_source_files_properties(src/miniaudio.cpp PROPERTIES LANGUAGE OBJCXX)
else()
    list(APPEND APP_SOURCES
        src/platform/desktop/Platform.cpp
        src/platform/desktop/NowPlaying.cpp)
endif()

add_executable(crimmusic MACOSX_BUNDLE ${APP_SOURCES})
target_include_directories(crimmusic PRIVATE include external external/stb "${CMAKE_BINARY_DIR}/taglib-headers")
target_link_libraries(crimmusic PRIVATE imgui tag SDL3::SDL3-static)
set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-O2")

if(IOS)
    set_target_properties(crimmusic PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.tgs0180.crimmusic"
        MACOSX_BUNDLE_BUNDLE_NAME "crimmusic"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.tgs0180.crimmusic"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
    target_link_libraries(crimmusic PRIVATE
        "-framework Foundation" "-framework UIKit" "-framework AVFoundation"
        "-framework AudioToolbox" "-framework CoreAudio" "-framework MediaPlayer"
        "-framework UniformTypeIdentifiers")
    set(ASSET_DEST "$<TARGET_BUNDLE_DIR:crimmusic>/assets")
else()
    target_link_libraries(crimmusic PRIVATE pthread dl m)
    set(ASSET_DEST "$<TARGET_FILE_DIR:crimmusic>/assets")
endif()

# Fonts go next to the program (desktop) or inside the .app (iOS).
add_custom_command(TARGET crimmusic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "${ASSET_DEST}/fonts")
