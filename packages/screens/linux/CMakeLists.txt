# react-native-screens on Linux: RNSScreenStack on libadwaita's
# AdwNavigationView, RNSScreen in an AdwToolbarView with an AdwHeaderBar,
# and the header config, subviews and screen container. Needs libadwaita
# 1.4 or later (libadwaita-1-dev on Debian and Ubuntu, libadwaita-devel on
# Fedora; in the GNOME runtime for Flatpaks).
cmake_minimum_required(VERSION 3.22)
project(rngtk_screens LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RNGTK_ADW REQUIRED IMPORTED_TARGET libadwaita-1>=1.4)

add_library(rngtk_screens STATIC src/Screens.cc)
target_link_libraries(rngtk_screens PRIVATE ReactNativeGtk::sdk PkgConfig::RNGTK_ADW)
# Its interface: the package function's caller links libadwaita too.
target_link_libraries(rngtk_screens INTERFACE PkgConfig::RNGTK_ADW)
