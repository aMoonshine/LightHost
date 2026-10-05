cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 1.2.0 LANGUAGES C CXX)

if (NOT MSVC)
    message(FATAL_ERROR "Light Host currently builds on Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE lives in lib/ which is gitignored. It is the only third-party dependency:
# the VST3 headers and the ASIO SDK are both bundled inside JUCE itself.
set(LIGHT_HOST_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/JUCE"
    CACHE PATH "Path to the JUCE source tree")

if (NOT EXISTS "${LIGHT_HOST_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${LIGHT_HOST_JUCE_DIR}. Expected the JUCE 8 source tree there "
        "(the folder containing modules/ and extras/).")
endif()

add_subdirectory("${LIGHT_HOST_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

# LIGHT_HOST_CONSOLE=ON builds a console-subsystem binary instead, so that DBG output and
# any startup failure are visible in a terminal. Off by default: the shipped app is a
# tray app and must not flash a console window.
if (LIGHT_HOST_CONSOLE)
    juce_add_console_app(LightHost
        PRODUCT_NAME          "Light Host Console"
        COMPANY_NAME          "Rolando Islas"
        BUNDLE_ID             "com.rolandoislas.lighthost"
        VERSION               ${PROJECT_VERSION}
        ICON_BIG              "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
        NEEDS_CURL            FALSE
        NEEDS_WEB_BROWSER     FALSE
        NEEDS_WEBVIEW2        FALSE)
else()
    juce_add_gui_app(LightHost
        PRODUCT_NAME          "Light Host"
        COMPANY_NAME          "Rolando Islas"
        BUNDLE_ID             "com.rolandoislas.lighthost"
        VERSION               ${PROJECT_VERSION}
        ICON_BIG              "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
        NEEDS_CURL            FALSE
        NEEDS_WEB_BROWSER     FALSE
        NEEDS_WEBVIEW2        FALSE)
endif()

target_sources(LightHost PRIVATE
    Source/HostStartup.cpp
    Source/IconMenu.cpp
    Source/PluginWindow.cpp)

target_include_directories(LightHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(LightHost PRIVATE
    JUCE_ASIO=1
    JUCE_DIRECTSOUND=1
    JUCE_WASAPI=1
    JUCE_ALSA=0
    JUCE_JACK=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_CDBURNER=0
    JUCE_USE_CDREADER=0
    JUCE_USE_CAMERA=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    LIGHT_HOST_VERSION="${PROJECT_VERSION}")

juce_add_binary_data(LightHostData
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/menu_icon.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/menu_icon_white.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")

target_link_libraries(LightHost PRIVATE
    LightHostData
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
