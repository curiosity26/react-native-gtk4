cmake_minimum_required(VERSION 3.22)
project(react_native_gtk4_spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
# GTK 4.14 is the floor (Ubuntu 24.04 / Linux Mint 22.3).
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.14)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
find_package(Threads REQUIRED)

# The host widgets live in linux/widgets; the spike benchmarks them.
add_library(rngtk_spike STATIC
  ../linux/widgets/rn_view.cc
  ../linux/widgets/rn_text.cc
  src/harness.cc)
target_include_directories(rngtk_spike PUBLIC src ../linux/widgets)
target_compile_options(rngtk_spike PRIVATE -Wall -Wextra)
# GTK 4.14 API only; newer features must be runtime-checked.
target_compile_definitions(rngtk_spike PUBLIC
  GDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14
  GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14)
target_link_libraries(rngtk_spike PUBLIC
  PkgConfig::GTK4 PkgConfig::PANGOCAIRO Threads::Threads)

add_executable(hello-world apps/hello_world.cc)
target_link_libraries(hello-world PRIVATE rngtk_spike)

add_executable(widget-benchmark apps/widget_benchmark.cc)
target_link_libraries(widget-benchmark PRIVATE rngtk_spike)

enable_testing()
add_test(NAME hello-world-self-test COMMAND hello-world --self-test)
add_test(NAME widget-benchmark-smoke
         COMMAND widget-benchmark --count 1000 --frames 30)
add_test(NAME widget-benchmark-fixed-smoke
         COMMAND widget-benchmark --count 1000 --frames 30 --mode fixed)
