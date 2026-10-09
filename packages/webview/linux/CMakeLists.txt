# react-native-webview on Linux: RNCWebView, a WebKitGTK 6.0 WebKitWebView
# (libwebkitgtk-6.0-dev on Debian and Ubuntu, webkitgtk6.0-devel on
# Fedora; in the GNOME runtime for Flatpaks), and RNCWebViewModule.
cmake_minimum_required(VERSION 3.22)
project(rngtk_webview LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RNGTK_WEBKIT REQUIRED IMPORTED_TARGET webkitgtk-6.0)

add_library(rngtk_webview STATIC src/WebView.cc)
target_link_libraries(rngtk_webview PRIVATE ReactNativeGtk::sdk PkgConfig::RNGTK_WEBKIT)
