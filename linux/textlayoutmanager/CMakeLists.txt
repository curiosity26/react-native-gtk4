# Replaces React Native's react_renderer_textlayoutmanager target: the shared
# measure cache from ReactCommon plus a Pango TextLayoutManager.
include(${REACT_COMMON_DIR}/cmake-utils/react-native-flags.cmake)

set(RN_TLM_DIR ${REACT_COMMON_DIR}/react/renderer/textlayoutmanager)

add_library(react_renderer_textlayoutmanager OBJECT
  ${RN_TLM_DIR}/TextMeasureCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/react/renderer/textlayoutmanager/TextLayoutManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PangoText.cc)

target_include_directories(react_renderer_textlayoutmanager
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${RN_TLM_DIR}
    ${REACT_COMMON_DIR})

target_link_libraries(react_renderer_textlayoutmanager
  PUBLIC PkgConfig::PANGO
  glog
  folly_runtime
  react_debug
  react_renderer_attributedstring
  react_renderer_componentregistry
  react_renderer_core
  react_renderer_debug
  react_renderer_graphics
  react_renderer_mapbuffer
  react_renderer_mounting
  react_renderer_telemetry
  react_utils
  yoga)

target_compile_reactnative_options(react_renderer_textlayoutmanager PRIVATE)
