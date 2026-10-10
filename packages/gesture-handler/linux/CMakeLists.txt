# react-native-gesture-handler on Linux: its recognizers (a port of the
# library's web implementation), fed by the host's pointer input, the
# RNGestureHandlerModule TurboModule and the RNGestureHandlerDetector.
cmake_minimum_required(VERSION 3.22)
project(rngtk_gesture_handler LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

add_library(rngtk_gesture_handler STATIC
  src/GestureHandler.cc
  src/Handlers.cc
  src/Orchestrator.cc
  src/GestureHandlerModule.cc)
target_include_directories(rngtk_gesture_handler PRIVATE src)
target_link_libraries(rngtk_gesture_handler PRIVATE ReactNativeGtk::sdk)
