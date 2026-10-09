// Dialogs: Alert (AlertManager) and the Linux Dialogs API (LinuxDialogs).
//
// - AlertManager: Alert.alert and Alert.prompt (overrides/Libraries/Alert/
//   Alert.linux.js) on a GTK message dialog, the one GtkAlertDialog shows,
//   modal over the app's active window: a title and message, buttons
//   (cancel first, destructive in red, the preferred one the default),
//   text, password or login and password fields, `cancelable` and
//   onDismiss. Screen readers see an alert dialog.
// - LinuxDialogs: open files (one or several, with filters), save a file,
//   pick folders, on GtkFileDialog (the desktop's file chooser portal when
//   there is one, as in Flatpak; GTK's own chooser otherwise). Paths come
//   back to JS (js/Dialogs.js).
#pragma once

#include <ReactCommon/TurboModule.h>
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>

namespace rngtk {

struct PlatformState;

// The module named `name`, or null.
std::shared_ptr<facebook::react::TurboModule> makeDialogModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker,
    const std::shared_ptr<PlatformState> &state);

// Main thread: the window dialogs open over: the app's active window (a
// Modal's, or another of its windows), else its main window.
GtkWindow *dialogParent(const PlatformState &state);

}  // namespace rngtk
