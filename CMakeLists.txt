cmake_minimum_required(VERSION 3.22)

# The C language is required by JUCE 9: its vendored zlib/libpng/libjpeg/libflac
# copies are now compiled as C rather than C++.
project(JUCEBoilerplate VERSION 0.1.0 LANGUAGES C CXX)

#===============================================================================
# Project identity. Override any of these with -D... when configuring.
#===============================================================================
set(BOILERPLATE_PRODUCT_NAME "JUCE Boilerplate" CACHE STRING "Plugin product name")
set(BOILERPLATE_COMPANY_NAME "Boilerplate Audio" CACHE STRING "Company/manufacturer name")
set(BOILERPLATE_COMPANY_WEBSITE "https://example.com" CACHE STRING "Company website")
set(BOILERPLATE_COMPANY_EMAIL "dev@example.com" CACHE STRING "Company contact email")
set(BOILERPLATE_BUNDLE_ID "com.boilerplateaudio.juceboilerplate" CACHE STRING "Bundle identifier")
set(BOILERPLATE_PLUGIN_MANUFACTURER_CODE "Wckm" CACHE STRING "Four-character manufacturer code")
set(BOILERPLATE_PLUGIN_CODE "Jbpl" CACHE STRING "Four-character plugin code")
set(BOILERPLATE_JUCE_VERSION "9.0.4" CACHE STRING "JUCE git tag fetched when no local checkout is supplied")

# Point this at an existing JUCE 9 checkout to skip the FetchContent download.
set(BOILERPLATE_JUCE_SOURCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

option(BOILERPLATE_ENABLE_LTO "Enable link-time optimisation (slow, memory-heavy)" OFF)
option(BOILERPLATE_COPY_PLUGIN_AFTER_BUILD "Install plugins to the user plugin folders after building" OFF)
option(BOILERPLATE_LIMIT_PARALLEL_JOBS "Cap parallel compile jobs to avoid OOM during JUCE module builds" ON)
option(BOILERPLATE_WINDOWS_MIDI_SERVICES "Use Windows MIDI Services (Windows only; forces C++20)" OFF)
option(BOILERPLATE_IS_SYNTH "Build an output-only instrument scaffold with MIDI input" OFF)
option(BOILERPLATE_BUILD_TESTS "Build behavioral and editor lifecycle tests" OFF)
set(BOILERPLATE_MAX_PARALLEL_JOBS 6 CACHE STRING "Maximum parallel compile jobs when limiting is enabled")

if(BOILERPLATE_WINDOWS_MIDI_SERVICES)
    set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard (use 20 for Windows MIDI Services)" FORCE)
endif()

#===============================================================================
# Language settings
#===============================================================================
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard (use 20 for Windows MIDI Services)")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# JUCE compiles a large number of translation units. Uncapped -j builds on
# many-core machines can exhaust RAM and get the compiler OOM-killed.
include(ProcessorCount)
ProcessorCount(_boilerplate_cpu_count)

if(_boilerplate_cpu_count EQUAL 0)
    set(_boilerplate_cpu_count 4)
endif()

if(BOILERPLATE_LIMIT_PARALLEL_JOBS AND _boilerplate_cpu_count GREATER BOILERPLATE_MAX_PARALLEL_JOBS)
    set(_boilerplate_compile_jobs ${BOILERPLATE_MAX_PARALLEL_JOBS})
else()
    set(_boilerplate_compile_jobs ${_boilerplate_cpu_count})
endif()

# Ninja honours job pools; other generators are capped on the command line
# (see README.md and CMakePresets.json).
if(CMAKE_GENERATOR MATCHES "Ninja" AND BOILERPLATE_LIMIT_PARALLEL_JOBS)
    set(CMAKE_JOB_POOLS "compile=${_boilerplate_compile_jobs}")
    set(CMAKE_JOB_POOL_COMPILE compile)
endif()

message(STATUS "JUCE Boilerplate: max parallel compile jobs = ${_boilerplate_compile_jobs}")

#===============================================================================
# JUCE
#===============================================================================
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

if(BOILERPLATE_JUCE_SOURCE_DIR)
    if(NOT EXISTS "${BOILERPLATE_JUCE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "BOILERPLATE_JUCE_SOURCE_DIR does not contain a JUCE checkout: ${BOILERPLATE_JUCE_SOURCE_DIR}")
    endif()

    add_subdirectory("${BOILERPLATE_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_juce")
else()
    include(FetchContent)

    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${BOILERPLATE_JUCE_VERSION}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)

    FetchContent_MakeAvailable(JUCE)
endif()

#===============================================================================
# Plugin target
#===============================================================================
set(BOILERPLATE_PLUGIN_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND BOILERPLATE_PLUGIN_FORMATS AU)
endif()

if(BOILERPLATE_IS_SYNTH)
    set(_boilerplate_vst3_categories Instrument Synth)
    set(_boilerplate_needs_audio_input FALSE)
else()
    set(_boilerplate_vst3_categories Fx)
    set(_boilerplate_needs_audio_input TRUE)
endif()

juce_add_plugin(JUCEBoilerplate
    COMPANY_NAME "${BOILERPLATE_COMPANY_NAME}"
    COMPANY_WEBSITE "${BOILERPLATE_COMPANY_WEBSITE}"
    COMPANY_EMAIL "${BOILERPLATE_COMPANY_EMAIL}"
    BUNDLE_ID "${BOILERPLATE_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE ${BOILERPLATE_PLUGIN_MANUFACTURER_CODE}
    PLUGIN_CODE ${BOILERPLATE_PLUGIN_CODE}
    PRODUCT_NAME "${BOILERPLATE_PRODUCT_NAME}"
    PLUGIN_NAME "${BOILERPLATE_PRODUCT_NAME}"
    DESCRIPTION "Scalable JUCE 9 audio plugin boilerplate"
    FORMATS ${BOILERPLATE_PLUGIN_FORMATS}
    IS_SYNTH ${BOILERPLATE_IS_SYNTH}
    NEEDS_MIDI_INPUT ${BOILERPLATE_IS_SYNTH}
    NEEDS_MIDI_OUTPUT FALSE
    MICROPHONE_PERMISSION_ENABLED ${_boilerplate_needs_audio_input}
    MICROPHONE_PERMISSION_TEXT "Audio input is required to process incoming audio."
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${BOILERPLATE_COPY_PLUGIN_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    NEEDS_WINDOWS_MIDI_SERVICES ${BOILERPLATE_WINDOWS_MIDI_SERVICES}
    VST3_AUTO_MANIFEST TRUE
    VST3_CATEGORIES ${_boilerplate_vst3_categories})

target_sources(JUCEBoilerplate
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/GainProcessor.cpp
        Source/DSP/GainProcessor.h
        Source/State/ParameterIDs.h
        Source/State/ParameterLayout.cpp
        Source/State/ParameterLayout.h
        Source/UI/BoilerplateLookAndFeel.cpp
        Source/UI/BoilerplateLookAndFeel.h)

target_include_directories(JUCEBoilerplate PRIVATE Source)

set_target_properties(JUCEBoilerplate PROPERTIES
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

# These are PUBLIC because the generated per-format targets (VST3, AU,
# Standalone) compile the plugin client code and must see the same definitions.
target_compile_definitions(JUCEBoilerplate
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(JUCEBoilerplate
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(BOILERPLATE_ENABLE_LTO)
    target_link_libraries(JUCEBoilerplate PUBLIC juce::juce_recommended_lto_flags)
endif()

if(BOILERPLATE_BUILD_TESTS)
    enable_testing()
    add_executable(BoilerplateTests Tests/BoilerplateTests.cpp)
    target_link_libraries(BoilerplateTests PRIVATE JUCEBoilerplate)
    # Reuse the shared code's module settings/headers without compiling JUCE again.
    target_include_directories(BoilerplateTests PRIVATE
        $<TARGET_PROPERTY:JUCEBoilerplate,INCLUDE_DIRECTORIES>)
    target_compile_definitions(BoilerplateTests PRIVATE
        $<TARGET_PROPERTY:JUCEBoilerplate,COMPILE_DEFINITIONS>)
    add_test(NAME audio-behavior COMMAND BoilerplateTests)
    add_test(NAME editor-lifecycle COMMAND BoilerplateTests --editor)
    set_tests_properties(audio-behavior editor-lifecycle PROPERTIES TIMEOUT 60)
endif()
