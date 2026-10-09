// The packages of the app's native libraries, autolinked by run-linux: it
// generates their definition (autolinking.cc) into the app's build, from
// the dependencies that have a linux/ folder. An app's main.cc passes them
// on:
//
//   options.packages = rngtk::autolinkedPackages();
#pragma once

#include <rngtk/App.h>

namespace rngtk {

PackageList autolinkedPackages();

}  // namespace rngtk
