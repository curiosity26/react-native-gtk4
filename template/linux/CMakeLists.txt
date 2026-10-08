# {{name}} for Linux (GTK4), with @curiosity26/react-native-gtk4.
#
# `npx react-native run-linux` builds the React Native host once into a
# shared cache and points this build at it (ReactNativeGtk_DIR). To build by
# hand afterwards:
#   cmake -S linux -B linux/build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#     -DReactNativeGtk_DIR=<cache>/host/<id>/lib/cmake/ReactNativeGtk
#   cmake --build linux/build/Debug
cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  set(CMAKE_CXX_COMPILER clang++)
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

project({{name}} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ReactNativeGtk REQUIRED)

add_executable({{name}} main.cc)
target_link_libraries({{name}} PRIVATE ReactNativeGtk::host)
# Copies the host libraries next to the executable. Release builds also get
# index.bundle.js and the assets there (run-linux --release bundles them).
rngtk_app({{name}})
