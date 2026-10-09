# Example's native code for React Native on Linux (GTK4): a TurboModule
# ("Example") and a native component ("RNGtkExampleView", a GtkCalendar).
#
# Apps don't build this by hand: `npx react-native run-linux` autolinks it
# (it adds this folder to the app's build and links the `example` target),
# and calls example_package() (src/ExamplePackage.cc) to register both.
# ReactNativeGtk::sdk brings React Native's headers and the host's flags.
cmake_minimum_required(VERSION 3.22)
project(example LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

add_library(example STATIC
  src/ExampleModule.cc
  src/ExampleView.cc
  src/ExamplePackage.cc)
target_include_directories(example PRIVATE src)
# PRIVATE: the app links the host and GTK through it, but doesn't take
# React Native's headers and flags (NDEBUG as the host's) for its own code.
target_link_libraries(example PRIVATE ReactNativeGtk::sdk)
