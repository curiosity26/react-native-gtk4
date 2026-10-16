#include "harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <unistd.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif

namespace rngtk {

GdkTexture *render_widget(GtkWidget *widget) {
  GdkPaintable *paintable = gtk_widget_paintable_new(widget);
  int w = gtk_widget_get_width(widget);
  int h = gtk_widget_get_height(widget);
  GtkSnapshot *snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, w, h);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  g_object_unref(paintable);
  if (!node) return nullptr;
  GskRenderer *renderer = gtk_native_get_renderer(gtk_widget_get_native(widget));
  graphene_rect_t viewport;
  graphene_rect_init(&viewport, 0, 0, (float)w, (float)h);
  GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &viewport);
  gsk_render_node_unref(node);
  return texture;
}

bool save_png(GdkTexture *texture, const char *path) {
  return texture && gdk_texture_save_to_png(texture, path);
}

Rgba8 texture_pixel(GdkTexture *texture, int x, int y) {
  int w = gdk_texture_get_width(texture);
  int h = gdk_texture_get_height(texture);
  std::vector<uint8_t> data(static_cast<size_t>(w) * h * 4);
  GdkTextureDownloader *dl = gdk_texture_downloader_new(texture);
  gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
  gdk_texture_downloader_download_into(dl, data.data(), w * 4);
  gdk_texture_downloader_free(dl);
  const uint8_t *p = &data[(static_cast<size_t>(y) * w + x) * 4];
  return {p[0], p[1], p[2], p[3]};
}

bool near(Rgba8 a, Rgba8 e, int tol) {
  return std::abs(a.r - e.r) <= tol && std::abs(a.g - e.g) <= tol &&
         std::abs(a.b - e.b) <= tol && std::abs(a.a - e.a) <= tol;
}

Stats summarize(std::vector<double> v) {
  Stats s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
  s.p50 = v[v.size() / 2];
  s.p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
  s.max = v.back();
  return s;
}

double now_ms() { return g_get_monotonic_time() / 1000.0; }

long rss_kb() {
  long pages = 0, resident = 0;
  if (FILE *f = fopen("/proc/self/statm", "r")) {
    if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
    fclose(f);
  }
  return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

std::string backend_name(GtkWidget *widget) {
  GdkDisplay *display = gtk_widget_get_display(widget);
#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY(display)) return "wayland";
#endif
#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_DISPLAY(display)) return "x11";
#endif
  return G_OBJECT_TYPE_NAME(display);
}

std::string renderer_name(GtkWidget *widget) {
  GskRenderer *r = gtk_native_get_renderer(gtk_widget_get_native(widget));
  return r ? G_OBJECT_TYPE_NAME(r) : "none";
}

}  // namespace rngtk
