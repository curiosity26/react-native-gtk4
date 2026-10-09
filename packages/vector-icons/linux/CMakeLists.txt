# Icon fonts for react-native-vector-icons on Linux. iOS and Android ship
# the fonts in the app bundle; here they go next to the executable
# (<build>/fonts, installed into share/<app>/fonts) and
# rngtk_vector_icons_package() registers them with fontconfig, so a Text's
# fontFamily (the font's PostScript name, as on iOS) finds them.
#
# The fonts: every .ttf/.otf of react-native-vector-icons (Fonts/) and of
# the @react-native-vector-icons/* packages (fonts/) in the app's
# node_modules, and the files RNGTK_VECTOR_ICONS_FONTS lists.
cmake_minimum_required(VERSION 3.22)
project(rngtk_vector_icons LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RNGTK_FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig freetype2)

# The app's root: its linux/ is the top-level CMake project.
get_filename_component(_app_root "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
file(GLOB _fonts
  "${_app_root}/node_modules/react-native-vector-icons/Fonts/*.ttf"
  "${_app_root}/node_modules/react-native-vector-icons/Fonts/*.otf"
  "${_app_root}/node_modules/@react-native-vector-icons/*/fonts/*.ttf"
  "${_app_root}/node_modules/@react-native-vector-icons/*/fonts/*.otf")
list(APPEND _fonts ${RNGTK_VECTOR_ICONS_FONTS})
list(LENGTH _fonts _count)
message(STATUS "react-native-vector-icons: ${_count} font(s)")

# Next to the executable (Debug and Release builds run from there).
set(_dir "${CMAKE_BINARY_DIR}/fonts")
file(REMOVE_RECURSE "${_dir}")
file(MAKE_DIRECTORY "${_dir}")
if(_fonts)
  file(COPY ${_fonts} DESTINATION "${_dir}")
endif()
# And with the bundle when the app is installed (package-linux).
install(DIRECTORY "${_dir}" DESTINATION "share/${CMAKE_PROJECT_NAME}")

add_library(rngtk_vector_icons STATIC src/VectorIcons.cc)
target_link_libraries(rngtk_vector_icons PRIVATE ReactNativeGtk::sdk PkgConfig::RNGTK_FONTCONFIG)
