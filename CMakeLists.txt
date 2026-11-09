# Ferrum 8 - an 8-channel drum synthesizer: LXR-02 sound engine, Microtonic-style workflow.
# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.22)
project(Ferrum8 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# JUCE: use a local checkout if FERRUM_JUCE_DIR is given, otherwise download the pinned version.
set(FERRUM_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(FERRUM_JUCE_DIR)
    add_subdirectory(${FERRUM_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 454fe891acc8204f99f4ef1e3a28c84b1a4da145 # JUCE 9.0.4
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Copy the VST3 into the system plug-in folder after building (needs admin rights on Windows)
option(FERRUM_COPY_PLUGIN "Copy the VST3 to the system VST3 folder after building" OFF)
# Linux builds without ALSA / Xrandr headers (used for the headless test build)
option(FERRUM_MINIMAL_LINUX_DEPS "Disable optional Linux system dependencies" OFF)
option(FERRUM_LTO "Link-time optimisation for Release builds" ON)
option(FERRUM_BUILD_TESTS "Build the headless test / screenshot tool" OFF)

juce_add_plugin(Ferrum8
    PRODUCT_NAME "Ferrum 8"
    COMPANY_NAME "Ferrum"
    BUNDLE_ID "audio.ferrum.ferrum8"
    PLUGIN_MANUFACTURER_CODE Frrm
    PLUGIN_CODE Fe08
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Drum
    COPY_PLUGIN_AFTER_BUILD ${FERRUM_COPY_PLUGIN})

juce_add_binary_data(Ferrum8Data SOURCES
    Resources/fonts/BarlowCondensed-Medium.ttf
    Resources/fonts/BarlowCondensed-SemiBold.ttf)

set(FERRUM_SOURCES
    Source/dsp/LxrTables.cpp
    Source/dsp/LxrVoice.cpp
    Source/dsp/FxBus.cpp
    Source/dsp/PatchParams.cpp
    Source/fx/FxRegistry.cpp
    Source/fx/FxRack.cpp
    Source/fx/FxDrive.cpp
    Source/fx/FxDynamics.cpp
    Source/fx/FxSpace.cpp
    Source/fx/FxLxr.cpp
    Source/fx/FxChains.cpp
    Source/engine/Engine.cpp
    Source/engine/Modulation.cpp
    Source/engine/Groove.cpp
    Source/engine/Randomizer.cpp
    Source/engine/Kits.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/Controls.cpp
    Source/ui/PatchPanel.cpp
    Source/ui/PatternPanel.cpp
    Source/ui/TopPanels.cpp
    Source/ui/DicePanel.cpp
    Source/ui/FxRackPanel.cpp
    Source/ui/ModPanel.cpp
    Source/ui/Tutorial.cpp
    Source/ui/ModStrip.cpp
    Source/ui/FxMorph.cpp)
target_sources(Ferrum8 PRIVATE ${FERRUM_SOURCES})

target_compile_definitions(Ferrum8 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(FERRUM_MINIMAL_LINUX_DEPS AND UNIX AND NOT APPLE)
    target_compile_definitions(Ferrum8 PUBLIC
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
endif()

target_link_libraries(Ferrum8
    PRIVATE
        Ferrum8Data
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)
if(FERRUM_LTO)
    target_link_libraries(Ferrum8 PUBLIC juce::juce_recommended_lto_flags)
endif()

if(MSVC)
    target_compile_options(Ferrum8 PRIVATE /bigobj)
endif()

if(FERRUM_BUILD_TESTS)
    juce_add_console_app(Ferrum8Tests PRODUCT_NAME "Ferrum8Tests")
    target_sources(Ferrum8Tests PRIVATE tests/plugin_tests.cpp ${FERRUM_SOURCES})
    target_compile_definitions(Ferrum8Tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
    target_link_libraries(Ferrum8Tests PRIVATE Ferrum8Data juce::juce_audio_utils juce::juce_audio_formats
                          juce::juce_recommended_config_flags)
endif()
