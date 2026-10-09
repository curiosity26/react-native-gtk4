// Autolinked by run-linux: linux/ (react-native-webview's RNCWebView on
// WebKitGTK 6.0, and RNCWebViewModule). With this package installed,
// @curiosity26/react-native-gtk4's Metro config gives react-native-webview
// its iOS JS on Linux, which drives the same native component.
module.exports = {
  dependency: {
    platforms: {
      linux: {
        cmakeTarget: 'rngtk_webview',
        packageFunction: 'rngtk_webview_package',
      },
    },
  },
};
