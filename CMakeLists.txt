cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Mac Intel + Apple Silicon), compatible macOS 10.15+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Version minimale de macOS")

project(IDGemsSynth VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Sur Windows : runtime statique (le .vst3 ne dépend d'aucune DLL Visual C++)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# JUCE est téléchargé automatiquement à la première configuration
# (même version qu'IDGems, pour partager les modules sans surprise).
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

option(IDGS_BUILD_TESTS "Compiler les tests de stabilité" ON)
option(IDGS_COPY_AFTER_BUILD "Copier le VST3 dans le dossier système après compilation" OFF)

juce_add_plugin(IDGemsSynth
    COMPANY_NAME                "IDGems"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.idgems.synth"
    PLUGIN_MANUFACTURER_CODE    Idgm
    PLUGIN_CODE                 Idgs
    PRODUCT_NAME                "IDGems Synth"
    FORMATS                     VST3
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Synth
    COPY_PLUGIN_AFTER_BUILD     ${IDGS_COPY_AFTER_BUILD})

set(IDGS_DSP_SOURCES
    Source/Core/Params.cpp
    Source/Synth/Wavetable.cpp
    Source/Synth/SampleData.cpp
    Source/Synth/SynthEngine.cpp
    Source/State/PresetManager.cpp
    Source/State/RandomEngine.cpp
    Source/State/MotionRecorder.cpp
    Source/State/BatchRender.cpp
    Source/Engine/Recorder.cpp
    Source/Modulation/ModSystem.cpp
    Source/Modulation/ModEngine.cpp
    Source/Fx/ParamDefs.cpp
    Source/Fx/ParamBank.cpp
    Source/Fx/Rack.cpp
    Source/Fx/EffectFactory.cpp
    Source/Fx/Sequencer.cpp
    Source/Play/NoteGen.cpp
    Source/PluginProcessor.cpp)

set(IDGS_UI_SOURCES
    Source/PluginEditor.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Theme.cpp
    Source/UI/Widgets.cpp
    Source/UI/SynthPage.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/Graphs.cpp
    Source/UI/ModUI.cpp
    Source/UI/FxPage.cpp
    Source/UI/TexturePage.cpp
    Source/UI/PlayPage.cpp
    Source/UI/RandomUI.cpp
    Source/UI/ExportPage.cpp)

# Police des titres (Michroma, licence SIL OFL : voir Resources/Fonts/OFL.txt)
juce_add_binary_data(IDGemsSynthData SOURCES Resources/Fonts/Michroma-Regular.ttf)
set_target_properties(IDGemsSynthData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(IDGemsSynth PRIVATE ${IDGS_DSP_SOURCES} ${IDGS_UI_SOURCES})
target_include_directories(IDGemsSynth PRIVATE Source)

set(IDGS_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(IDGemsSynth PUBLIC ${IDGS_DEFINITIONS})

# Les sources sont en UTF-8 (textes en français) : indispensable pour Visual Studio
if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(IDGemsSynth PRIVATE /utf-8)
endif()

set(IDGS_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra)

target_link_libraries(IDGemsSynth
    PRIVATE ${IDGS_JUCE_MODULES} IDGemsSynthData
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests de stabilité (exécutable console) : NaN, clics, plafond, CPU...
# ---------------------------------------------------------------------------
if(IDGS_BUILD_TESTS)
    juce_add_console_app(IDGemsSynthTests PRODUCT_NAME "IDGemsSynthTests")
    target_sources(IDGemsSynthTests PRIVATE Tests/StabilityTests.cpp ${IDGS_DSP_SOURCES} ${IDGS_UI_SOURCES})
    target_include_directories(IDGemsSynthTests PRIVATE Source)
    target_compile_definitions(IDGemsSynthTests PRIVATE ${IDGS_DEFINITIONS} IDGS_TESTS=1 JUCE_STANDALONE_APPLICATION=0)
    if(MSVC)
        target_compile_options(IDGemsSynthTests PRIVATE /utf-8)
    endif()
    target_link_libraries(IDGemsSynthTests
        PRIVATE ${IDGS_JUCE_MODULES} IDGemsSynthData
        PUBLIC juce::juce_recommended_config_flags)
endif()
