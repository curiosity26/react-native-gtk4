# @react-native-async-storage/async-storage on Linux: the RNAsyncStorage
# TurboModule, storing each database as a JSON file under
# $XDG_DATA_HOME/<app id>/async-storage/.
cmake_minimum_required(VERSION 3.22)
project(rngtk_async_storage LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

add_library(rngtk_async_storage STATIC
  src/AsyncStorageModule.cc
  src/Store.cc
  src/Package.cc)
target_include_directories(rngtk_async_storage PRIVATE src)
target_link_libraries(rngtk_async_storage PRIVATE ReactNativeGtk::sdk)
