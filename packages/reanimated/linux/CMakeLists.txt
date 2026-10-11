# react-native-reanimated on Linux: the library's shared C++ engine
# (Common/cpp, from its package in the app's node_modules) and
# ReanimatedModule, on react-native-worklets' UI runtime (packages/worklets).
#
# Reanimated's C++ picks Android's or Apple's way in a few places; Linux
# takes Apple's (synchronous prop updates as folly::dynamic by view tag, the
# mounted props for layout animations), so the sources are built from a copy
# where __APPLE__ reads RNGTK_REANIMATED_APPLE.
cmake_minimum_required(VERSION 3.22)
project(rngtk_reanimated LANGUAGES CXX)

if(NOT TARGET ReactNativeGtk::sdk)
  find_package(ReactNativeGtk REQUIRED)
endif()

find_program(RNGTK_NODE node REQUIRED)
if(NOT RNGTK_REANIMATED_DIR)
  execute_process(
    COMMAND "${RNGTK_NODE}" -p
            "require('path').dirname(require.resolve('react-native-reanimated/package.json', {paths: [process.argv[1]]}))"
            "${CMAKE_SOURCE_DIR}/.."
    OUTPUT_VARIABLE RNGTK_REANIMATED_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(
  COMMAND "${RNGTK_NODE}" -e "
    const path = require('path');
    const dir = process.argv[1], app = process.argv[2];
    const flags = {...require(path.join(dir, 'src/featureFlags/staticFlags.json'))};
    try { Object.assign(flags, require(path.join(app, 'package.json')).reanimated?.staticFeatureFlags); } catch {}
    process.stdout.write(require(path.join(dir, 'package.json')).version + ';' +
      Object.entries(flags).map(([k, v]) => '[' + k + ':' + v + ']').join(''));"
    "${RNGTK_REANIMATED_DIR}" "${CMAKE_SOURCE_DIR}/.."
  OUTPUT_VARIABLE _rngtk_reanimated_info COMMAND_ERROR_IS_FATAL ANY)
list(GET _rngtk_reanimated_info 0 RNGTK_REANIMATED_VERSION)
list(GET _rngtk_reanimated_info 1 RNGTK_REANIMATED_FLAGS)

# The copy, with Apple's branches for Linux (rewritten when a source
# changes).
set(_src "${RNGTK_REANIMATED_DIR}/Common/cpp")
set(_copy "${CMAKE_CURRENT_BINARY_DIR}/cpp")
file(GLOB_RECURSE _files CONFIGURE_DEPENDS RELATIVE "${_src}" "${_src}/reanimated/*.cpp" "${_src}/reanimated/*.h")
set(RNGTK_REANIMATED_SOURCES "")
foreach(_file IN LISTS _files)
  file(READ "${_src}/${_file}" _text)
  string(REPLACE "__APPLE__" "RNGTK_REANIMATED_APPLE" _text "${_text}")
  set(_out "${_copy}/${_file}")
  set(_old "")
  if(EXISTS "${_out}")
    file(READ "${_out}" _old)
  endif()
  if(NOT _old STREQUAL _text)
    file(WRITE "${_out}" "${_text}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_src}/${_file}")
  if(_file MATCHES "\\.cpp$")
    list(APPEND RNGTK_REANIMATED_SOURCES "${_out}")
  endif()
endforeach()

# Codegen for the library's specs (react/renderer/components/rnreanimated:
# the shared element transition boundary's props, which its C++ reads), with
# the app's React Native.
set(_codegen "${CMAKE_CURRENT_BINARY_DIR}/codegen")
file(GLOB _specs CONFIGURE_DEPENDS "${RNGTK_REANIMATED_DIR}/src/specs/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_specs})
execute_process(
  COMMAND "${RNGTK_NODE}" -e "
    const {execFileSync} = require('child_process');
    const fs = require('fs'), path = require('path');
    const [dir, out] = process.argv.slice(1);
    const rn = path.dirname(require.resolve('react-native/package.json', {paths: [dir]}));
    const codegen = path.dirname(require.resolve('@react-native/codegen/package.json', {paths: [rn]}));
    const tmp = fs.mkdtempSync(out + '-tmp');
    const schema = path.join(tmp, 'schema.json');
    execFileSync(process.execPath, [path.join(codegen, 'lib/cli/combine/combine-js-to-schema-cli.js'),
      '--platform', 'android', schema, path.join(dir, 'src/specs')], {stdio: 'inherit'});
    execFileSync(process.execPath, [path.join(rn, 'scripts/generate-specs-cli.js'), '--platform', 'android',
      '--schemaPath', schema, '--outputDir', tmp, '--libraryName', 'rnreanimated',
      '--javaPackageName', 'com.swmansion.reanimated'], {stdio: 'inherit'});
    const from = path.join(tmp, 'jni/react/renderer/components/rnreanimated');
    const to = path.join(out, 'react/renderer/components/rnreanimated');
    fs.mkdirSync(to, {recursive: true});
    for (const f of fs.readdirSync(from)) {
      const text = fs.readFileSync(path.join(from, f));
      const dest = path.join(to, f);
      if (!fs.existsSync(dest) || !fs.readFileSync(dest).equals(text)) fs.writeFileSync(dest, text);
    }
    fs.rmSync(tmp, {recursive: true});"
    "${RNGTK_REANIMATED_DIR}" "${_codegen}"
  COMMAND_ERROR_IS_FATAL ANY)
file(GLOB RNGTK_REANIMATED_CODEGEN_SOURCES "${_codegen}/react/renderer/components/rnreanimated/*.cpp")

add_library(rngtk_reanimated STATIC ${RNGTK_REANIMATED_SOURCES} ${RNGTK_REANIMATED_CODEGEN_SOURCES}
  src/ReanimatedModule.cc)
target_include_directories(rngtk_reanimated PRIVATE "${_copy}" "${_codegen}" compat
  ${CMAKE_CURRENT_SOURCE_DIR}/../../worklets/linux/compat)
target_compile_definitions(rngtk_reanimated PRIVATE
  RNGTK_REANIMATED_APPLE=1
  REANIMATED_VERSION=${RNGTK_REANIMATED_VERSION}
  "REANIMATED_FEATURE_FLAGS=${RNGTK_REANIMATED_FLAGS}")
set_source_files_properties(${RNGTK_REANIMATED_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-include;rngtk_reanimated_compat.h")
target_compile_options(rngtk_reanimated PRIVATE -Wno-unused-parameter)
# Worklets' headers, and its stable API (the UI runtime from JS's holders).
target_link_libraries(rngtk_reanimated PRIVATE ReactNativeGtk::sdk rngtk_worklets)
