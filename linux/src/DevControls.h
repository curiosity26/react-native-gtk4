// Dev mode's window actions: the dev menu's "dev.*" actions and the
// Ctrl+R (reload), Ctrl+D / Ctrl+M (dev menu) shortcuts.
#pragma once

#include <gtk/gtk.h>

namespace rngtk {

class RNGtkHost;

// `verbose` also logs every key press. `host` must outlive the window.
void addDevControls(GtkWidget *window, RNGtkHost *host, bool verbose);

}  // namespace rngtk
