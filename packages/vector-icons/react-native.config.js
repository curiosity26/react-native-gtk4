// Autolinked by run-linux: linux/ copies the icon fonts of the app's
// react-native-vector-icons packages next to the app and registers them
// with fontconfig at startup.
module.exports = {
  dependency: {
    platforms: {
      linux: {
        cmakeTarget: 'rngtk_vector_icons',
        packageFunction: 'rngtk_vector_icons_package',
      },
    },
  },
};
