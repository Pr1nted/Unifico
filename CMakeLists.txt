# Unifico -- the Open Doctrines launcher.
#
# SMALL AND FAST, AND THE BUILD IS WHERE THAT IS DECIDED
#
#   * raylib, static, with its audio and 3D modules compiled OUT. A launcher
#     that plays no sound and draws no models has no reason to carry either.
#   * No TLS library of its own. HTTPS goes through the operating system's
#     stack -- WinHTTP on Windows, the system libcurl everywhere else (macOS
#     ships one; every Linux and BSD desktop has one) -- so certificate stores
#     and proxies are the system's, and the binary carries no copy of either.
#   * Fonts and art are embedded, compressed, and subset to what is drawn.
#   * -Os, LTO and stripping in release.
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" UNIFICO_VERSION_RAW)
string(STRIP "${UNIFICO_VERSION_RAW}" UNIFICO_VERSION)
project(Unifico VERSION ${UNIFICO_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# The account service and the keys its achievement grants verify against.
# The same two values the game is built with; see Open Doctrines'
# src/AccountIssuer.h.in. Both are public.
set(UNIFICO_ACCOUNT_ISSUER "https://opendoctrines-net.opendoctrines.workers.dev" CACHE STRING
    "Account service the launcher signs in to")
set(OD_ACHIEVEMENT_KEYS "" CACHE STRING
    "Achievement-grant public keys (base64url raw Ed25519, comma-separated)")
set(UNIFICO_GITHUB_REPO "Pr1nted/Open-Doctrines" CACHE STRING "Where game releases are published")
set(UNIFICO_SELF_REPO "Pr1nted/Unifico" CACHE STRING "Where launcher releases are published")
option(UNIFICO_WITH_DRAGOMAN "Build the map translator (fetches open-dragoman)" ON)
set(UNIFICO_DRAGOMAN_REF "v0.5.2" CACHE STRING "open-dragoman tag to build against")

configure_file(src/BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h" @ONLY)
configure_file(src/gen/AccountIssuer.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/AccountIssuer.h" @ONLY)

include(FetchContent)

# ---- raylib, trimmed ----
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RMODELS OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_GIF OFF CACHE BOOL "" FORCE)
set(SUPPORT_SCREEN_CAPTURE OFF CACHE BOOL "" FORCE)
set(SUPPORT_GIF_RECORDING OFF CACHE BOOL "" FORCE)
set(SUPPORT_TRACELOG ON CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# ---- open-dragoman, for the map translator ----
if(UNIFICO_WITH_DRAGOMAN)
    set(DRAGOMAN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DRAGOMAN_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(DRAGOMAN_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    if(DEFINED ENV{UNIFICO_DRAGOMAN_DIR})
        # A local checkout, for working on both at once.
        add_subdirectory($ENV{UNIFICO_DRAGOMAN_DIR} "${CMAKE_BINARY_DIR}/dragoman")
    else()
        FetchContent_Declare(dragoman
            GIT_REPOSITORY https://github.com/Pr1nted/dragoman.git
            GIT_TAG ${UNIFICO_DRAGOMAN_REF}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(dragoman)
    endif()
endif()

# ---- embedded assets ----
# Every file under assets/ becomes a byte array, compressed where that helps.
# tools/embed.py writes one .c file per asset and an index header; nothing is
# read from disk at run time, so the launcher is one file to download.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE UNIFICO_ASSETS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.ttf"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.jpg"
     "${CMAKE_CURRENT_SOURCE_DIR}/lang/*.json")
set(UNIFICO_EMBED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.c")
add_custom_command(
    OUTPUT "${UNIFICO_EMBED_C}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed.py"
            "${CMAKE_CURRENT_SOURCE_DIR}" "${UNIFICO_EMBED_C}"
    DEPENDS ${UNIFICO_ASSETS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed.py"
    COMMENT "Embedding launcher assets")

set(UNIFICO_SOURCES
    src/main.cpp
    src/App.cpp
    src/core/Paths.cpp
    src/core/Fs.cpp
    src/core/Http.cpp
    src/core/Json.cpp
    src/core/Zip.cpp
    src/core/Process.cpp
    src/core/Jobs.cpp
    src/core/Settings.cpp
    src/core/Log.cpp
    src/core/Sha256.cpp
    src/core/Ed25519.cpp
    src/core/Assets.cpp
    src/core/FileDialog.cpp
    src/ui/Ui.cpp
    src/ui/Text.cpp
    src/ui/Arabic.cpp
    src/ui/Art.cpp
    src/ui/Strings.cpp
    src/od/Releases.cpp
    src/od/Installs.cpp
    src/od/Worlds.cpp
    src/od/Mods.cpp
    src/od/Servers.cpp
    src/od/GameConfig.cpp
    src/od/Account.cpp
    src/od/Achievements.cpp
    src/od/GrantVerify.cpp
    src/od/News.cpp
    src/od/Analytics.cpp
    src/od/Launch.cpp
    src/tools/Translate.cpp
    src/tools/OtherGames.cpp
    src/tools/TempleOS.cpp
    src/update/SelfUpdate.cpp
    src/update/Uninstall.cpp
    src/tabs/PlayTab.cpp
    src/tabs/InstallsTab.cpp
    src/tabs/WorldsTab.cpp
    src/tabs/ModsTab.cpp
    src/tabs/AchievementsTab.cpp
    src/tabs/ServersTab.cpp
    src/tabs/AccountTab.cpp
    src/tabs/SettingsTab.cpp
    src/tabs/ToolsTab.cpp
    src/tabs/ConsolePanel.cpp
    src/tabs/OtherGame.cpp
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
    "${UNIFICO_EMBED_C}"
)

if(WIN32)
    list(APPEND UNIFICO_SOURCES packaging/windows/unifico.rc)
endif()

if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE unifico.icns)
    set(UNIFICO_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/unifico.icns")
    set_source_files_properties(${UNIFICO_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(Unifico MACOSX_BUNDLE ${UNIFICO_SOURCES} ${UNIFICO_ICNS})
    set_target_properties(Unifico PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_VERSION "${UNIFICO_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${UNIFICO_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.opendoctrines.unifico")
elseif(WIN32)
    add_executable(Unifico WIN32 ${UNIFICO_SOURCES})
else()
    add_executable(Unifico ${UNIFICO_SOURCES})
endif()

target_include_directories(Unifico PRIVATE src third_party third_party/miniz
                           "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(Unifico PRIVATE MINIZ_NO_TIME=0)
target_link_libraries(Unifico PRIVATE raylib)
if(UNIFICO_WITH_DRAGOMAN)
    target_link_libraries(Unifico PRIVATE dragoman)
    target_compile_definitions(Unifico PRIVATE UNIFICO_HAS_DRAGOMAN=1)
endif()

if(WIN32)
    target_link_libraries(Unifico PRIVATE winhttp shell32 ole32 uuid comdlg32 psapi ws2_32)
    target_compile_definitions(Unifico PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        # One self-contained exe: no VC++ redistributable to install first.
        set_property(TARGET Unifico PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    find_package(CURL REQUIRED)
    target_link_libraries(Unifico PRIVATE CURL::libcurl)
    find_package(Threads REQUIRED)
    target_link_libraries(Unifico PRIVATE Threads::Threads)
    if(NOT APPLE)
        target_link_libraries(Unifico PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()
if(APPLE)
    target_link_libraries(Unifico PRIVATE "-framework CoreFoundation" "-framework AppKit")
endif()

# ---- release shrink ----
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
    if(ipo_ok)
        set_property(TARGET Unifico PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    if(NOT MSVC)
        target_compile_options(Unifico PRIVATE -Os -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(Unifico PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(Unifico PRIVATE -Wl,--gc-sections -s)
        endif()
    else()
        target_compile_options(Unifico PRIVATE /O1 /Gy)
        target_link_options(Unifico PRIVATE /OPT:REF /OPT:ICF)
    endif()
endif()

# ---- tests (no window) ----
add_executable(UnificoTest EXCLUDE_FROM_ALL
    tests/unifico_test.cpp
    src/core/Fs.cpp src/core/Json.cpp src/core/Zip.cpp src/core/Sha256.cpp src/core/Ed25519.cpp
    src/core/Paths.cpp src/core/Log.cpp src/core/Jobs.cpp
    src/od/GrantVerify.cpp src/od/Worlds.cpp src/od/GameConfig.cpp src/od/Releases.cpp
    src/update/SelfUpdate.cpp src/core/Http.cpp src/core/Process.cpp src/core/Settings.cpp
    third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
target_include_directories(UnificoTest PRIVATE src third_party third_party/miniz
                           "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(UnificoTest PRIVATE UNIFICO_NO_UI=1)
if(WIN32)
    target_link_libraries(UnificoTest PRIVATE winhttp shell32 ole32)
else()
    target_link_libraries(UnificoTest PRIVATE CURL::libcurl Threads::Threads)
endif()
