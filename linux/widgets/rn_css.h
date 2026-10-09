// CSS scoped to one widget and its descendants (a GtkText's placeholder, a
// GtkSwitch's slider): `css` uses `&` for the widget, e.g.
// "& > placeholder { color: red; }". Replaces the widget's previous CSS.
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

void rn_widget_set_css(GtkWidget *widget, const char *css);

// The GTK theme's accent (selection) color for `widget`: its
// accent_bg_color, or GTK 3's theme_selected_bg_color (Yaru's orange),
// else Adwaita's blue.
GdkRGBA rn_theme_accent(GtkWidget *widget);

G_END_DECLS
