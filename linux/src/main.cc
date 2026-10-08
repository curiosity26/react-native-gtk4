// Runs a React Native JS bundle in a GTK4 window.
//
//   rn-gtk-host --bundle index.bundle.js [--module HelloWorld]
//               [--width 800 --height 600] [--self-test] [--screenshot F]
//
// --self-test waits for the first mount, renders the window, checks that the
// Hello World example drew what its App.js describes, and exits 0/1.
#include <glog/logging.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDynamicProvider.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>

#include "GtkMountingManager.h"
#include "RNGtkHost.h"
#include "harness.h"
#include "rn_text.h"
#include "rn_view.h"

using namespace facebook::react;

namespace {

constexpr SurfaceId kSurfaceId = 1;
constexpr int kFramesAfterMount = 2;

struct Options {
  std::string bundle;
  std::string module = "HelloWorld";
  int width = 800, height = 600;
  bool self_test = false;
  const char *screenshot = nullptr;
  int timeout_ms = 20000;
} opts;

struct App {
  rngtk::RNGtkHost *host = nullptr;
  GtkWidget *root = nullptr;
  int frames_since_mount = 0;
  int exit_code = 0;
} app;

bool check(bool ok, const char *what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) app.exit_code = 1;
  return ok;
}

// Depth-first search for the n-th widget of a type under `widget`.
GtkWidget *find_nth(GtkWidget *widget, GType type, int *n) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (G_TYPE_CHECK_INSTANCE_TYPE(c, type) && (*n)-- == 0) return c;
    if (GtkWidget *found = find_nth(c, type, n)) return found;
  }
  return nullptr;
}

graphene_rect_t bounds_in_root(GtkWidget *widget) {
  graphene_rect_t b = GRAPHENE_RECT_INIT(0, 0, 0, 0);
  if (!gtk_widget_compute_bounds(widget, app.root, &b)) {
    return GRAPHENE_RECT_INIT(0, 0, 0, 0);
  }
  return b;
}

bool has_dark_pixel(GdkTexture *tex, graphene_rect_t r) {
  for (int y = 0; y < (int)r.size.height; y++) {
    for (int x = 0; x < (int)r.size.width; x++) {
      auto p = rngtk::texture_pixel(tex, (int)r.origin.x + x,
                                    (int)r.origin.y + y);
      if (p.r < 90 && p.g < 90 && p.b < 90) return true;
    }
  }
  return false;
}

// Checks the examples/hello-world App.js layout: a centered 400x200 white
// card with a 2px #007AFF border, radius 16, and two lines of text.
void verify_hello_world(GdkTexture *tex) {
  auto &mm = app.host->mountingManager();
  printf("mounted views: %zu, mount transactions: %d, js errors: %d\n",
         mm.mountedViewCount(), mm.mountCount(), app.host->jsErrorCount());
  check(app.host->jsErrorCount() == 0, "no JS errors");
  // root + app root + card + 2 paragraphs
  check(mm.mountedViewCount() >= 5, "JS tree mounted (>= 5 views)");
  check(tex && gdk_texture_get_width(tex) == opts.width &&
            gdk_texture_get_height(tex) == opts.height,
        "root rendered at the surface size");
  if (!tex) return;

  const rngtk::Rgba8 root_bg{0xF5, 0xF5, 0xF7, 0xFF};
  const rngtk::Rgba8 white{0xFF, 0xFF, 0xFF, 0xFF};
  const rngtk::Rgba8 blue{0x00, 0x7A, 0xFF, 0xFF};
  float cx = (opts.width - 400) / 2.0f, cy = (opts.height - 200) / 2.0f;
  check(rngtk::near(rngtk::texture_pixel(tex, 10, 10), root_bg),
        "root background is #F5F5F7");
  check(rngtk::near(rngtk::texture_pixel(tex, cx + 30, cy + 30), white),
        "card is white and centered by flexbox");
  check(rngtk::near(rngtk::texture_pixel(tex, cx + 200, cy + 1), blue, 40),
        "card border is #007AFF");
  check(rngtk::near(rngtk::texture_pixel(tex, cx + 1, cy + 1), root_bg, 12),
        "card corner is rounded");

  int n = 0;
  GtkWidget *title = find_nth(app.root, RN_TYPE_TEXT, &n);
  n = 1;
  GtkWidget *subtitle = find_nth(app.root, RN_TYPE_TEXT, &n);
  if (check(title && subtitle, "two Text paragraphs mounted")) {
    graphene_rect_t t = bounds_in_root(title), s = bounds_in_root(subtitle);
    printf("title frame %.0f,%.0f %.0fx%.0f; subtitle frame %.0f,%.0f %.0fx%.0f\n",
           t.origin.x, t.origin.y, t.size.width, t.size.height, s.origin.x,
           s.origin.y, s.size.width, s.size.height);
    check(t.size.width > 100 && t.size.height >= 36,
          "title measured by Pango (36px bold)");
    check(std::abs((t.origin.x + t.size.width / 2) - opts.width / 2.0f) <= 1,
          "title horizontally centered");
    check(s.origin.y >= t.origin.y + t.size.height + 8 - 0.5f,
          "subtitle 8px below the title");
    check(has_dark_pixel(tex, t), "title text drawn");
  }
}

void finish() {
  GdkTexture *tex = rngtk::render_widget(app.root);
  printf("backend=%s renderer=%s\n", rngtk::backend_name(app.root).c_str(),
         rngtk::renderer_name(app.root).c_str());
  if (opts.screenshot) {
    check(rngtk::save_png(tex, opts.screenshot), "screenshot saved");
  }
  if (opts.self_test) verify_hello_world(tex);
  g_clear_object(&tex);
  if (opts.self_test) g_application_quit(g_application_get_default());
}

gboolean on_tick(GtkWidget *, GdkFrameClock *, gpointer) {
  // Wait for the first mount to settle, then for frames to paint it.
  if (app.host->mountingManager().mountCount() == 0 || !app.host->isIdle()) {
    return G_SOURCE_CONTINUE;
  }
  if (++app.frames_since_mount < kFramesAfterMount) return G_SOURCE_CONTINUE;
  finish();
  return G_SOURCE_REMOVE;
}

gboolean on_timeout(gpointer) {
  fprintf(stderr, "FAIL nothing mounted within %d ms\n", opts.timeout_ms);
  app.exit_code = 1;
  g_application_quit(g_application_get_default());
  return G_SOURCE_REMOVE;
}

void activate(GtkApplication *gtk_app, gpointer) {
  GtkWidget *window = gtk_application_window_new(gtk_app);
  gtk_window_set_title(GTK_WINDOW(window), opts.module.c_str());
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  app.root = rn_view_new();
  gtk_window_set_child(GTK_WINDOW(window), app.root);

  app.host = new rngtk::RNGtkHost();
  if (!app.host->loadBundle(opts.bundle)) {
    fprintf(stderr, "could not load %s\n", opts.bundle.c_str());
    app.exit_code = 1;
    g_application_quit(G_APPLICATION(gtk_app));
    return;
  }
  app.host->startSurface(kSurfaceId, opts.module, app.root, opts.width,
                         opts.height);

  if (opts.self_test || opts.screenshot) {
    gtk_widget_add_tick_callback(app.root, on_tick, nullptr, nullptr);
    g_timeout_add(opts.timeout_ms, on_timeout, nullptr);
  }
  gtk_window_present(GTK_WINDOW(window));
}

void set_up_feature_flags() {
  folly::dynamic flags = folly::dynamic::object();
  flags["enableBridgelessArchitecture"] = true;
  flags["cxxNativeAnimatedEnabled"] = true;
  ReactNativeFeatureFlags::override(
      std::make_unique<ReactNativeFeatureFlagsDynamicProvider>(flags));
}

int usage() {
  fprintf(stderr,
          "usage: rn-gtk-host --bundle FILE [--module NAME] [--width N]\n"
          "                   [--height N] [--self-test] [--screenshot PNG]\n");
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    auto arg = [&](const char *name) {
      return !strcmp(argv[i], name) && i + 1 < argc;
    };
    if (arg("--bundle")) opts.bundle = argv[++i];
    else if (arg("--module")) opts.module = argv[++i];
    else if (arg("--width")) opts.width = atoi(argv[++i]);
    else if (arg("--height")) opts.height = atoi(argv[++i]);
    else if (arg("--screenshot")) opts.screenshot = argv[++i];
    else if (arg("--timeout")) opts.timeout_ms = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--self-test")) opts.self_test = true;
    else return usage();
  }
  if (opts.bundle.empty()) return usage();

  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = 1;  // warnings and up
  set_up_feature_flags();

  GtkApplication *gtk_app = gtk_application_new(
      "dev.curiosity26.RNGtk4.Host", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), nullptr);
  int status = g_application_run(G_APPLICATION(gtk_app), 1, argv);
  delete app.host;
  g_object_unref(gtk_app);
  return status ? status : app.exit_code;
}
