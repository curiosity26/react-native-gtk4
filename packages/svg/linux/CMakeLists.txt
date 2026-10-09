# react-native-svg on Linux: its RNSVG* native components. An <Svg> is one
# widget; the elements inside it (G, Path, Rect, gradients, clip paths,
# masks, patterns, text, filters...) are turned back into an SVG document,
# which librsvg draws.
cmake_minimum_required(VERSION 3.22)
project(rngtk_svg LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RNGTK_RSVG REQUIRED IMPORTED_TARGET librsvg-2.0>=2.52)

add_library(rngtk_svg STATIC
  src/SvgDocument.cc
  src/SvgComponents.cc
  src/SvgViewModule.cc)
target_include_directories(rngtk_svg PRIVATE src)
target_link_libraries(rngtk_svg PRIVATE ReactNativeGtk::sdk PkgConfig::RNGTK_RSVG)
