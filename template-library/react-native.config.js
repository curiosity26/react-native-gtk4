// The library's native code per platform. For Linux, run-linux autolinks
// linux/ (its CMakeLists.txt) when an app depends on this package; these
// are the defaults (from the package name), spelled out.
module.exports = {
  dependency: {
    platforms: {
      linux: {
        // The CMake target to link.
        cmakeTarget: 'example',
        // std::shared_ptr<const rngtk::Package> example_package();
        packageFunction: 'example_package',
      },
    },
  },
};
