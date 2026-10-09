// rngtk::runApp: the window and command line around RNGtkHost for apps.
#include "rngtk/App.h"

#include <glog/logging.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "DevControls.h"
#include "DevUI.h"
#include "FeatureFlags.h"
#include "GtkMountingManager.h"
#include "RNGtkHost.h"
#include "rn_view.h"

namespace rngtk {
namespace {

constexpr facebook::react::SurfaceId kSurfaceId = 1;
constexpr int kFramesAfterMount = 2;

struct Run {
  AppOptions options;
  bool dev = false;
  std::string bundle;
  bool inspector = true;
  bool smoke = false;
  std::string screenshot;
  int timeoutMs = 120000;
  bool verbose = false;

  RNGtkHost *host = nullptr;
  GtkApplication *gtkApp = nullptr;
  GtkWidget *root = nullptr;
  int frames = 0;
  bool done = false;
  guint timeoutId = 0;
  int exitCode = 0;
};

std::string executableDir() {
  char path[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n <= 0) return ".";
  path[n] = 0;
  std::string s(path);
  return s.substr(0, s.rfind('/'));
}

std::string executableName() {
  char path[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n <= 0) return "";
  path[n] = 0;
  std::string s(path);
  return s.substr(s.rfind('/') + 1);
}

// index.bundle.js next to the executable, or in ../share/<executable>/.
std::string defaultBundle() {
  std::string dir = executableDir();
  for (std::string candidate :
       {dir + "/index.bundle.js",
        dir + "/../share/" + executableName() + "/index.bundle.js"}) {
    if (access(candidate.c_str(), R_OK) == 0) return candidate;
  }
  return "";
}

// Renders the window's content with its GSK renderer and saves a PNG.
bool saveScreenshot(GtkWidget *widget, const char *path) {
  int w = gtk_widget_get_width(widget);
  int h = gtk_widget_get_height(widget);
  GdkPaintable *paintable = gtk_widget_paintable_new(widget);
  GtkSnapshot *snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, w, h);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  g_object_unref(paintable);
  if (!node) return false;
  GskRenderer *renderer =
      gtk_native_get_renderer(gtk_widget_get_native(widget));
  graphene_rect_t viewport;
  graphene_rect_init(&viewport, 0, 0, (float)w, (float)h);
  GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &viewport);
  gsk_render_node_unref(node);
  bool ok = texture && gdk_texture_save_to_png(texture, path);
  g_clear_object(&texture);
  return ok;
}

void finish(Run *run, int exitCode) {
  run->done = true;
  run->exitCode = exitCode;
  if (run->timeoutId) g_source_remove(run->timeoutId);
  run->timeoutId = 0;
  g_application_quit(G_APPLICATION(run->gtkApp));
}

// --smoke: after each painted frame, until the first mount has settled.
void onAfterPaint(GdkFrameClock *, gpointer data) {
  auto *run = static_cast<Run *>(data);
  if (run->done) return;
  if (run->host->loadFailed()) {
    fprintf(stderr, "SMOKE FAIL the bundle did not load\n");
    if (run->host->devUI() && !run->host->devUI()->bannerText().empty()) {
      fprintf(stderr, "dev banner: %s\n",
              run->host->devUI()->bannerText().c_str());
    }
    finish(run, 1);
    return;
  }
  auto &mm = run->host->mountingManager();
  if (mm.mountCount() == 0 && run->host->jsErrorCount() > 0 &&
      run->host->isIdle()) {
    fprintf(stderr, "SMOKE FAIL a JS error before the first mount\n");
    if (run->host->devUI() && !run->host->devUI()->bannerText().empty()) {
      fprintf(stderr, "dev banner: %s\n",
              run->host->devUI()->bannerText().c_str());
    }
    finish(run, 1);
    return;
  }
  if (mm.mountCount() == 0 || !run->host->isIdle() ||
      ++run->frames < kFramesAfterMount) {
    return;
  }
  size_t views = mm.mountedViewCount();
  int errors = run->host->jsErrorCount();
  if (views == 0 || errors > 0) {
    fprintf(stderr, "SMOKE FAIL views %zu, JS errors %d\n", views, errors);
    finish(run, 1);
    return;
  }
  if (!run->screenshot.empty() &&
      !saveScreenshot(run->root, run->screenshot.c_str())) {
    fprintf(stderr, "SMOKE FAIL could not save %s\n", run->screenshot.c_str());
    finish(run, 1);
    return;
  }
  printf("SMOKE OK module=%s source=%s views=%zu\n",
         run->options.moduleName.c_str(), run->dev ? "metro" : "bundle",
         views);
  finish(run, 0);
}

gboolean onTick(GtkWidget *, GdkFrameClock *clock, gpointer data) {
  auto *run = static_cast<Run *>(data);
  if (!g_object_get_data(G_OBJECT(clock), "rngtk-smoke")) {
    g_object_set_data(G_OBJECT(clock), "rngtk-smoke", run);
    g_signal_connect(clock, "after-paint", G_CALLBACK(onAfterPaint), run);
  }
  return run->done ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

gboolean onTimeout(gpointer data) {
  auto *run = static_cast<Run *>(data);
  run->timeoutId = 0;
  fprintf(stderr, "SMOKE FAIL timed out after %d ms waiting for the first mount\n",
          run->timeoutMs);
  if (run->host->devUI() && !run->host->devUI()->bannerText().empty()) {
    fprintf(stderr, "dev banner: %s\n", run->host->devUI()->bannerText().c_str());
  }
  finish(run, 1);
  return G_SOURCE_REMOVE;
}

void activate(GtkApplication *gtkApp, gpointer data) {
  auto *run = static_cast<Run *>(data);
  if (run->host) {
    // A unique app activated again: show the existing window.
    GtkWindow *window = gtk_application_get_active_window(gtkApp);
    if (window) gtk_window_present(window);
    return;
  }
  const AppOptions &o = run->options;
  GtkWidget *window = gtk_application_window_new(gtkApp);
  gtk_window_set_title(GTK_WINDOW(window),
                       (o.title.empty() ? o.moduleName : o.title).c_str());
  // The surface has a fixed size for now.
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  GtkWidget *overlay = gtk_overlay_new();
  run->root = rn_view_new();
  gtk_widget_set_halign(run->root, GTK_ALIGN_START);
  gtk_widget_set_valign(run->root, GTK_ALIGN_START);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), run->root);
  gtk_window_set_child(GTK_WINDOW(window), overlay);

  RNGtkHostOptions hostOptions{
      .appId = o.appId.empty() ? "dev.curiosity26.RNGtk4.App" : o.appId,
      .isTesting = run->smoke,
      .devMode = run->dev,
      .devServerHost = o.devServerHost,
      .devServerPort = static_cast<uint32_t>(o.devServerPort),
      .inspector = run->inspector,
  };
  run->host = new RNGtkHost(hostOptions, GTK_OVERLAY(overlay));
  if (run->dev) addDevControls(window, run->host, run->verbose);
  if (!run->host->run(run->dev ? o.entry : run->bundle, kSurfaceId,
                      o.moduleName, run->root, o.width, o.height)) {
    fprintf(stderr, "could not load %s\n", run->bundle.c_str());
    run->exitCode = 1;
    g_application_quit(G_APPLICATION(gtkApp));
    return;
  }
  if (run->smoke) {
    gtk_widget_add_tick_callback(run->root, onTick, run, nullptr);
    run->timeoutId = g_timeout_add(run->timeoutMs, onTimeout, run);
  }
  gtk_window_present(GTK_WINDOW(window));
}

int usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [--dev-server [HOST:PORT] | --bundle FILE]\n"
          "          [--smoke [--screenshot PNG] [--timeout MS]]\n"
          "          [--no-inspector] [--verbose]\n",
          argv0);
  return 2;
}

}  // namespace

const char *reactNativeVersion() { return RNGTK_RN_VERSION; }

int runApp(int argc, char **argv, const AppOptions &options) {
  // Test drivers read progress lines while the app runs.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  Run run;
  run.options = options;
  run.dev = options.devServerByDefault;
  for (int i = 1; i < argc; i++) {
    auto arg = [&](const char *name) {
      return !strcmp(argv[i], name) && i + 1 < argc;
    };
    if (arg("--bundle")) {
      run.bundle = argv[++i];
      run.dev = false;
    } else if (arg("--screenshot")) {
      run.screenshot = argv[++i];
    } else if (arg("--timeout")) {
      run.timeoutMs = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--smoke")) {
      run.smoke = true;
    } else if (!strcmp(argv[i], "--no-inspector")) {
      run.inspector = false;
    } else if (!strcmp(argv[i], "--verbose")) {
      run.verbose = true;
    } else if (!strcmp(argv[i], "--dev-server")) {
      run.dev = true;
      // Optional HOST:PORT (or :PORT).
      if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
        std::string server = argv[++i];
        auto colon = server.rfind(':');
        if (colon != 0) run.options.devServerHost = server.substr(0, colon);
        if (colon != std::string::npos) {
          run.options.devServerPort = atoi(server.c_str() + colon + 1);
        }
      }
    } else {
      return usage(argv[0]);
    }
  }
  if (!run.dev && run.bundle.empty()) {
    run.bundle = defaultBundle();
    if (run.bundle.empty()) {
      fprintf(stderr,
              "%s: no index.bundle.js next to the executable; pass --bundle "
              "FILE or --dev-server\n",
              argv[0]);
      return 1;
    }
  }

  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = run.verbose ? 0 : 1;  // info, or warnings and up
  setUpFeatureFlags();

  // Smoke runs may overlap a running copy of the app.
  auto flags = run.smoke ? G_APPLICATION_NON_UNIQUE : G_APPLICATION_DEFAULT_FLAGS;
  const char *appId = options.appId.empty() ? nullptr : options.appId.c_str();
  if (appId && !g_application_id_is_valid(appId)) {
    fprintf(stderr, "invalid application id \"%s\"\n", appId);
    appId = nullptr;
  }
  run.gtkApp = gtk_application_new(appId, flags);
  g_signal_connect(run.gtkApp, "activate", G_CALLBACK(activate), &run);
  // GApplication only sees the program name: the options are ours.
  int status = g_application_run(G_APPLICATION(run.gtkApp), 1, argv);
  delete run.host;
  g_object_unref(run.gtkApp);
  return status ? status : run.exitCode;
}

}  // namespace rngtk
