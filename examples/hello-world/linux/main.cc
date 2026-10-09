// The Showcase's Linux entry point: the component registered as
// "Showcase" (Showcase.js) in a GTK4 window.
#include <rngtk/App.h>
#include <rngtk/Autolinking.h>

int main(int argc, char **argv) {
  rngtk::AppOptions options;
  options.appId = "dev.curiosity26.RNGtk4.Showcase";
  options.title = "React Native GTK4 Showcase";
  options.moduleName = "Showcase";
  options.width = 1100;
  options.height = 780;
  options.packages = rngtk::autolinkedPackages();
  return rngtk::runApp(argc, argv, options);
}
