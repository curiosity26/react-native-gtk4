# @react-native-community/netinfo on Linux: the RNCNetInfo TurboModule, on
# GIO's GNetworkMonitor (NetworkManager, or the network portal in a
# Flatpak), with the connection's type and addresses from NetworkManager
# over D-Bus when it's there.
cmake_minimum_required(VERSION 3.22)
project(rngtk_netinfo LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

add_library(rngtk_netinfo STATIC src/NetInfoModule.cc src/Package.cc)
target_include_directories(rngtk_netinfo PRIVATE src)
target_link_libraries(rngtk_netinfo PRIVATE ReactNativeGtk::sdk)
