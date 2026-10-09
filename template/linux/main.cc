// {{name}}'s Linux entry point: a GTK4 window running the React Native
// component registered as "{{moduleName}}".
//
// Debug builds load the JS from Metro (npx react-native start); Release
// builds load index.bundle.js next to the executable. See
// rngtk/App.h for the command-line options (--smoke, --bundle, ...).
#include <rngtk/App.h>

int main(int argc, char **argv) {
  rngtk::AppOptions options;
  options.appId = "{{appId}}";
  options.title = "{{title}}";
  options.moduleName = "{{moduleName}}";
  options.width = 800;
  options.height = 600;
  return rngtk::runApp(argc, argv, options);
}
