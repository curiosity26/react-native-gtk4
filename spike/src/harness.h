// Small helpers shared by the spike apps: screenshots, pixel probes, stats.
#pragma once

#include <gtk/gtk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rngtk {

// Renders the widget with its window's GSK renderer.
GdkTexture *render_widget(GtkWidget *widget);
bool save_png(GdkTexture *texture, const char *path);

struct Rgba8 {
  uint8_t r, g, b, a;
};
Rgba8 texture_pixel(GdkTexture *texture, int x, int y);
bool near(Rgba8 actual, Rgba8 expected, int tolerance = 6);

struct Stats {
  double mean = 0, p50 = 0, p95 = 0, max = 0;
};
Stats summarize(std::vector<double> values);

double now_ms();
long rss_kb();

// Display backend ("x11", "wayland", ...) and GSK renderer name.
std::string backend_name(GtkWidget *widget);
std::string renderer_name(GtkWidget *widget);

}  // namespace rngtk
