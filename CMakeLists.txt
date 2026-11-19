cmake_minimum_required(VERSION 3.22)

# Must come BEFORE project(), or CMake ignores them: universal (Apple Silicon + Intel), macOS 11+
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(LOWBLOW VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same JUCE version as the rest of the Castle Danger plugins.
# (Pass -DJUCE_LOCAL_DIR=/path/to/JUCE to build against a local checkout.)
if(JUCE_LOCAL_DIR)
    add_subdirectory(${JUCE_LOCAL_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT LOWBLOW_TESTS_ONLY)
juce_add_plugin(LowBlow
    COMPANY_NAME "Castle Danger"
    PRODUCT_NAME "Low Blow"
    BUNDLE_ID com.castledanger.lowblow
    PLUGIN_MANUFACTURER_CODE Cdgr
    PLUGIN_CODE Lwb1
    FORMATS VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(LowBlow PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(LowBlow PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(LowBlow
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# Headless checks: renders the editor to PNGs and tests the DSP + presets + link
juce_add_console_app(LowBlowSnapshot PRODUCT_NAME "LowBlowSnapshot")
target_sources(LowBlowSnapshot PRIVATE test/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(LowBlowSnapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_ALSA=0 JUCE_JACK=0
    "JucePlugin_VersionString=\"${PROJECT_VERSION}\"")
target_link_libraries(LowBlowSnapshot PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
