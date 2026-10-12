# IronClip plug-in targets (VST3, CLAP and, on macOS, AUv2) built with iPlug2.

set(IPLUG2_DIR "${PROJECT_SOURCE_DIR}/external/iPlug2")
if(NOT EXISTS "${IPLUG2_DIR}/iPlug2.cmake")
  message(FATAL_ERROR "iPlug2 is missing. Run: git submodule update --init --depth 1")
endif()

set(IPLUG2_CXX_STANDARD 20)
set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Copy built plug-ins into the system plug-in folders after building")
set(IGRAPHICS_BACKEND "SKIA")
set(IRONCLIP_RENDERER "" CACHE STRING "Skia renderer: METAL (macOS default), GL3 (Windows default) or CPU")
if(IRONCLIP_RENDERER)
  set(IGRAPHICS_RENDERER "${IRONCLIP_RENDERER}")
elseif(APPLE)
  set(IGRAPHICS_RENDERER "METAL")
else()
  set(IGRAPHICS_RENDERER "GL3")
endif()

include("${IPLUG2_DIR}/iPlug2.cmake")
find_package(iPlug2 REQUIRED)

# Fail early with a clear message when the SDKs or Skia have not been fetched.
set(_skia_header "${IPLUG2_DIR}/Dependencies/Build/src/skia/include/core/SkCanvas.h")
if(NOT IPLUG2_VST3_SUPPORTED OR NOT IPLUG2_CLAP_SUPPORTED OR NOT EXISTS "${_skia_header}")
  message(FATAL_ERROR "iPlug2 dependencies are missing (VST3 SDK, CLAP SDK or Skia). Run scripts/bootstrap.sh "
                      "(or scripts/bootstrap.ps1 on Windows) first.")
endif()

# On Windows, iPlug2's CMake expects a locally built Skia (out/Release-x64).
# The prebuilt archive fetched by scripts/bootstrap.ps1 instead provides
# Build/win/x64/<config>/*.lib, the layout iPlug2's Visual Studio property
# sheets use: IGraphicsSkia.cpp pulls most libraries in via #pragma comment,
# so they only need to be on the library path; svg.lib is linked explicitly.
if(WIN32)
  set(_skia_win_dir "${IPLUG2_DIR}/Dependencies/Build/win/x64/$<IF:$<CONFIG:Debug>,Debug,Release>")
  get_target_property(_skia_libs iPlug2::IGraphics::Skia INTERFACE_LINK_LIBRARIES)
  list(FILTER _skia_libs EXCLUDE REGEX "out/Release-x64")
  list(APPEND _skia_libs "${_skia_win_dir}/svg.lib")
  set_target_properties(iPlug2::IGraphics::Skia PROPERTIES
    INTERFACE_LINK_LIBRARIES "${_skia_libs}"
    INTERFACE_LINK_DIRECTORIES "${_skia_win_dir}")
endif()

# Skip optional iPlug2 modules IronClip does not use.
foreach(_optional iPlug2_Extras_FlexBox_obj iPlug2_Extras_IWebViewControl_obj)
  if(TARGET ${_optional})
    set_target_properties(${_optional} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()

# Generated sources: version header and embedded fonts
set(IRONCLIP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${IRONCLIP_GENERATED_DIR}")
math(EXPR IRONCLIP_VERSION_INT "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
math(EXPR IRONCLIP_VERSION_HEX "${IRONCLIP_VERSION_INT}" OUTPUT_FORMAT HEXADECIMAL)
configure_file(IronClipVersion.h.in "${IRONCLIP_GENERATED_DIR}/IronClipVersion.h" @ONLY)

include("${PROJECT_SOURCE_DIR}/cmake/EmbedFonts.cmake")
set(_fonts "${PROJECT_SOURCE_DIR}/resources/fonts")
ironclip_embed_fonts("${IRONCLIP_GENERATED_DIR}"
  "FontMedium=${_fonts}/BarlowSemiCondensed-Medium.ttf"
  "FontSemiBold=${_fonts}/BarlowSemiCondensed-SemiBold.ttf"
  "FontBold=${_fonts}/BarlowSemiCondensed-Bold.ttf")

set(_ui "${PROJECT_SOURCE_DIR}/src/ui")
iplug_add_plugin(IronClip
  SOURCES
    IronClip.cpp
    IronClip.h
    config.h
    ${_ui}/Controls.cpp
    ${_ui}/Controls.h
    ${_ui}/Editor.cpp
    ${_ui}/Editor.h
    ${_ui}/Theme.cpp
    ${_ui}/Theme.h
    "${IRONCLIP_GENERATED_DIR}/EmbeddedFonts.cpp"
  FORMATS VST3 CLAP AU
  LINK ironclip-dsp
  DEFINES SAMPLE_TYPE_FLOAT IGRAPHICS_NO_SKIA_SKPARAGRAPH
)

target_include_directories(_IronClip-base INTERFACE
  "${PROJECT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${IRONCLIP_GENERATED_DIR}")

set(IRONCLIP_PLUGIN_TARGETS IronClip-vst3 IronClip-clap)
if(APPLE)
  list(APPEND IRONCLIP_PLUGIN_TARGETS IronClip-au)
endif()
set(IRONCLIP_PLUGIN_TARGETS ${IRONCLIP_PLUGIN_TARGETS} PARENT_SCOPE)

if(APPLE)
  foreach(_fmt VST3 CLAP AU)
    string(TOLOWER ${_fmt} _lower)
    configure_file("resources/IronClip-${_fmt}-Info.plist.in" "${IRONCLIP_GENERATED_DIR}/IronClip-${_fmt}-Info.plist" @ONLY)
    set_target_properties(IronClip-${_lower} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${IRONCLIP_GENERATED_DIR}/IronClip-${_fmt}-Info.plist")
  endforeach()
endif()

# Drop unreferenced code from the statically linked Skia libraries.
if(APPLE)
  foreach(_target ${IRONCLIP_PLUGIN_TARGETS})
    target_link_options(${_target} PRIVATE "LINKER:-dead_strip")
  endforeach()
endif()

if(MSVC)
  foreach(_target ${IRONCLIP_PLUGIN_TARGETS})
    target_compile_options(${_target} PRIVATE /utf-8)
  endforeach()
endif()
