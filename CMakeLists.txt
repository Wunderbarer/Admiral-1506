cmake_minimum_required(VERSION 3.22)

# Laufzeitbibliothek statisch einbinden: das Plugin läuft damit ohne installierte VC++-Redistributables.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

# macOS: Universal-Binary (Apple Silicon + Intel), ab macOS 11. Muss vor project() gesetzt werden.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(Admiral1506 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ADMIRAL_BUILD_TESTS "Offline-Test des Kerns (core_test) mitbauen" OFF)
option(ADMIRAL_COPY_PLUGIN "VST3 nach dem Bauen automatisch in den System-VST3-Ordner kopieren" OFF)
set(ADMIRAL_JUCE_DIR "" CACHE PATH "Optional: lokaler JUCE-Ordner statt Download")

if(ADMIRAL_JUCE_DIR)
    add_subdirectory(${ADMIRAL_JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Plugin-Formate: Windows VST3 + Standalone; macOS zusätzlich Audio Unit (AU)
if(APPLE)
    set(ADMIRAL_FORMATS VST3 AU Standalone)
elseif(DEFINED ADMIRAL_FORMATS_OVERRIDE)
    set(ADMIRAL_FORMATS ${ADMIRAL_FORMATS_OVERRIDE})
else()
    set(ADMIRAL_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Admiral1506
    COMPANY_NAME                "CSSynth"
    PRODUCT_NAME                "Admiral 1506"
    BUNDLE_ID                   "com.cssynth.admiral1506"
    PLUGIN_MANUFACTURER_CODE    Csyn
    PLUGIN_CODE                 A506
    FORMATS                     ${ADMIRAL_FORMATS}
    VST3_CATEGORIES             Instrument Drum
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${ADMIRAL_COPY_PLUGIN})

# Eingebettete Daten: Schriftarten (SIL Open Font License, siehe Resources/Fonts/OFL-Licenses.txt)
# und die per Klangsynthese erzeugten Kits (tools/make_kits.py)
file(GLOB ADMIRAL_KIT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Kits/*.wav)
juce_add_binary_data(AdmiralData SOURCES
    Resources/Fonts/FontTitle.ttf
    Resources/Fonts/FontModel.ttf
    Resources/Fonts/FontLabel.ttf
    Resources/Fonts/FontLabelBold.ttf
    ${ADMIRAL_KIT_FILES})
set_target_properties(AdmiralData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Admiral1506 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Core/Generator.cpp
    Source/Core/DrumEngine.cpp
    Source/Core/KitDesigner.cpp)

target_include_directories(Admiral1506 PRIVATE Source)

target_compile_definitions(Admiral1506 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1   # MP3-Dateien als Samples
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(Admiral1506 PRIVATE /utf-8)
endif()

target_link_libraries(Admiral1506
    PRIVATE
        AdmiralData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(ADMIRAL_BUILD_TESTS)
    add_executable(core_test tests/core_test.cpp Source/Core/Generator.cpp Source/Core/DrumEngine.cpp Source/Core/KitDesigner.cpp)
    target_include_directories(core_test PRIVATE Source)
    if(MSVC)
        target_compile_options(core_test PRIVATE /utf-8)
    endif()
endif()

# Test des kompletten Plugins (Audio, MIDI, Zustand, Oberfläche) ohne DAW
option(ADMIRAL_BUILD_PLUGIN_TEST "plugin_test mitbauen" OFF)
if(ADMIRAL_BUILD_PLUGIN_TEST)
    add_executable(plugin_test tests/plugin_test.cpp)
    target_include_directories(plugin_test PRIVATE Source)
    target_link_libraries(plugin_test PRIVATE Admiral1506 AdmiralData
        juce::juce_audio_utils juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(plugin_test PRIVATE /utf-8)
    endif()
endif()
