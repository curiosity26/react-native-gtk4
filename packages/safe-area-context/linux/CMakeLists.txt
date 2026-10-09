# react-native-safe-area-context on Linux: the RNCSafeAreaContext
# TurboModule (initialWindowMetrics). Its components are the package's
# pure-JS ones, which @curiosity26/react-native-gtk4's Metro config picks.
cmake_minimum_required(VERSION 3.22)
project(rngtk_safe_area_context LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

add_library(rngtk_safe_area_context STATIC src/SafeAreaContextModule.cc)
target_link_libraries(rngtk_safe_area_context PRIVATE ReactNativeGtk::sdk)
