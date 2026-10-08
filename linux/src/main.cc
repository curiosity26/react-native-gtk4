// Runs a React Native app in a GTK4 window, from a bundle file or from Metro.
//
//   rn-gtk-host --bundle index.bundle.js [--module HelloWorld]
//   rn-gtk-host --dev-server [localhost:8081] [--entry index] [--no-inspector]
//   common:     [--width 800 --height 600] [--self-test] [--screenshot F]
//
// Dev mode keys: Ctrl+R reloads, Ctrl+D (or Ctrl+M) opens the dev menu.
//
// --self-test waits for the first mount, renders the window, checks that the
// Hello World example drew what its App.js describes, and exits 0/1. Dev-loop
// checks follow it, in this order (scripts/test-dev-loop.sh drives them):
//   --test-reload          reload (what Ctrl+R does), then check again, with
//                          no leaked widgets
//   --expect-reload        then print "READY expect-reload", wait for a
//                          reload from outside (Metro's `r`), check again
//   --expect-text TEXT     print "READY expect-text", then wait for a Text
//                          containing TEXT, without a reload (fast refresh)
//   --expect-logbox        print "READY expect-logbox", then wait for LogBox
//   --logbox-screenshot F  save the window once LogBox shows
#include <glog/logging.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDynamicProvider.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "DevUI.h"
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
  bool dev = false;
  std::string dev_host = "localhost";
  uint32_t dev_port = 8081;
  std::string entry = "index";
  bool inspector = true;  // dev mode only
  std::string module = "HelloWorld";
  int width = 800, height = 600;
  bool self_test = false;
  const char *screenshot = nullptr;
  bool test_reload = false;
  bool expect_reload = false;
  std::string expect_text;
  bool expect_logbox = false;
  const char *logbox_screenshot = nullptr;
  int timeout_ms = 20000;
  bool verbose = false;
} opts;

enum class Phase { Initial, Reloading, ExpectText, ExpectLogBox, Done };

struct App {
  rngtk::RNGtkHost *host = nullptr;
  GtkWidget *root = nullptr;
  GtkWidget *overlay = nullptr;
  Phase phase = Phase::Initial;
  int frames = 0;
  int mounts_before = 0;
  size_t views_before = 0;
  int instances_before = 0;
  bool reload_external = false;
  int reloads_done = 0;
  guint timeout_id = 0;
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

bool has_text(GtkWidget *widget, const std::string &needle) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (RN_IS_TEXT(c) &&
        std::string(rn_text_get_text(RN_TEXT(c))).find(needle) !=
            std::string::npos) {
      return true;
    }
    if (has_text(c, needle)) return true;
  }
  return false;
}

graphene_rect_t bounds_in_root(GtkWidget *widget) {
  graphene_rect_t b{};
  if (!gtk_widget_compute_bounds(widget, app.root, &b)) return {};
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
// card with a 2px #007AFF border, radius 16, and three lines of text, the
// last one built from Platform.OS and Platform.constants.
void verify_hello_world(GdkTexture *tex) {
  auto &mm = app.host->mountingManager();
  printf("mounted views: %zu, mount transactions: %d, js errors: %d\n",
         mm.mountedViewCount(), mm.mountCount(), app.host->jsErrorCount());
  check(app.host->jsErrorCount() == 0, "no JS errors");
  // root + app root + card + 3 paragraphs
  check(mm.mountedViewCount() >= 6, "JS tree mounted (>= 6 views)");
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

  // The JS side of Platform: OS 'linux' from the .linux.js override, the
  // window system from our PlatformConstants module.
  n = 2;
  GtkWidget *platform = find_nth(app.root, RN_TYPE_TEXT, &n);
  std::string text = platform ? rn_text_get_text(RN_TEXT(platform)) : "";
  printf("platform line: \"%s\"\n", text.c_str());
  check(text.rfind("Running on linux ", 0) == 0, "Platform.OS is 'linux'");
  std::string expected = "(" + rngtk::backend_name(app.root) + ")";
  check(text.find(expected) != std::string::npos,
        "Platform.constants.windowSystem matches the GDK backend");
}

gboolean on_timeout(gpointer);

void restart_timeout() {
  if (app.timeout_id) g_source_remove(app.timeout_id);
  app.timeout_id = g_timeout_add(opts.timeout_ms, on_timeout, nullptr);
}

void quit() {
  app.phase = Phase::Done;
  if (app.timeout_id) g_source_remove(app.timeout_id);
  app.timeout_id = 0;
  if (opts.self_test) g_application_quit(g_application_get_default());
}

void enter(Phase phase) {
  app.phase = phase;
  app.frames = 0;
  restart_timeout();
  const char *name = phase == Phase::Reloading && app.reload_external
                         ? "expect-reload"
                     : phase == Phase::ExpectText ? "expect-text"
                     : phase == Phase::ExpectLogBox ? "expect-logbox"
                                                    : nullptr;
  if (name) {
    printf("READY %s\n", name);
    fflush(stdout);
  }
}

// The dev-loop check after `done`, or quit.
void next_check(Phase done) {
  if (!opts.self_test) {
    quit();
  } else if (done <= Phase::Reloading && opts.dev &&
             app.reloads_done < int(opts.test_reload) + int(opts.expect_reload)) {
    auto &mm = app.host->mountingManager();
    app.views_before = mm.mountedViewCount();
    app.mounts_before = mm.mountCount();
    app.instances_before = app.host->instanceCount();
    // The host's own reload first, then one from outside.
    app.reload_external = !opts.test_reload || app.reloads_done == 1;
    printf("reloading %s (views %zu, JS instances %d)\n",
           app.reload_external ? "from outside" : "via the dev menu action",
           app.views_before, app.instances_before);
    if (!app.reload_external) app.host->reload();
    enter(Phase::Reloading);
  } else if (done < Phase::ExpectText && !opts.expect_text.empty()) {
    app.instances_before = app.host->instanceCount();
    enter(Phase::ExpectText);
  } else if (done < Phase::ExpectLogBox && opts.expect_logbox) {
    enter(Phase::ExpectLogBox);
  } else {
    quit();
  }
}

void check_app(bool first) {
  GdkTexture *tex = rngtk::render_widget(app.root);
  printf("backend=%s renderer=%s\n", rngtk::backend_name(app.root).c_str(),
         rngtk::renderer_name(app.root).c_str());
  if (opts.screenshot && first) {
    check(rngtk::save_png(tex, opts.screenshot), "screenshot saved");
  }
  if (opts.self_test) verify_hello_world(tex);
  g_clear_object(&tex);
}

gboolean on_tick(GtkWidget *, GdkFrameClock *, gpointer) {
  auto &mm = app.host->mountingManager();
  switch (app.phase) {
    case Phase::Initial:
      // Wait for the first mount to settle, then for frames to paint it.
      if (mm.mountCount() == 0 || !app.host->isIdle() ||
          ++app.frames < kFramesAfterMount) {
        break;
      }
      check_app(true);
      next_check(Phase::Initial);
      break;
    case Phase::Reloading:
      if (app.host->instanceCount() <= app.instances_before ||
          mm.mountCount() <= app.mounts_before ||
          mm.mountedViewCount() < app.views_before || !app.host->isIdle() ||
          ++app.frames < kFramesAfterMount) {
        break;
      }
      printf("reloaded (views %zu, JS instances %d)\n", mm.mountedViewCount(),
             app.host->instanceCount());
      check(app.host->instanceCount() == app.instances_before + 1,
            "reload created a new JS instance");
      check(mm.mountedViewCount() == app.views_before,
            "reload re-mounted the same views (no leaked widgets)");
      check_app(false);
      app.reloads_done++;
      next_check(Phase::Reloading);
      break;
    case Phase::ExpectText:
      if (!has_text(app.root, opts.expect_text) || !app.host->isIdle()) break;
      printf("saw \"%s\" (JS instances %d)\n", opts.expect_text.c_str(),
             app.host->instanceCount());
      check(true, "fast refresh updated the mounted Text");
      check(app.host->instanceCount() == app.instances_before,
            "fast refresh kept the JS instance (no reload)");
      next_check(Phase::ExpectText);
      break;
    case Phase::ExpectLogBox:
      // A few more frames: LogBox mounts in several commits.
      if (!app.host->isLogBoxShowing() || !app.host->isIdle() ||
          ++app.frames < kFramesAfterMount + 10) {
        break;
      }
      check(true, "LogBox is showing");
      if (opts.logbox_screenshot) {
        GdkTexture *tex = rngtk::render_widget(app.overlay);
        check(rngtk::save_png(tex, opts.logbox_screenshot),
              "LogBox screenshot saved");
        g_clear_object(&tex);
      }
      next_check(Phase::ExpectLogBox);
      break;
    case Phase::Done:
      return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

gboolean on_timeout(gpointer) {
  const char *what = app.phase == Phase::Reloading      ? "the reload"
                     : app.phase == Phase::ExpectText   ? "the expected text"
                     : app.phase == Phase::ExpectLogBox ? "LogBox"
                                                        : "the first mount";
  fprintf(stderr, "FAIL timed out after %d ms waiting for %s\n",
          opts.timeout_ms, what);
  if (app.host->devUI() && !app.host->devUI()->bannerText().empty()) {
    fprintf(stderr, "dev banner: %s\n", app.host->devUI()->bannerText().c_str());
  }
  app.exit_code = 1;
  app.timeout_id = 0;
  g_application_quit(g_application_get_default());
  return G_SOURCE_REMOVE;
}

void add_action(GActionMap *map, const char *name, void (*fn)()) {
  GSimpleAction *action = g_simple_action_new(name, nullptr);
  g_signal_connect(action, "activate",
                   G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer fn) {
                     reinterpret_cast<void (*)()>(fn)();
                   }),
                   reinterpret_cast<gpointer>(fn));
  g_action_map_add_action(map, G_ACTION(action));
  g_object_unref(action);
}

void add_shortcut(GtkShortcutController *controller, const char *trigger,
                  const char *action) {
  gtk_shortcut_controller_add_shortcut(
      controller, gtk_shortcut_new(gtk_shortcut_trigger_parse_string(trigger),
                                   gtk_named_action_new(action)));
}

// The dev menu's actions and Ctrl+R / Ctrl+D / Ctrl+M.
void add_dev_controls(GtkWidget *window) {
  GSimpleActionGroup *group = g_simple_action_group_new();
  add_action(G_ACTION_MAP(group), "reload", [] { app.host->reload(); });
  add_action(G_ACTION_MAP(group), "open-debugger",
             [] { app.host->openDebugger(); });
  add_action(G_ACTION_MAP(group), "menu", [] { app.host->showDevMenu(); });
  gtk_widget_insert_action_group(window, "dev", G_ACTION_GROUP(group));
  g_object_unref(group);

  GtkEventController *controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller),
                                    GTK_SHORTCUT_SCOPE_GLOBAL);
  auto *shortcuts = GTK_SHORTCUT_CONTROLLER(controller);
  add_shortcut(shortcuts, "<Control>r", "dev.reload");
  add_shortcut(shortcuts, "<Control>d", "dev.menu");
  add_shortcut(shortcuts, "<Control>m", "dev.menu");
  gtk_widget_add_controller(window, controller);

  if (opts.verbose) {
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(+[](GtkEventControllerKey *, guint keyval,
                                    guint, GdkModifierType state, gpointer) {
                       LOG(INFO) << "key " << gdk_keyval_name(keyval)
                                 << " modifiers " << state;
                       return FALSE;
                     }),
                     nullptr);
    gtk_widget_add_controller(window, keys);
  }
}

void activate(GtkApplication *gtk_app, gpointer) {
  GtkWidget *window = gtk_application_window_new(gtk_app);
  gtk_window_set_title(GTK_WINDOW(window), opts.module.c_str());
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  app.overlay = gtk_overlay_new();
  app.root = rn_view_new();
  gtk_overlay_set_child(GTK_OVERLAY(app.overlay), app.root);
  gtk_window_set_child(GTK_WINDOW(window), app.overlay);

  rngtk::RNGtkHostOptions host_options{
      .isTesting = opts.self_test,
      .devMode = opts.dev,
      .devServerHost = opts.dev_host,
      .devServerPort = opts.dev_port,
      .inspector = opts.inspector,
  };
  app.host = new rngtk::RNGtkHost(host_options, GTK_OVERLAY(app.overlay));
  if (opts.dev) add_dev_controls(window);
  if (!app.host->run(opts.dev ? opts.entry : opts.bundle, kSurfaceId,
                     opts.module, app.root, opts.width, opts.height)) {
    fprintf(stderr, "could not load %s\n", opts.bundle.c_str());
    app.exit_code = 1;
    g_application_quit(G_APPLICATION(gtk_app));
    return;
  }

  if (opts.self_test || opts.screenshot) {
    gtk_widget_add_tick_callback(app.root, on_tick, nullptr, nullptr);
    restart_timeout();
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
          "usage: rn-gtk-host --bundle FILE [options]\n"
          "       rn-gtk-host --dev-server [HOST:PORT] [--entry index]\n"
          "                   [--no-inspector] [options]\n"
          "options: [--module NAME] [--width N] [--height N] [--self-test]\n"
          "         [--screenshot PNG] [--timeout MS] [--test-reload]\n"
          "         [--expect-reload]\n"
          "         [--expect-text TEXT] [--expect-logbox]\n"
          "         [--logbox-screenshot PNG] [--verbose]\n");
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  // Test drivers read progress lines while the host runs.
  setvbuf(stdout, nullptr, _IOLBF, 0);
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
    else if (arg("--entry")) opts.entry = argv[++i];
    else if (arg("--expect-text")) opts.expect_text = argv[++i];
    else if (arg("--logbox-screenshot")) opts.logbox_screenshot = argv[++i];
    else if (!strcmp(argv[i], "--self-test")) opts.self_test = true;
    else if (!strcmp(argv[i], "--test-reload")) opts.test_reload = true;
    else if (!strcmp(argv[i], "--expect-reload")) opts.expect_reload = true;
    else if (!strcmp(argv[i], "--expect-logbox")) opts.expect_logbox = true;
    else if (!strcmp(argv[i], "--no-inspector")) opts.inspector = false;
    else if (!strcmp(argv[i], "--verbose")) opts.verbose = true;
    else if (!strcmp(argv[i], "--dev-server")) {
      opts.dev = true;
      // Optional HOST:PORT.
      if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
        std::string server = argv[++i];
        auto colon = server.rfind(':');
        opts.dev_host = server.substr(0, colon);
        if (colon != std::string::npos) {
          opts.dev_port = atoi(server.c_str() + colon + 1);
        }
      }
    } else {
      return usage();
    }
  }
  // Exactly one of --bundle and --dev-server.
  if (opts.bundle.empty() == !opts.dev) return usage();
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = opts.verbose ? 0 : 1;  // info, or warnings and up
  set_up_feature_flags();

  GtkApplication *gtk_app = gtk_application_new(
      "dev.curiosity26.RNGtk4.Host", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), nullptr);
  int status = g_application_run(G_APPLICATION(gtk_app), 1, argv);
  delete app.host;
  g_object_unref(gtk_app);
  return status ? status : app.exit_code;
}
