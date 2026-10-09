# The Showcase as an app of its own (what `init-linux` makes), for
# `react-native package-linux` (see docs/packaging.md). The repository's
# rn-gtk-host harness runs it too (npm run dev:showcase).
cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  set(CMAKE_CXX_COMPILER clang++)
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

project(Showcase LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ReactNativeGtk REQUIRED)

add_executable(Showcase main.cc)
target_link_libraries(Showcase PRIVATE ReactNativeGtk::host)
rngtk_app(Showcase)
