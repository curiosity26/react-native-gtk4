// Maps React Native view props (Fabric's ViewProps) onto our GTK widgets:
// RNView styles, transforms, pointerEvents and the cursor.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/LayoutMetrics.h>

namespace rngtk {

void apply_view_props(GtkWidget *widget,
                      const facebook::react::ViewProps &props,
                      const facebook::react::LayoutMetrics &layout);

// Only the RNView drawing styles (background, borders, shadows, filters,
// gradients), for a widget whose box is drawn by an inner RNView.
void apply_view_style(GtkWidget *view, const facebook::react::ViewProps &props,
                      const facebook::react::LayoutMetrics &layout);

GdkRGBA to_rgba(const facebook::react::SharedColor &color);

}  // namespace rngtk
