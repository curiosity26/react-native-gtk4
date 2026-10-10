// Autolinked by run-linux: linux/ (react-native-screens' native components
// on libadwaita), and overrides/, which the Metro config uses instead of the
// library's files when this package is installed (Linux is a native
// platform for react-native-screens then).
module.exports = {
  dependency: {
    platforms: {
      linux: {
        cmakeTarget: 'rngtk_screens',
        packageFunction: 'rngtk_screens_package',
      },
    },
  },
};
