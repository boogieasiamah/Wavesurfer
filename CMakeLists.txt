cmake_minimum_required(VERSION 3.22)

# High Sierra (10.13) is the oldest test machine; JUCE supports 10.11+.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Universal binary for Intel + Apple Silicon (override with -DCMAKE_OSX_ARCHITECTURES=x86_64)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
endif()

project(WAVESURFER VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point at a JUCE checkout:  git clone https://github.com/juce-framework/JUCE.git
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_PATH} JUCE)

option(WAVESURFER_HEADLESS_LINUX "Disable optional X11 extensions/ALSA (CI or minimal Linux boxes)" OFF)
option(WAVESURFER_SNAPSHOT_TOOL  "Build the UI snapshot renderer" OFF)

juce_add_binary_data(WaveSurferAssets SOURCES
    Resources/fonts/Inter-Regular.otf
    Resources/fonts/Inter-Medium.otf
    Resources/fonts/Inter-SemiBold.otf)

juce_add_plugin(WaveSurfer
    COMPANY_NAME "Quantum Research Audio"
    BUNDLE_ID com.quantumresearchaudio.wavesurfer
    PLUGIN_MANUFACTURER_CODE Qrau
    PLUGIN_CODE Wsrf
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "WaveSurfer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

set(WAVESURFER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_sources(WaveSurfer PRIVATE ${WAVESURFER_SOURCES})
target_include_directories(WaveSurfer PRIVATE Source)

set(WAVESURFER_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)
if(WAVESURFER_HEADLESS_LINUX)
    list(APPEND WAVESURFER_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XRENDER=0
                                JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()
target_compile_definitions(WaveSurfer PUBLIC ${WAVESURFER_DEFS})

target_link_libraries(WaveSurfer
    PRIVATE
        WaveSurferAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- DSP quality check (no JUCE): every parameter at its extremes ----------
add_executable(WaveSurferStressTest Tests/StressTest.cpp)
target_include_directories(WaveSurferStressTest PRIVATE Source)
if(NOT MSVC)
    target_compile_options(WaveSurferStressTest PRIVATE -O2)
endif()

# ---- optional: render the editor to a PNG without a DAW ---------------------
if(WAVESURFER_SNAPSHOT_TOOL)
    juce_add_console_app(WaveSurferSnapshot PRODUCT_NAME "WaveSurferSnapshot")
    target_sources(WaveSurferSnapshot PRIVATE Tools/Snapshot.cpp ${WAVESURFER_SOURCES})
    target_include_directories(WaveSurferSnapshot PRIVATE Source)
    target_compile_definitions(WaveSurferSnapshot PRIVATE ${WAVESURFER_DEFS}
        JucePlugin_Build_Standalone=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(WaveSurferSnapshot PRIVATE WaveSurferAssets juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# ---- optional: load the built VST3 like a DAW and abuse it -----------------
option(WAVESURFER_HOST_TEST "Build the VST3 host test" OFF)
if(WAVESURFER_HOST_TEST)
    juce_add_console_app(WaveSurferHostTest PRODUCT_NAME "WaveSurferHostTest")
    target_sources(WaveSurferHostTest PRIVATE Tools/HostTest.cpp)
    target_compile_definitions(WaveSurferHostTest PRIVATE ${WAVESURFER_DEFS} JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(WaveSurferHostTest PRIVATE juce::juce_audio_processors juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
