# react-native-worklets on Linux: the library's shared C++ (Common/cpp,
# built from its package in the app's node_modules) and WorkletsModule,
# which runs its UI worklet runtime on GTK's main thread.
cmake_minimum_required(VERSION 3.22)
project(rngtk_worklets LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

# react-native-worklets: RNGTK_WORKLETS_DIR, or resolved from the app
# (this file's build runs from the app's linux/ folder).
find_program(RNGTK_NODE node REQUIRED)
if(NOT RNGTK_WORKLETS_DIR)
  execute_process(
    COMMAND "${RNGTK_NODE}" -p
            "require('path').dirname(require.resolve('react-native-worklets/package.json', {paths: [process.argv[1]]}))"
            "${CMAKE_SOURCE_DIR}/.."
    OUTPUT_VARIABLE RNGTK_WORKLETS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif()
# Its version and static feature flags (src/featureFlags/staticFlags.json,
# with the app's package.json "worklets": {"staticFeatureFlags": {...}}), as
# its Android build passes them.
execute_process(
  COMMAND "${RNGTK_NODE}" -e "
    const path = require('path'), fs = require('fs');
    const dir = process.argv[1], app = process.argv[2];
    const flags = {...require(path.join(dir, 'src/featureFlags/staticFlags.json'))};
    try { Object.assign(flags, require(path.join(app, 'package.json')).worklets?.staticFeatureFlags); } catch {}
    process.stdout.write(require(path.join(dir, 'package.json')).version + ';' +
      Object.entries(flags).map(([k, v]) => '[' + k + ':' + v + ']').join(''));"
    "${RNGTK_WORKLETS_DIR}" "${CMAKE_SOURCE_DIR}/.."
  OUTPUT_VARIABLE _rngtk_worklets_info COMMAND_ERROR_IS_FATAL ANY)
list(GET _rngtk_worklets_info 0 RNGTK_WORKLETS_VERSION)
list(GET _rngtk_worklets_info 1 RNGTK_WORKLETS_FLAGS)

file(GLOB_RECURSE RNGTK_WORKLETS_SOURCES CONFIGURE_DEPENDS
     "${RNGTK_WORKLETS_DIR}/Common/cpp/worklets/*.cpp")
add_library(rngtk_worklets STATIC ${RNGTK_WORKLETS_SOURCES} src/WorkletsModule.cc src/PlatformLogger.cc)
set_source_files_properties(${RNGTK_WORKLETS_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/rngtk_worklets_compat.h")
# Other ports (Reanimated) build against worklets' headers too.
target_include_directories(rngtk_worklets PUBLIC "${RNGTK_WORKLETS_DIR}/Common/cpp")
target_compile_definitions(rngtk_worklets PUBLIC
  WORKLETS_VERSION=${RNGTK_WORKLETS_VERSION}
  "WORKLETS_FEATURE_FLAGS=${RNGTK_WORKLETS_FLAGS}")
target_compile_options(rngtk_worklets PRIVATE -Wno-unused-parameter)
target_link_libraries(rngtk_worklets PRIVATE ReactNativeGtk::sdk)
set(RNGTK_WORKLETS_DIR "${RNGTK_WORKLETS_DIR}" CACHE INTERNAL "react-native-worklets")
