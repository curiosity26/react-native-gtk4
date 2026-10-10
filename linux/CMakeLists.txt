# The GTK4 host: React Native's C++ core (ReactCommon + ReactCxxPlatform),
# Hermes, and GTK widgets that Fabric mounts into.
#
# Apps don't build this directly: `react-native run-linux` builds it once
# into a shared cache and installs ReactNativeGtk::host (librngtk_host.so),
# which apps link (see template/linux and docs/getting-started.md).
#
# Fetch and build the dependencies first (see docs/building-react-native.md):
#   python3 scripts/fetch-rn-deps.py && scripts/build-hermes.sh
#   cmake -S linux -B build/linux -G Ninja \
#     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build/linux
#
# The target list follows React Native's Fantom tester
# (private/react-native-fantom/tester/CMakeLists.txt), minus its test-only
# modules and with a Pango TextLayoutManager.
cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
  set(CMAKE_C_COMPILER clang)
endif()
if(NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  set(CMAKE_CXX_COMPILER clang++)
endif()
if(NOT CMAKE_BUILD_TYPE)
  # Same as Fantom; matches the Hermes build's debugger setting.
  set(CMAKE_BUILD_TYPE Debug)
endif()

project(react_native_gtk4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -latomic")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -latomic")
# React Native and folly headers assume <cstdint> arrives transitively, which
# libstdc++ 13 no longer does; folly also uses APIs newer libstdc++ deprecates.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdint -Wno-deprecated-declarations")

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "React Native's sources need clang (GCC rejects '#pragma mark' under -Werror)")
endif()

# In this repository (not in the npm package) the self-test harness and
# examples come along.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../spike/src/harness.cc)
  set(RNGTK_IN_REPO ON)
else()
  set(RNGTK_IN_REPO OFF)
endif()

set(RNGTK_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/deps"
    CACHE PATH "Output of scripts/fetch-rn-deps.py and scripts/build-hermes.sh")
get_filename_component(RNGTK_DEPS_DIR "${RNGTK_DEPS_DIR}" ABSOLUTE)
set(RN_PACKAGE_DIR ${RNGTK_DEPS_DIR}/react-native/packages/react-native)
set(FANTOM_TESTER_DIR ${RNGTK_DEPS_DIR}/react-native/private/react-native-fantom/tester)

set(REACT_COMMON_DIR ${RN_PACKAGE_DIR}/ReactCommon)
set(REACT_CXX_PLATFORM_DIR ${RN_PACKAGE_DIR}/ReactCxxPlatform)
set(REACT_ANDROID_DIR ${RN_PACKAGE_DIR}/ReactAndroid)
set(REACT_THIRD_PARTY_NDK_DIR ${RNGTK_DEPS_DIR}/third-party-ndk)
set(FANTOM_THIRD_PARTY_DIR ${RNGTK_DEPS_DIR}/fantom-third-party)
set(FANTOM_CODEGEN_DIR ${RNGTK_DEPS_DIR}/codegen)

if(NOT EXISTS ${REACT_COMMON_DIR})
  message(FATAL_ERROR "React Native sources missing in ${RNGTK_DEPS_DIR}; run scripts/fetch-rn-deps.py")
endif()

list(APPEND CMAKE_MODULE_PATH "${FANTOM_TESTER_DIR}/cmake/modules/")
include(Deps)

find_package(PkgConfig REQUIRED)
# GTK 4.14 is the floor (Ubuntu 24.04 / Linux Mint 22.3).
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.14)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pango>=1.50 pangocairo)
# HTTP and WebSocket for fetch, XMLHttpRequest, WebSocket and Metro.
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0>=3.0)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Hermes, built by scripts/build-hermes.sh with the same compiler.
include_directories(${RNGTK_DEPS_DIR}/hermes-headers)
find_library(LIB_HERMES hermesvm HINTS ${RNGTK_DEPS_DIR}/hermes-build/lib
             NO_DEFAULT_PATH REQUIRED)
add_library(hermes-engine::hermesvm INTERFACE IMPORTED)
set_target_properties(hermes-engine::hermesvm PROPERTIES
  INTERFACE_LINK_LIBRARIES ${LIB_HERMES})

# Third-party C++ libraries
add_subdirectory(${FANTOM_TESTER_DIR}/third-party/boost boost)
add_react_third_party_ndk_subdir(glog)
add_react_third_party_ndk_subdir(double-conversion)
add_react_third_party_ndk_subdir(fast_float)
add_react_third_party_ndk_subdir(fmt)
add_fantom_third_party_subdir(folly)
add_fantom_third_party_subdir(gflags)
add_fantom_third_party_subdir(nlohmann_json)

add_subdirectory(${FANTOM_CODEGEN_DIR} codegen)

add_library(glog_init INTERFACE)

# ReactCommon
set(RNGTK_REACT_COMMON_SUBDIRS
  callinvoker
  cxxreact
  devtoolsruntimesettings
  hermes/executor
  hermes/inspector-modern
  jserrorhandler
  jsi
  jsiexecutor
  jsinspector-modern
  jsinspector-modern/cdp
  jsinspector-modern/network
  jsinspector-modern/tracing
  jsitooling
  logger
  oscompat
  react/bridging
  react/debug
  react/featureflags
  react/nativemodule/core
  react/nativemodule/defaults
  react/nativemodule/devtoolsruntimesettings
  react/nativemodule/dom
  react/nativemodule/featureflags
  react/nativemodule/idlecallbacks
  react/nativemodule/intersectionobserver
  react/nativemodule/microtasks
  react/nativemodule/mutationobserver
  react/nativemodule/viewtransition
  react/nativemodule/webperformance
  react/performance/cdpmetrics
  react/performance/timeline
  react/renderer/animated
  react/renderer/animationbackend
  react/renderer/attributedstring
  react/renderer/bridging
  react/renderer/componentregistry
  react/renderer/componentregistry/native
  react/renderer/components/image
  react/renderer/components/legacyviewmanagerinterop
  react/renderer/components/modal
  react/renderer/components/root
  react/renderer/components/scrollview
  react/renderer/components/text
  react/renderer/components/view
  react/renderer/consistency
  react/renderer/core
  react/renderer/css
  react/renderer/debug
  react/renderer/dom
  react/renderer/graphics
  react/renderer/imagemanager
  react/renderer/leakchecker
  react/renderer/mapbuffer
  react/renderer/mounting
  react/renderer/observers/events
  react/renderer/observers/intersection
  react/renderer/observers/mutation
  react/renderer/runtimescheduler
  react/renderer/scheduler
  react/renderer/telemetry
  react/renderer/uimanager
  react/renderer/viewtransition
  react/renderer/uimanager/consistency
  react/runtime
  react/runtime/hermes
  react/timing
  react/utils
  reactperflogger
  runtimeexecutor
  yoga)
# Newer React Native versions' additions (0.88: the stable C++ API guard,
# ResizeObserver), when the sources have them.
set(RNGTK_REACT_COMMON_OPTIONAL_SUBDIRS
  react/cxxstableapi
  react/nativemodule/resizeobserver
  react/renderer/observers/resize)
foreach(dir ${RNGTK_REACT_COMMON_OPTIONAL_SUBDIRS})
  if(EXISTS ${REACT_COMMON_DIR}/${dir}/CMakeLists.txt)
    list(APPEND RNGTK_REACT_COMMON_SUBDIRS ${dir})
  endif()
endforeach()
foreach(dir ${RNGTK_REACT_COMMON_SUBDIRS})
  add_react_common_subdir(${dir})
endforeach()
# Libraries that depend on the JNI yoga target link "yoga".
add_library(yoga ALIAS yogacore)

# PlatformColor: graphics/ holds our HostPlatformColor.h and
# PlatformColorParser.h, which replace the cxx platform's (a parser that
# makes every PlatformColor transparent) for every target that includes
# them: light and dark libadwaita colors (graphics/rngtk/PlatformColors.h).
set(RNGTK_GRAPHICS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/graphics)
foreach(prop INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(rngtk_dirs react_renderer_graphics ${prop})
  set_property(TARGET react_renderer_graphics PROPERTY ${prop}
               ${RNGTK_GRAPHICS_DIR} ${rngtk_dirs})
endforeach()

# View and paragraph props: components/react/renderer/components/ holds
# our HostPlatformViewProps (focus and keys), HostPlatformViewTraits-
# Initializer and HostPlatformParagraphProps (selectionColor), which
# replace the cxx platform's (the base props alone).
set(RNGTK_PROPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/components)
foreach(target rrc_view rrc_text)
  foreach(prop INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(rngtk_dirs ${target} ${prop})
    set_property(TARGET ${target} PROPERTY ${prop}
                 ${RNGTK_PROPS_DIR} ${rngtk_dirs})
  endforeach()
endforeach()
target_sources(rrc_view PRIVATE
  ${RNGTK_PROPS_DIR}/react/renderer/components/view/HostPlatformViewProps.cpp)
# A header that newly shadows another isn't in any object's depfile, so
# Ninja wouldn't rebuild existing builds against it: bump this whenever
# a header joins the replaced set, so every consumer's flags change too.
target_compile_definitions(react_renderer_graphics PUBLIC RNGTK_HEADER_OVERRIDES=3)
target_sources(rrc_text PRIVATE
  ${RNGTK_PROPS_DIR}/react/renderer/components/text/HostPlatformParagraphProps.cpp)

# Our Pango text measurement replaces react/renderer/textlayoutmanager.
add_subdirectory(textlayoutmanager)

# TextInput: React Native's iOS C++ component (name "TextInput", which
# TextInput.js renders off Android). It is plain C++; ReactCommon's own
# CMake target builds the Android one.
set(RNGTK_TEXTINPUT_DIR ${REACT_COMMON_DIR}/react/renderer/components/textinput)
file(GLOB rngtk_textinput_SRC CONFIGURE_DEPENDS
  ${RNGTK_TEXTINPUT_DIR}/*.cpp
  ${RNGTK_TEXTINPUT_DIR}/platform/ios/react/renderer/components/iostextinput/*.cpp)
add_library(rngtk_textinput OBJECT ${rngtk_textinput_SRC})
target_include_directories(rngtk_textinput PUBLIC
  ${RNGTK_TEXTINPUT_DIR} ${RNGTK_TEXTINPUT_DIR}/platform/ios)
target_link_libraries(rngtk_textinput
  glog folly_runtime jsi react_debug react_renderer_attributedstring
  react_renderer_componentregistry react_renderer_core react_renderer_debug
  react_renderer_graphics react_renderer_imagemanager react_renderer_mapbuffer
  react_renderer_mounting react_renderer_textlayoutmanager
  react_renderer_uimanager react_utils rrc_image rrc_text rrc_view yoga)
target_compile_reactnative_options(rngtk_textinput PRIVATE)

# ReactCxxPlatform
foreach(dir
    react/coremodules
    react/devsupport
    react/http
    react/io
    react/logging
    react/nativemodule
    react/profiling
    react/renderer/scheduler
    react/renderer/uimanager
    react/runtime
    react/threading
    react/utils)
  add_react_cxx_platform_subdir(${dir})
endforeach()

# DevServerHelper asks Metro for platform=android (bundle URL and HMR setup;
# upstream T159303412). Build a copy that asks for platform=linux instead.
set(RNGTK_DEV_SERVER_HELPER_SRC
    ${REACT_CXX_PLATFORM_DIR}/react/devsupport/DevServerHelper.cpp)
file(READ ${RNGTK_DEV_SERVER_HELPER_SRC} rngtk_dsh)
string(REPLACE "DEFAULT_PLATFORM = \"android\"" "DEFAULT_PLATFORM = \"linux\""
       rngtk_dsh_linux "${rngtk_dsh}")
if(rngtk_dsh_linux STREQUAL rngtk_dsh)
  message(FATAL_ERROR "DevServerHelper.cpp changed upstream: no DEFAULT_PLATFORM = \"android\"")
endif()
string(REPLACE "#include \"DevServerHelper.h\"" "#include <react/devsupport/DevServerHelper.h>"
       rngtk_dsh_linux "${rngtk_dsh_linux}")
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rngtk/DevServerHelper.cpp
     CONTENT "${rngtk_dsh_linux}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RNGTK_DEV_SERVER_HELPER_SRC})
get_target_property(rngtk_devsupport_src react_cxx_platform_react_devsupport SOURCES)
list(REMOVE_ITEM rngtk_devsupport_src ${RNGTK_DEV_SERVER_HELPER_SRC})
list(APPEND rngtk_devsupport_src ${CMAKE_CURRENT_BINARY_DIR}/rngtk/DevServerHelper.cpp)
set_property(TARGET react_cxx_platform_react_devsupport PROPERTY SOURCES ${rngtk_devsupport_src})

# ReactCommon's cxx ImageManager is a stub; ours (linux/imagemanager) loads
# through GtkImageLoader and is built into rn-gtk-host instead.
get_target_property(rngtk_imagemanager_src react_renderer_imagemanager SOURCES)
list(FILTER rngtk_imagemanager_src EXCLUDE REGEX "platform/cxx/.*ImageManager\\.cpp$")
set_property(TARGET react_renderer_imagemanager PROPERTY SOURCES ${rngtk_imagemanager_src})

# React Native's own -Werror stays useful for our code, but warnings that a
# newer toolchain finds in upstream sources should not stop the build.
function(rngtk_relax_upstream_warnings dir)
  get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(t ${targets})
    get_target_property(type ${t} TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
      set_property(TARGET ${t} APPEND PROPERTY COMPILE_OPTIONS -Wno-error)
    endif()
  endforeach()
  get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(sub ${subdirs})
    rngtk_relax_upstream_warnings(${sub})
  endforeach()
endfunction()
rngtk_relax_upstream_warnings(${CMAKE_CURRENT_SOURCE_DIR})

# GTK widgets (also used by the Phase 0 spike)
add_library(rngtk_widgets STATIC widgets/rn_view.cc widgets/rn_text.cc
            widgets/rn_scroll_view.cc widgets/rn_text_input.cc
            widgets/rn_css.cc)
target_include_directories(rngtk_widgets PUBLIC widgets)
target_compile_definitions(rngtk_widgets PUBLIC
  GDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14
  GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14)
target_link_libraries(rngtk_widgets PUBLIC PkgConfig::GTK4 PkgConfig::PANGO)

# The host: our sources, shared by the app library and the test harness.
add_library(rngtk_host_objs OBJECT
  src/AccessibilityInfo.cc
  src/AppWindows.cc
  src/Appearance.cc
  src/DevControls.cc
  src/DevUI.cc
  src/Dialogs.cc
  src/FeatureFlags.cc
  src/JsMessageQueueThread.cc
  src/GtkMountingManager.cc
  src/GtkModals.cc
  src/GtkMenus.cc
  src/GtkPointerHandler.cc
  src/GtkDragDrop.cc
  src/GtkScrollViews.cc
  src/GtkTextInputs.cc
  src/GtkControls.cc
  src/GtkAccessibility.cc
  components/GtkSwitchShadowNode.cpp
  components/GtkWindowControlsShadowNode.cpp
  src/GtkImageLoader.cc
  src/GtkKeyboardHandler.cc
  src/KeyNames.cc
  src/Notifications.cc
  src/GtkImages.cc
  imagemanager/ImageManager.cpp
  src/GtkViewProps.cc
  src/PlatformConstantsModule.cc
  src/PlatformModules.cc
  src/RNGtkHost.cc
  src/SoupNetworking.cc)
target_include_directories(rngtk_host_objs PUBLIC src components include)
target_compile_options(rngtk_host_objs PUBLIC -Wall -Werror -fexceptions -frtti)

# Platform.constants.reactNativeVersion, from rn-version.properties.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../rn-version.properties RNGTK_RN_VERSION
     REGEX "^reactNative=")
if(NOT RNGTK_RN_VERSION MATCHES "^reactNative=([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "no reactNative=X.Y.Z in rn-version.properties")
endif()
set(RNGTK_RN_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
target_compile_definitions(rngtk_host_objs PRIVATE
  RNGTK_RN_VERSION_MAJOR=${CMAKE_MATCH_1}
  RNGTK_RN_VERSION_MINOR=${CMAKE_MATCH_2}
  RNGTK_RN_VERSION_PATCH=${CMAKE_MATCH_3})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_SOURCE_DIR}/../rn-version.properties)

# Most React Native targets are OBJECT libraries, whose objects only reach
# targets that link them directly: each binary links the whole list.
set(RNGTK_REACT_NATIVE_LIBS
    rngtk_widgets
    PkgConfig::SOUP
    Threads::Threads

    # third-party
    boost
    double-conversion
    fmt
    folly_runtime
    gflags
    glog

    # hermes
    hermes-engine::hermesvm

    # codegen
    react_codegen_rncore

    # ReactCommon
    bridgeless
    bridgelesshermes
    devtoolsruntimesettings
    hermes_executor_common
    hermes_inspector_modern
    jserrorhandler
    jsi
    jsinspector
    jsinspector_cdp
    jsinspector_network
    jsinspector_tracing
    jsireact
    jsitooling
    logger
    oscompat
    react_bridging
    react_cxxreact
    react_debug
    react_featureflags
    react_nativemodule_core
    react_nativemodule_defaults
    react_nativemodule_devtoolsruntimesettings
    react_nativemodule_dom
    react_nativemodule_featureflags
    react_nativemodule_idlecallbacks
    react_nativemodule_intersectionobserver
    $<$<TARGET_EXISTS:react_nativemodule_resizeobserver>:react_nativemodule_resizeobserver>
    react_nativemodule_microtasks
    react_nativemodule_mutationobserver
    react_nativemodule_viewtransition
    react_nativemodule_webperformance
    react_performance_cdpmetrics
    react_performance_timeline
    react_renderer_attributedstring
    react_renderer_componentregistry
    react_renderer_consistency
    react_renderer_core
    react_renderer_css
    react_renderer_debug
    react_renderer_dom
    react_renderer_graphics
    react_renderer_imagemanager
    react_renderer_leakchecker
    react_renderer_mapbuffer
    react_renderer_mounting
    react_renderer_observers_events
    react_renderer_observers_intersection
    $<$<TARGET_EXISTS:react_renderer_observers_resize>:react_renderer_observers_resize>
    react_renderer_observers_mutation
    react_renderer_runtimescheduler
    react_renderer_scheduler
    react_renderer_telemetry
    react_renderer_textlayoutmanager
    react_renderer_uimanager
    react_renderer_uimanager_consistency
    react_utils
    reactperflogger
    rrc_legacyviewmanagerinterop
    rrc_image
    rrc_modal
    rrc_native
    rrc_root
    rrc_scrollview
    rrc_text
    rngtk_textinput
    rrc_view
    runtimeexecutor
    yogacore

    # ReactCxxPlatform
    react_cxx_platform_react_coremodules
    react_cxx_platform_react_devsupport
    react_cxx_platform_react_http
    react_cxx_platform_react_io
    react_cxx_platform_react_logging
    react_cxx_platform_react_profiling
    react_renderer_animated
    react_renderer_animationbackend
    react_cxx_platform_react_renderer_scheduler
    react_cxx_platform_react_runtime
    react_cxx_platform_react_threading
    react_cxx_platform_react_utils
)
target_link_libraries(rngtk_host_objs PUBLIC ${RNGTK_REACT_NATIVE_LIBS})

function(rngtk_link_host target)
  target_link_libraries(${target} PRIVATE rngtk_host_objs ${RNGTK_REACT_NATIVE_LIBS})
endfunction()

# JSI is React Native's one SHARED library; installed next to the host.
set_target_properties(jsi PROPERTIES INSTALL_RPATH "$ORIGIN")

# ReactNativeGtk::host for apps: librngtk_host.so, exporting the
# rngtk::runApp API (include/rngtk/App.h), and React Native's C++ (with the
# folly, glog and fmt it is built with) for native libraries'
# ReactNativeGtk::sdk (src/rngtk_host.map). The rest stays inside it.
add_library(rngtk_host SHARED src/App.cc)
rngtk_link_host(rngtk_host)
target_link_libraries(rngtk_host PRIVATE atomic)
target_compile_definitions(rngtk_host PRIVATE RNGTK_RN_VERSION="${RNGTK_RN_VERSION}")
target_link_options(rngtk_host PRIVATE
  -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/rngtk_host.map
  -Wl,--no-undefined)
set_property(TARGET rngtk_host APPEND PROPERTY LINK_DEPENDS
             ${CMAKE_CURRENT_SOURCE_DIR}/src/rngtk_host.map)
set_target_properties(rngtk_host PROPERTIES
  BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")

# Installed by `react-native run-linux` into the shared cache
# (~/.cache/react-native-gtk4/<rn-version>/host/...), where app builds find
# it with find_package(ReactNativeGtk).
include(GNUInstallDirs)
# With its shared dependencies: Hermes and JSI.
install(TARGETS rngtk_host jsi LIBRARY DESTINATION lib)
install(FILES ${LIB_HERMES} DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
configure_file(cmake/ReactNativeGtkConfig.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/ReactNativeGtkConfig.cmake @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ReactNativeGtkConfig.cmake
        DESTINATION lib/cmake/ReactNativeGtk)

# ReactNativeGtk::sdk, for libraries' native modules and components
# (include/rngtk/Extensions.h): the include directories and definitions the
# host is built with, so their objects match it. Directories in this
# package's linux/ folder are installed with it (include/rngtk-sdk/linux);
# React Native's and the third-party ones stay in the dependencies' cache.
set(RNGTK_SDK_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/ReactNativeGtkSdk.generated.cmake)
file(GENERATE OUTPUT ${RNGTK_SDK_GENERATED} CONTENT
"set(ReactNativeGtk_SDK_INCLUDE_DIRS [==[$<TARGET_PROPERTY:rngtk_host_objs,INCLUDE_DIRECTORIES>]==])
set(ReactNativeGtk_SDK_DEFINITIONS [==[$<TARGET_PROPERTY:rngtk_host_objs,COMPILE_DEFINITIONS>]==])
set(ReactNativeGtk_SDK_OPTIONS [==[$<TARGET_PROPERTY:rngtk_host_objs,COMPILE_OPTIONS>]==])
set(ReactNativeGtk_SDK_NDEBUG $<IF:$<CONFIG:Debug>,OFF,ON>)
")
install(DIRECTORY ./ DESTINATION include/rngtk-sdk/linux
        FILES_MATCHING PATTERN "*.h"
        PATTERN "tests" EXCLUDE PATTERN "build" EXCLUDE)
install(CODE "
  file(READ \"${RNGTK_SDK_GENERATED}\" sdk)
  string(REPLACE \"${CMAKE_CURRENT_SOURCE_DIR}\" \"\${CMAKE_INSTALL_PREFIX}/include/rngtk-sdk/linux\" sdk \"\${sdk}\")
  file(WRITE \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/ReactNativeGtk/ReactNativeGtkSdk.cmake\" \"\${sdk}\")
")

# The repository's test harness: self-tests, the gallery and the dev-loop
# checks (not part of the package).
option(RNGTK_BUILD_HARNESS "Build rn-gtk-host, the self-test harness"
       ${RNGTK_IN_REPO})
if(NOT RNGTK_BUILD_HARNESS)
  return()
endif()

add_executable(rn-gtk-host src/main.cc ../spike/src/harness.cc)
target_include_directories(rn-gtk-host PRIVATE ../spike/src)
# The native library template's module and component (GalleryNativeModule),
# registered as a package the way autolinking registers an app's.
set(RNGTK_TEMPLATE_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/../template-library/linux)
target_sources(rn-gtk-host PRIVATE
  ${RNGTK_TEMPLATE_LIBRARY}/src/ExampleModule.cc
  ${RNGTK_TEMPLATE_LIBRARY}/src/ExampleView.cc
  ${RNGTK_TEMPLATE_LIBRARY}/src/ExamplePackage.cc)
target_include_directories(rn-gtk-host PRIVATE ${RNGTK_TEMPLATE_LIBRARY}/src)
# The react-native-gesture-handler port (packages/gesture-handler), for
# GalleryGestures.
target_sources(rn-gtk-host PRIVATE
  ../packages/gesture-handler/linux/src/GestureHandler.cc
  ../packages/gesture-handler/linux/src/Handlers.cc
  ../packages/gesture-handler/linux/src/Orchestrator.cc
  ../packages/gesture-handler/linux/src/GestureHandlerModule.cc)
# The react-native-screens port (packages/screens), for GalleryNavigation:
# the Showcase's bundle uses its native components. Needs libadwaita.
pkg_check_modules(RNGTK_HARNESS_ADW QUIET IMPORTED_TARGET libadwaita-1>=1.4)
if(RNGTK_HARNESS_ADW_FOUND)
  target_sources(rn-gtk-host PRIVATE ../packages/screens/linux/src/Screens.cc)
  target_link_libraries(rn-gtk-host PRIVATE PkgConfig::RNGTK_HARNESS_ADW)
  target_compile_definitions(rn-gtk-host PRIVATE RNGTK_HARNESS_SCREENS)
else()
  message(WARNING "libadwaita-1 >= 1.4 not found: rn-gtk-host is built without the "
                  "react-native-screens port, and gallery-navigation-self-test fails")
endif()
# GalleryAccessibility's self-test runs scripts/a11y-probe.py.
target_compile_definitions(rn-gtk-host PRIVATE
  RNGTK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
rngtk_link_host(rn-gtk-host)

enable_testing()
set(RNGTK_HELLO_BUNDLE
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples/hello-world/build/index.bundle.js)
add_test(NAME hello-world-js-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE} --self-test)
add_test(NAME gallery-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE} --module Gallery
                 --width 940 --height 680 --self-test)
add_test(NAME gallery-lists-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryLists --width 940 --height 680 --self-test)
add_test(NAME gallery-controls-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryControls --width 940 --height 680 --self-test)
add_test(NAME gallery-platform-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryPlatform --width 940 --height 680 --self-test
                 --url rngtk-test:initial)
add_test(NAME gallery-platform-rtl-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryPlatform --width 940 --height 680 --self-test --rtl)
add_test(NAME gallery-accessibility-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryAccessibility --width 940 --height 680 --self-test)
add_test(NAME gallery-mouse-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryMouse --width 940 --height 680 --self-test)
add_test(NAME gallery-keyboard-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryKeyboard --width 940 --height 680 --self-test)
add_test(NAME gallery-selection-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GallerySelection --width 940 --height 680 --self-test)
add_test(NAME gallery-appearance-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryAppearance --width 940 --height 680 --self-test)
add_test(NAME gallery-modal-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryModal --width 940 --height 680 --self-test)
add_test(NAME gallery-dialogs-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryDialogs --width 940 --height 680 --self-test)
add_test(NAME gallery-menus-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryMenus --width 940 --height 680 --self-test)
add_test(NAME gallery-windows-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryWindows --width 940 --height 680 --self-test)
add_test(NAME gallery-titlebar-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryTitleBar --width 940 --height 680 --self-test)
add_test(NAME gallery-dragdrop-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryDragDrop --width 940 --height 680 --self-test)
# On a private session bus (nothing to start on it), with a stand-in
# notification server.
add_test(NAME gallery-notifications-self-test
         COMMAND dbus-run-session
                 --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/private-session-bus.conf --
                 $<TARGET_FILE:rn-gtk-host> --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryNotifications --width 940 --height 680 --self-test)
add_test(NAME gallery-native-module-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryNativeModule --width 940 --height 680 --self-test)
add_test(NAME gallery-navigation-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryNavigation --width 940 --height 680 --self-test)
# The same without the port: react-native-screens' web components.
add_test(NAME gallery-navigation-web-self-test
         COMMAND rn-gtk-host
                 --bundle ${CMAKE_CURRENT_SOURCE_DIR}/../examples/hello-world/build/web-screens.bundle.js
                 --module GalleryNavigation --width 940 --height 680 --self-test)
add_test(NAME gallery-gestures-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryGestures --width 940 --height 680 --self-test)
add_test(NAME gallery-images-self-test
         COMMAND rn-gtk-host --bundle ${RNGTK_HELLO_BUNDLE}
                 --module GalleryImages --width 940 --height 680 --self-test)

# libsoup clients against a running Metro (scripts/test-dev-loop.sh).
add_executable(rngtk-net-check tests/net_check.cc src/SoupNetworking.cc)
target_compile_options(rngtk-net-check PRIVATE -Wall -Werror -fexceptions -frtti)
target_link_libraries(rngtk-net-check PRIVATE
  PkgConfig::SOUP folly_runtime glog react_cxx_platform_react_http)
