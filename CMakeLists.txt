cmake_minimum_required(VERSION 3.25)

# Universal macOS binaries by default; override with -DCMAKE_OSX_ARCHITECTURES=arm64
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(IronClip
  VERSION 0.1.0
  DESCRIPTION "A simple, powerful, no-nonsense hard clipper"
  HOMEPAGE_URL "https://github.com/BTFLV/IronClip"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  # iPlug2 links the static runtime; keep every target consistent.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(IRONCLIP_BUILD_PLUGIN "Build the VST3/CLAP/AU plug-ins (needs iPlug2 dependencies)" ON)
option(IRONCLIP_BUILD_TESTS "Build the DSP tests and benchmark" ON)
option(IRONCLIP_BUILD_TOOLS "Build developer tools (macOS screenshot host)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Build metadata shown in the editor footer
set(IRONCLIP_BUILD_ID "" CACHE STRING "Build identifier (e.g. short commit hash)")
if(NOT IRONCLIP_BUILD_ID)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE IRONCLIP_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  set(IRONCLIP_BUILD_ID "${IRONCLIP_GIT_HASH}")
endif()

function(ironclip_warnings target)
  if(MSVC)
    # C4324: padding added for alignas() is intended.
    target_compile_options(${target} PRIVATE /W4 /permissive- /wd4324)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  endif()
endfunction()

# --- DSP core (no framework dependencies) -------------------------------------
add_library(ironclip-dsp STATIC
  src/dsp/ClipperEngine.cpp
  src/dsp/ClipperEngine.h
  src/dsp/Denormals.h
  src/dsp/Halfband.h
  src/dsp/HalfbandCoefficients.h
  src/dsp/Oversampler.h
  src/dsp/Smoothing.h
  src/dsp/Telemetry.h)
target_include_directories(ironclip-dsp PUBLIC src)
set_target_properties(ironclip-dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
ironclip_warnings(ironclip-dsp)

# --- Tests and benchmark -------------------------------------------------------
if(IRONCLIP_BUILD_TESTS)
  enable_testing()
  add_executable(ironclip-tests tests/DspTests.cpp tests/TestFramework.h)
  target_link_libraries(ironclip-tests PRIVATE ironclip-dsp)
  ironclip_warnings(ironclip-tests)
  add_test(NAME dsp COMMAND ironclip-tests)

  add_executable(ironclip-benchmark tests/Benchmark.cpp)
  target_link_libraries(ironclip-benchmark PRIVATE ironclip-dsp)
  ironclip_warnings(ironclip-benchmark)
endif()

# --- Plug-ins --------------------------------------------------------------------
if(IRONCLIP_BUILD_PLUGIN)
  add_subdirectory(src/plugin)
endif()

# --- Developer tools ---------------------------------------------------------------
if(IRONCLIP_BUILD_TOOLS AND APPLE)
  enable_language(OBJCXX)
  add_executable(ironclip-screenshot tools/screenshot/ClapScreenshot.mm)
  target_include_directories(ironclip-screenshot PRIVATE
    "${PROJECT_SOURCE_DIR}/external/iPlug2/Dependencies/IPlug/CLAP_SDK/include")
  target_compile_options(ironclip-screenshot PRIVATE -fobjc-arc)
  target_link_libraries(ironclip-screenshot PRIVATE "-framework Cocoa")
endif()
