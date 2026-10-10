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
//                          no leaked widgets or threads
//   --reloads N            how many such reloads (default 1)
//   --expect-reload        then print "READY expect-reload", wait for a
//                          reload from outside (Metro's `r`), check again
//   --expect-text TEXT     print "READY expect-text", then wait for a Text
//                          containing TEXT, without a reload (fast refresh)
//   --expect-logbox        print "READY expect-logbox", then wait for LogBox
//   --logbox-screenshot F  save the window once LogBox shows
//   --dismiss-logbox       then click LogBox's Dismiss button and wait for
//                          LogBox to close
//   --system-appearance    follow the desktop's light/dark style (self-tests
//                          are light otherwise); GalleryAppearance then
//                          flips GNOME's color-scheme and restores it
//   --system-accessibility follow the AT-SPI bus's screen reader state;
//                          GalleryAccessibility then flips it (without
//                          starting Orca) and restores it
//   --url URL              Linking.getInitialURL()
//   --rtl                  GalleryPlatform: start with forceRTL(true) saved
//   --test-animation       before the first reload and after each one,
//                          hold the card (a TouchableOpacity) and check
//                          that its native-driver fade runs, then release
//   --step-delay MS        wait before each self-test step (to watch it, or
//                          to screenshot the desktop)
#include <ReactCommon/TurboModule.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/propsConversions.h>
#include <rngtk/Extensions.h>
#include <glib/gstdio.h>
#include <glog/logging.h>
#include <folly/json.h>
#include <libsoup/soup.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>
#include <functional>
#include <string>
#include <vector>

#include "AccessibilityInfo.h"
#include "Appearance.h"
#include "DevControls.h"
#include "DevUI.h"
#include "FeatureFlags.h"
#include "GtkMountingManager.h"
#include "GtkKeyboardHandler.h"
#include "GtkPointerHandler.h"
#include "PangoText.h"
#include "rn_scroll_view.h"
#include "rn_text_input.h"
#include "RNGtkHost.h"
#include "harness.h"
#include "rn_text.h"
#include "rn_view.h"

using namespace facebook::react;

// template-library/linux/src/ExamplePackage.cc
std::shared_ptr<const rngtk::Package> example_package();
std::shared_ptr<const rngtk::Package> rngtk_gesture_handler_package();
#ifdef RNGTK_HARNESS_SCREENS
std::shared_ptr<const rngtk::Package> rngtk_screens_package();
#endif

namespace {

constexpr SurfaceId kSurfaceId = 1;
constexpr int kFramesAfterMount = 2;

struct Options {
  std::string bundle;
  std::string initial_props;  // JSON
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
  int reloads = 1;  // with --test-reload
  bool expect_reload = false;
  std::string expect_text;
  bool expect_logbox = false;
  const char *logbox_screenshot = nullptr;
  bool dismiss_logbox = false;
  bool test_animation = false;
  // Follow the desktop's light/dark style in a self-test too; with
  // GalleryAppearance, flip GNOME's color-scheme setting and follow it.
  bool system_appearance = false;
  // GalleryAccessibility: flip the AT-SPI bus's ScreenReaderEnabled (what
  // GNOME sets while Orca runs; Orca isn't started) and follow it.
  bool system_accessibility = false;
  // Linking.getInitialURL() (as if the app was started with this URL).
  std::string url;
  // GalleryPlatform: start with I18nManager.forceRTL(true) saved.
  bool rtl = false;
  // The main window's title bar ('default', 'hidden', 'none'; GalleryTitleBar
  // defaults to 'hidden') and transparency.
  std::string title_bar;
  bool transparent = false;
  int timeout_ms = 20000;
  // Waits this long before each self-test step (to watch, or take
  // screenshots of the desktop).
  int step_delay_ms = 0;
  bool verbose = false;
} opts;

enum class Phase { Initial, Reloading, ExpectText, ExpectLogBox, Steps, Done };

// An asynchronous self-test step: start() once, then done() each frame
// until it returns true.
struct Step {
  std::string name;
  std::function<void()> start;
  std::function<bool()> done;
};

struct App {
  rngtk::RNGtkHost *host = nullptr;
  GtkWidget *root = nullptr;
  GtkWidget *overlay = nullptr;
  GtkWidget *window = nullptr;
  Phase phase = Phase::Initial;
  int frames = 0;
  int mounts_before = 0;
  size_t views_before = 0;
  int instances_before = 0;
  bool reload_external = false;
  int threads_after_first_reload = 0;
  int reloads_done = 0;
  guint timeout_id = 0;
  int exit_code = 0;
  std::vector<Step> steps;
  size_t step = 0;
  bool step_started = false;
  // What the current steps follow; next_check() continues from it.
  Phase steps_after = Phase::Initial;
  // The JS instance whose native animation --test-animation checked.
  int animated_instance = 0;
} app;

int thread_count() {
  int n = 0;
  if (GDir *dir = g_dir_open("/proc/self/task", 0, nullptr)) {
    while (g_dir_read_name(dir)) n++;
    g_dir_close(dir);
  }
  return n;
}

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

// The first paragraph under `widget` whose text is exactly `text`.
GtkWidget *find_text(GtkWidget *widget, const std::string &text) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (RN_IS_TEXT(c) && text == rn_text_get_text(RN_TEXT(c))) return c;
    if (GtkWidget *found = find_text(c, text)) return found;
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

// The texture's pixels, downloaded once (texture_pixel downloads it all on
// every call).
struct Pixels {
  GdkTexture *tex = nullptr;
  int w = 0, h = 0;
  std::vector<uint8_t> data;
  rngtk::Rgba8 at(int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return {};
    const uint8_t *p = &data[(size_t(y) * w + x) * 4];
    return rngtk::Rgba8{p[0], p[1], p[2], p[3]};
  }
} pixels;

rngtk::Rgba8 px(GdkTexture *tex, float x, float y) {
  if (pixels.tex != tex) {
    pixels.tex = tex;
    pixels.w = gdk_texture_get_width(tex);
    pixels.h = gdk_texture_get_height(tex);
    pixels.data.resize(size_t(pixels.w) * pixels.h * 4);
    GdkTextureDownloader *dl = gdk_texture_downloader_new(tex);
    gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
    gdk_texture_downloader_download_into(dl, pixels.data.data(),
                                         size_t(pixels.w) * 4);
    gdk_texture_downloader_free(dl);
  }
  return pixels.at(int(x), int(y));
}

bool has_dark_pixel(GdkTexture *tex, graphene_rect_t r) {
  for (int y = 0; y < (int)r.size.height; y++) {
    for (int x = 0; x < (int)r.size.width; x++) {
      auto p = px(tex, r.origin.x + x, r.origin.y + y);
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


// ---------------------------------------------------------------------------
// Gallery (examples/hello-world/Gallery.js) checks

bool is_gallery() { return opts.module == "Gallery"; }

// The mounted view with nativeID `id`: the one on screen when several
// have it (screens of a navigation stack).
GtkWidget *mapped_with_id(GtkWidget *widget, const char *id) {
  auto &mm = app.host->mountingManager();
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (!gtk_widget_get_mapped(c)) continue;
    auto props = std::dynamic_pointer_cast<const facebook::react::ViewProps>(
        mm.propsForTag(mm.targetForView(c).tag));
    if (props && props->nativeId == id && mm.viewForTag(mm.targetForView(c).tag) == c) return c;
    if (GtkWidget *found = mapped_with_id(c, id)) return found;
  }
  return nullptr;
}

GtkWidget *by_id(const char *id) {
  GtkWidget *shown = app.root ? mapped_with_id(app.root, id) : nullptr;
  return shown ? shown : app.host->mountingManager().viewForNativeId(id);
}

bool near_color(rngtk::Rgba8 p, int r, int g, int b, int tol = 40) {
  return rngtk::near(p, rngtk::Rgba8{uint8_t(r), uint8_t(g), uint8_t(b), 0xFF},
                     tol);
}

// Some pixel in `r` matches `pred`.
template <typename Pred>
bool any_pixel(GdkTexture *tex, graphene_rect_t r, Pred pred) {
  for (int y = int(r.origin.y); y < int(r.origin.y + r.size.height); y++) {
    for (int x = int(r.origin.x); x < int(r.origin.x + r.size.width); x++) {
      if (pred(px(tex, float(x), float(y)))) return true;
    }
  }
  return false;
}

// Every RNText draws the layout Yoga measured: the drawn layout at the
// frame's width has the frame's height (within a pixel) and fits its width.
void check_text_parity(GtkWidget *widget, int *checked, int *mismatched) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (RN_IS_TEXT(c)) {
      graphene_rect_t f = rn_widget_get_frame(c);
      float in[4];
      rn_text_get_insets(RN_TEXT(c), in);
      f.size.width -= in[1] + in[3];
      f.size.height -= in[0] + in[2];
      PangoLayout *layout = rn_text_get_layout(RN_TEXT(c));
      float w = 0, h = 0;
      rngtk::pango_layout_size_px(layout, &w, &h);
      (*checked)++;
      if (std::abs(h - f.size.height) > 1 || w > f.size.width + 1) {
        (*mismatched)++;
        printf("  text \"%.30s\": frame %.0fx%.0f, drawn %.0fx%.0f\n",
               rn_text_get_text(RN_TEXT(c)), f.size.width, f.size.height, w, h);
      }
    }
    check_text_parity(c, checked, mismatched);
  }
}

void verify_gallery(GdkTexture *tex) {
  auto &mm = app.host->mountingManager();
  printf("mounted views: %zu, js errors: %d\n", mm.mountedViewCount(),
         app.host->jsErrorCount());
  check(app.host->jsErrorCount() == 0, "no JS errors");
  if (!check(tex && gdk_texture_get_width(tex) == opts.width &&
                 gdk_texture_get_height(tex) == opts.height,
             "root rendered at the surface size") &&
      tex) {
    printf("  rendered %dx%d, root allocated %dx%d\n", gdk_texture_get_width(tex),
           gdk_texture_get_height(tex), gtk_widget_get_width(app.root),
           gtk_widget_get_height(app.root));
  }
  if (!tex) return;
  const int bg[3] = {0xF5, 0xF5, 0xF7};

  if (GtkWidget *v = by_id("borders")) {
    graphene_rect_t b = bounds_in_root(v);
    float x = b.origin.x, y = b.origin.y, w = b.size.width, h = b.size.height;
    check(near_color(px(tex, x + w / 2, y + 1), 255, 0, 0) &&
              near_color(px(tex, x + w - 3, y + h / 2), 0, 192, 0) &&
              near_color(px(tex, x + w / 2, y + h - 5), 0, 0, 255) &&
              near_color(px(tex, x + 7, y + h / 2), 255, 149, 0) &&
              near_color(px(tex, x + w / 2, y + h / 2), 255, 255, 255),
          "per-side border widths and colors");
    // 16px left border: still orange at x+14, white past it.
    check(near_color(px(tex, x + 14, y + h / 2), 255, 149, 0) &&
              near_color(px(tex, x + 18, y + h / 2), 255, 255, 255),
          "left border is 16px wide");
  } else {
    check(false, "borders view mounted");
  }

  // Button (overrides/Libraries/Components/Button.linux.js): Adwaita's
  // neutral button with dark text; `color` with white text; disabled at
  // half opacity over the background, its text no longer dark.
  {
    GtkWidget *plain = by_id("button"), *color = by_id("button-color"),
              *disabled = by_id("button-disabled");
    if (check(plain && color && disabled, "Buttons mounted")) {
      auto fill = [&](GtkWidget *v) {
        graphene_rect_t b = bounds_in_root(v);
        return px(tex, b.origin.x + 6, b.origin.y + b.size.height / 2);
      };
      auto has = [&](GtkWidget *v, auto pred) {
        return any_pixel(tex, bounds_in_root(v), pred);
      };
      auto dark = [](rngtk::Rgba8 p) { return p.r < 0x70 && p.g < 0x70 && p.b < 0x70; };
      auto white = [](rngtk::Rgba8 p) { return p.r > 0xF0 && p.g > 0xF0 && p.b > 0xF0; };
      graphene_rect_t b = bounds_in_root(plain);
      printf("  Button %.0fx%.0f\n", b.size.width, b.size.height);
      check(near_color(fill(plain), 0xE6, 0xE6, 0xE6, 4) && has(plain, dark) &&
                b.size.height >= 34,
            "Button: neutral background, dark text, 34px tall");
      check(near_color(fill(color), 0x35, 0x84, 0xE4, 4) && has(color, white),
            "Button color: that background, white text");
      check(near_color(fill(disabled), 0xEE, 0xEE, 0xEF, 4) && !has(disabled, dark),
            "Button disabled: dimmed");
      // Rounded: the corner pixel is the background, not the button.
      check(near_color(px(tex, b.origin.x, b.origin.y), bg[0], bg[1], bg[2], 6),
            "Button corners are rounded");
    }
  }

  if (GtkWidget *v = by_id("radii")) {
    graphene_rect_t b = bounds_in_root(v);
    float x = b.origin.x, y = b.origin.y, w = b.size.width, h = b.size.height;
    check(near_color(px(tex, x + 1, y + 1), 0x58, 0x56, 0xD6) &&
              near_color(px(tex, x + w - 2, y + 2), bg[0], bg[1], bg[2], 12) &&
              near_color(px(tex, x + w - 4, y + h - 4), bg[0], bg[1], bg[2], 12),
          "per-corner radii (square top-left, rounded right corners)");
  }

  if (GtkWidget *v = by_id("clip")) {
    graphene_rect_t b = bounds_in_root(v);
    float x = b.origin.x, y = b.origin.y, w = b.size.width, h = b.size.height;
    check(near_color(px(tex, x + w / 2, y + h / 2), 0xFF, 0x3B, 0x30) &&
              near_color(px(tex, x + 3, y + 3), bg[0], bg[1], bg[2], 12) &&
              near_color(px(tex, x + w - 3, y + h - 3), bg[0], bg[1], bg[2], 12),
          "overflow hidden clips children to the rounded corners");
  }

  if (GtkWidget *v = by_id("shadow")) {
    graphene_rect_t b = bounds_in_root(v);
    rngtk::Rgba8 below = px(tex, b.origin.x + b.size.width / 2,
                            b.origin.y + b.size.height + 8);
    printf("  shadow below the box: %d,%d,%d\n", below.r, below.g, below.b);
    check(below.r < bg[0] - 40 && below.g < bg[1] - 40,
          "boxShadow drawn below the view");
  }

  if (GtkWidget *v = by_id("inset")) {
    graphene_rect_t b = bounds_in_root(v);
    check(px(tex, b.origin.x + 2, b.origin.y + b.size.height / 2).b > 200 &&
              near_color(px(tex, b.origin.x + b.size.width / 2,
                            b.origin.y + b.size.height / 2),
                         255, 255, 255, 30),
          "inset boxShadow inside the edges");
  }

  if (GtkWidget *v = by_id("translated")) {
    // The layout frame, without the transform...
    graphene_rect_t parent = bounds_in_root(gtk_widget_get_parent(v));
    graphene_rect_t f = rn_widget_get_frame(v);
    float lx = parent.origin.x + f.origin.x, ly = parent.origin.y + f.origin.y;
    // ...and where it's drawn: 40 right, 10 down.
    check(near_color(px(tex, lx + 40 + 30, ly + 10 + 30), 0xFF, 0x2D, 0x55) &&
              near_color(px(tex, lx + 5, ly + 5), bg[0], bg[1], bg[2], 12),
          "transform translateX 40 / translateY 10 moves the view");
    graphene_rect_t b = bounds_in_root(v);
    check(std::abs(b.origin.x - (lx + 40)) < 1 && std::abs(b.origin.y - (ly + 10)) < 1,
          "transformed bounds (used for hit-testing) follow the transform");
  }

  if (GtkWidget *v = by_id("rotated")) {
    graphene_rect_t parent = bounds_in_root(gtk_widget_get_parent(v));
    graphene_rect_t f = rn_widget_get_frame(v);
    float cx = parent.origin.x + f.origin.x + f.size.width / 2;
    float cy = parent.origin.y + f.origin.y + f.size.height / 2;
    // Rotated 45° about its centre: the centre stays, a corner is empty.
    check(near_color(px(tex, cx, cy), 0xFF, 0x2D, 0x55) &&
              near_color(px(tex, cx - f.size.width / 2 + 2,
                            cy - f.size.height / 2 + 2),
                         bg[0], bg[1], bg[2], 12),
          "rotate 45deg turns the view about its centre");
  }

  if (GtkWidget *v = by_id("grayscale")) {
    graphene_rect_t b = bounds_in_root(v);
    rngtk::Rgba8 p = px(tex, b.origin.x + b.size.width / 2, b.origin.y + b.size.height / 2);
    check(std::abs(p.r - p.g) < 6 && std::abs(p.g - p.b) < 6,
          "filter grayscale(1) removes color");
  }

  if (GtkWidget *v = by_id("linear")) {
    graphene_rect_t b = bounds_in_root(v);
    rngtk::Rgba8 l = px(tex, b.origin.x + 2, b.origin.y + b.size.height / 2);
    rngtk::Rgba8 r = px(tex, b.origin.x + b.size.width - 3, b.origin.y + b.size.height / 2);
    check(l.r > 200 && l.b < 60 && r.b > 200 && r.r < 60,
          "linear-gradient(90deg, red, blue) runs left to right");
  }

  if (GtkWidget *v = by_id("dashed")) {
    graphene_rect_t b = bounds_in_root(v);
    int on = 0, off = 0;
    for (int x = int(b.origin.x) + 12; x < int(b.origin.x + b.size.width) - 12; x++) {
      if (px(tex, x, b.origin.y + 1).b > 200 && px(tex, x, b.origin.y + 1).r < 80) on++;
      else off++;
    }
    check(on > 5 && off > 5, "dashed border has gaps");
  }

  if (GtkWidget *v = by_id("nested")) {
    graphene_rect_t b = bounds_in_root(v);
    check(any_pixel(tex, b, [](rngtk::Rgba8 p) { return p.r > 200 && p.g < 60 && p.b < 60; }) &&
              any_pixel(tex, b, [](rngtk::Rgba8 p) { return p.b > 200 && p.r < 60 && p.g < 60; }) &&
              any_pixel(tex, b, [](rngtk::Rgba8 p) { return p.r < 40 && p.g < 40 && p.b < 40; }) &&
              any_pixel(tex, b, [](rngtk::Rgba8 p) { return p.r > 240 && p.g > 220 && p.b < 80; }),
          "nested Text spans draw their own colors (black, red, blue, marked)");
  }

  if (GtkWidget *v = by_id("ellipsis")) {
    PangoLayout *layout = rn_text_get_layout(RN_TEXT(v));
    graphene_rect_t f = rn_widget_get_frame(v);
    check(pango_layout_is_ellipsized(layout) && f.size.height < 14 * 1.6f &&
              pango_layout_get_line_count(layout) == 1,
          "numberOfLines 1 ellipsizes to one line");
  }

  pixels.tex = nullptr;
  if (GtkWidget *v = by_id("pressable")) {
    GdkCursor *cursor = gtk_widget_get_cursor(v);
    check(cursor && !g_strcmp0(gdk_cursor_get_name(cursor), "pointer"),
          "cursor: 'pointer' sets the GTK cursor");
  }

  int texts = 0, mismatched = 0;
  check_text_parity(app.root, &texts, &mismatched);
  printf("  %d texts measured\n", texts);
  check(texts > 10 && mismatched == 0,
        "every Text draws at the size Yoga measured (measure/draw parity)");
}

// The center of a mounted view, in root coordinates.
graphene_point_t center_of(GtkWidget *v) {
  graphene_rect_t b = bounds_in_root(v);
  return graphene_point_t{b.origin.x + b.size.width / 2,
                          b.origin.y + b.size.height / 2};
}

void send(rngtk::GtkPointerHandler::Phase phase, graphene_point_t p) {
  rngtk::GtkPointerHandler::Input input{};
  input.phase = phase;
  input.x = p.x;
  input.y = p.y;
  input.timeMs = uint32_t(g_get_monotonic_time() / 1000);
  app.host->pointerHandler()->dispatch(input);
}

// A click (press + release) on the view `id`, through the same dispatch
// the GTK input controller uses; then wait for `expect` in any Text.
Step click_step(const char *id, std::string expect) {
  return Step{
      std::string("click ") + id + " -> \"" + expect + "\"",
      [id] {
        GtkWidget *v = by_id(id);
        if (!v) return;
        graphene_point_t c = center_of(v);
        send(rngtk::GtkPointerHandler::Phase::Down, c);
        send(rngtk::GtkPointerHandler::Phase::Up, c);
      },
      [expect] { return has_text(app.root, expect); }};
}

rngtk::Rgba8 pixel_at_center(const char *id) {
  GtkWidget *v = by_id(id);
  if (!v) return {};
  GdkTexture *tex = rngtk::render_widget(app.root);
  if (!tex) return {};  // nothing drawn yet this frame
  graphene_point_t c = center_of(v);
  // Off the label: near the left edge, vertically centred.
  graphene_rect_t b = bounds_in_root(v);
  auto p = px(tex, b.origin.x + 6, c.y);
  pixels.tex = nullptr;
  g_object_unref(tex);
  return p;
}

Step after_frames(std::string name, int frames, std::function<bool()> pred);

void add_gallery_input_steps() {
  using Phase = rngtk::GtkPointerHandler::Phase;
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(click_step("pressable", "pressed 1"));
  app.steps.push_back(click_step("pressable", "pressed 2"));
  app.steps.push_back(click_step("opacity", "opacity 1"));
  // Holding TouchableOpacity fades it with a native-driver Animated.timing:
  // C++ Animated drives the widget's opacity frame by frame, without a
  // React commit.
  app.steps.push_back(Step{
      "TouchableOpacity fades while held (native Animated)",
      [] {
        if (GtkWidget *v = by_id("opacity")) send(Phase::Down, center_of(v));
      },
      [] {
        GtkWidget *v = by_id("opacity");
        return v && gtk_widget_get_opacity(v) < 0.5;
      }});
  app.steps.push_back(Step{
      "TouchableOpacity fades back after release",
      [] {
        if (GtkWidget *v = by_id("opacity")) send(Phase::Up, center_of(v));
      },
      [] {
        GtkWidget *v = by_id("opacity");
        return v && gtk_widget_get_opacity(v) > 0.99 &&
               has_text(app.root, "opacity 2");
      }});
  app.steps.push_back(click_step("highlight", "highlight 1"));
  app.steps.push_back(click_step("button", "button 1"));
  app.steps.push_back(Step{
      "a disabled Button doesn't press",
      [] {
        if (GtkWidget *v = by_id("button-disabled")) {
          send(Phase::Down, center_of(v));
          send(Phase::Up, center_of(v));
        }
      },
      [] { return true; }});
  app.steps.push_back(after_frames("  ...the press count stays 1", 15,
                                   [] { return has_text(app.root, "button 1"); }));
  // Right-click on selectable text: a Copy menu that copies its text.
  static std::string clipboard;
  app.steps.push_back(Step{
      "selectable Text: right-click Copy puts the text on the clipboard",
      [] {
        GtkWidget *v = by_id("selectable");
        if (!v) return;
        rngtk::GtkPointerHandler::Input input{};
        graphene_point_t c = center_of(v);
        input.x = c.x;
        input.y = c.y;
        input.button = 3;
        input.phase = Phase::Down;
        app.host->pointerHandler()->dispatch(input);
        input.phase = Phase::Up;
        app.host->pointerHandler()->dispatch(input);
        // The menu is a popover on the root; pick its Copy item.
        for (GtkWidget *c = gtk_widget_get_first_child(app.root); c;
             c = gtk_widget_get_next_sibling(c)) {
          if (GTK_IS_POPOVER(c)) {
            gtk_widget_activate_action(c, "rngtk-text.copy", nullptr);
            gtk_popover_popdown(GTK_POPOVER(c));
          }
        }
        gdk_clipboard_read_text_async(
            gdk_display_get_clipboard(gdk_display_get_default()), nullptr,
            [](GObject *source, GAsyncResult *result, gpointer) {
              char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source),
                                                          result, nullptr);
              clipboard = text ? text : "";
              g_free(text);
            },
            nullptr);
      },
      [] { return clipboard.rfind("Justified text spreads", 0) == 0; }});
  // Hover: onHoverIn turns the Pressable lighter (#3395FF), and back.
  app.steps.push_back(Step{
      "hover in -> #3395FF",
      [] {
        if (GtkWidget *v = by_id("pressable")) send(Phase::Move, center_of(v));
      },
      [] { return near_color(pixel_at_center("pressable"), 0x33, 0x95, 0xFF, 12); }});
  app.steps.push_back(Step{
      "hover out -> #007AFF",
      [] { send(Phase::Move, graphene_point_t{2, 2}); },
      [] { return near_color(pixel_at_center("pressable"), 0x00, 0x7A, 0xFF, 12); }});
  // Button shades 5% darker on hover, like a GTK button.
  app.steps.push_back(Step{
      "Button hover in -> #DBDBDB",
      [] {
        if (GtkWidget *v = by_id("button")) send(Phase::Move, center_of(v));
      },
      [] { return near_color(pixel_at_center("button"), 0xDB, 0xDB, 0xDB, 3); }});
  app.steps.push_back(Step{
      "Button hover out -> #E6E6E6",
      [] { send(Phase::Move, graphene_point_t{2, 2}); },
      [] { return near_color(pixel_at_center("button"), 0xE6, 0xE6, 0xE6, 3); }});
}


// ---------------------------------------------------------------------------
// GalleryLists and GalleryImages checks

bool is_lists() { return opts.module == "GalleryLists"; }
bool is_images() { return opts.module == "GalleryImages"; }

RNScrollView *scroll_by_id(const char *id) {
  GtkWidget *v = by_id(id);
  return v && RN_IS_SCROLL_VIEW(v) ? RN_SCROLL_VIEW(v) : nullptr;
}

double offset_of(const char *id, bool horizontal = false) {
  double x = 0, y = 0;
  if (auto *s = scroll_by_id(id)) rn_scroll_view_get_offset(s, &x, &y);
  return horizontal ? x : y;
}

void wheel(const char *id, double dx, double dy) {
  GtkWidget *v = by_id(id);
  if (!v) return;
  rngtk::GtkPointerHandler::Input input{};
  graphene_point_t c = center_of(v);
  input.phase = rngtk::GtkPointerHandler::Phase::Scroll;
  input.x = c.x;
  input.y = c.y;
  input.dx = dx;
  input.dy = dy;
  app.host->pointerHandler()->dispatch(input);
}

// Waits `frames` painted frames, then checks `pred`.
Step after_frames(std::string name, int frames, std::function<bool()> pred) {
  auto counter = std::make_shared<int>(0);
  return Step{std::move(name), [counter] { *counter = 0; },
              [counter, frames, pred] {
                if (++*counter < frames) return false;
                if (!pred()) {
                  check(false, "(condition above)");
                }
                return true;
              }};
}

// Frame intervals while the 10k FlatList scrolls continuously.
struct ScrollTiming {
  std::vector<double> frames;
  gint64 last = 0;
  size_t maxViews = 0;
  double cpuStartMs = 0;
} timing;

// CPU time this thread (the GTK main thread) has used, in ms.
double thread_cpu_ms() {
  std::ifstream in("/proc/thread-self/stat");
  std::string stat((std::istreambuf_iterator<char>(in)), {});
  auto close = stat.rfind(')');
  if (close == std::string::npos) return 0;
  std::istringstream fields(stat.substr(close + 2));
  std::string f;
  unsigned long long utime = 0, stime = 0;
  for (int i = 3; i <= 15 && fields >> f; i++) {
    if (i == 14) utime = std::stoull(f);
    if (i == 15) stime = std::stoull(f);
  }
  return double(utime + stime) * 1000.0 / sysconf(_SC_CLK_TCK);
}

void add_lists_steps() {
  using Phase = rngtk::GtkPointerHandler::Phase;
  app.host->pointerHandler()->setRealInputEnabled(false);
  auto &mm = app.host->mountingManager();

  app.steps.push_back(Step{
      "wheel scroll moves the ScrollView and onScroll reaches JS",
      [] { wheel("vscroll", 0, 3); },
      [&mm] {
        GtkWidget *v = by_id("vscroll");
        int tag = v ? mm.targetForView(v).tag : 0;
        double y = offset_of("vscroll");
        return y > 0 && mm.scrollEventCount(tag) > 0 &&
               has_text(app.root, "offset " + std::to_string(int(std::lround(y))));
      }});
  app.steps.push_back(Step{
      "scrollTo command (ref.scrollTo({y: 200}))",
      [] {
        GtkWidget *b = by_id("scroll-to-200");
        if (!b) return;
        send(Phase::Down, center_of(b));
        send(Phase::Up, center_of(b));
      },
      [] {
        return std::abs(offset_of("vscroll") - 200) < 0.5 &&
               has_text(app.root, "offset 200");
      }});
  app.steps.push_back(Step{
      "a wheel scroll back to the top",
      [] { wheel("vscroll", 0, -30); },
      [] { return offset_of("vscroll") == 0 && has_text(app.root, "offset 0"); }});
  // Hit-testing through the scroll offset: scroll 15px, press the row
  // where it is now drawn.
  app.steps.push_back(Step{
      "press inside a scrolled ScrollView hits the scrolled row",
      [] {
        rn_scroll_view_scroll_to(scroll_by_id("vscroll"), 0, 15, FALSE);
        GtkWidget *b = by_id("scroll-press");
        if (!b) return;
        graphene_rect_t r = bounds_in_root(b);
        graphene_point_t p{r.origin.x + 20, r.origin.y + r.size.height - 8};
        send(Phase::Down, p);
        send(Phase::Up, p);
      },
      [] { return has_text(app.root, "scroll press 1"); }});
  app.steps.push_back(Step{
      "scrolling during a press cancels it (touchCancel)",
      [] {
        GtkWidget *b = by_id("scroll-press");
        if (!b) return;
        graphene_rect_t r = bounds_in_root(b);
        graphene_point_t p{r.origin.x + 20, r.origin.y + r.size.height - 8};
        send(Phase::Down, p);
        wheel("vscroll", 0, 1);
        send(Phase::Up, p);
      },
      [] { return true; }});
  app.steps.push_back(after_frames(
      "  ...and onPress didn't fire", 20,
      [] { return has_text(app.root, "scroll press 1"); }));
  app.steps.push_back(Step{
      "horizontal ScrollView scrolls sideways",
      [] { wheel("hscroll", 0, 2); },
      [] {
        return offset_of("hscroll", true) > 0 && offset_of("hscroll") == 0; }});

  // FlatList windowing.
  static size_t views_at_start = 0;
  app.steps.push_back(Step{
      "FlatList scrollToIndex(5000) renders that window",
      [&mm] {
        views_at_start = mm.mountedViewCount();
        GtkWidget *b = by_id("list-index");
        if (!b) return;
        send(Phase::Down, center_of(b));
        send(Phase::Up, center_of(b));
      },
      [] {
        return has_text(app.root, "item 5000") &&
               std::abs(offset_of("flatlist") - 5000 * 30) < 1;
      }});
  app.steps.push_back(Step{
      "FlatList scrollToEnd reaches item 9999 and onEndReached",
      [] {
        GtkWidget *b = by_id("list-end");
        if (!b) return;
        send(Phase::Down, center_of(b));
        send(Phase::Up, center_of(b));
      },
      [] {
        return has_text(app.root, "item 9999") &&
               !has_text(app.root, "end reached 0");
      }});
  app.steps.push_back(after_frames(
      "FlatList keeps the mounted view count bounded (windowing)", 10, [&mm] {
        printf("  mounted views: %zu at start, %zu at the end of 10k rows\n",
               views_at_start, mm.mountedViewCount());
        return !has_text(app.root, "item 0 ") &&
               mm.mountedViewCount() < views_at_start + 400;
      }));

  // Sticky section headers follow the native-driver Animated.event.
  app.steps.push_back(Step{
      "SectionList sticky header stays at the top",
      [] { rn_scroll_view_scroll_to(scroll_by_id("sections"), 0, 120, FALSE); },
      [] {
        GtkWidget *list = by_id("sections");
        GtkWidget *header = list ? find_text(list, "section A") : nullptr;
        if (!header) return false;
        graphene_rect_t l = bounds_in_root(list), h = bounds_in_root(header);
        // The header's text sits a few px into the 28px header.
        return h.origin.y >= l.origin.y - 0.5 && h.origin.y < l.origin.y + 20;
      }});

  // Frame times while the 10k list scrolls: a wheel step every frame, fast
  // (a whole notch, ~58 px) and moderate (a quarter notch, ~15 px).
  for (double notch : {1.0, 0.25, 0.0}) {
    app.steps.push_back(Step{
        notch == 0.0 ? "frame timing with no scrolling (baseline)" : notch == 1.0 ? "FlatList (10k rows) scroll timing, 1 notch/frame"
                     : "FlatList (10k rows) scroll timing, 1/4 notch/frame",
        [] {
          rn_scroll_view_scroll_to(scroll_by_id("flatlist"), 0, 0, FALSE);
          timing = ScrollTiming{};
          timing.cpuStartMs = thread_cpu_ms();
        },
        [&mm, notch] {
          GdkFrameClock *clock = gtk_widget_get_frame_clock(app.root);
          gint64 now = gdk_frame_clock_get_frame_time(clock);
          if (timing.last) timing.frames.push_back((now - timing.last) / 1000.0);
          timing.last = now;
          timing.maxViews = std::max(timing.maxViews, mm.mountedViewCount());
          if (timing.frames.size() < 240) {
            wheel("flatlist", 0, notch);
            return false;
          }
                    double cpu = thread_cpu_ms() - timing.cpuStartMs;
          auto stats = rngtk::summarize(timing.frames);
          printf("  scrolled %.0f px in %zu frames (%.0f px/frame): frame p50 "
                 "%.1f ms, p95 %.1f ms, main thread CPU %.1f ms/frame, max "
                 "mounted views %zu\n",
                 offset_of("flatlist"), timing.frames.size(),
                 offset_of("flatlist") / timing.frames.size(), stats.p50,
                 stats.p95, cpu / timing.frames.size(), timing.maxViews);
          return true;
        }});
  }
}

// The http images for GalleryImages: green.png and a 404.
std::string start_image_server() {
  static SoupServer *server = soup_server_new(nullptr, nullptr);
  static GBytes *png = [] {
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 40, 30);
    gdk_pixbuf_fill(pb, 0x00C800FF);
    gchar *buf = nullptr;
    gsize len = 0;
    gdk_pixbuf_save_to_buffer(pb, &buf, &len, "png", nullptr, nullptr);
    g_object_unref(pb);
    return g_bytes_new_take(buf, len);
  }();
  soup_server_add_handler(
      server, nullptr,
      [](SoupServer *, SoupServerMessage *msg, const char *path, GHashTable *,
         gpointer) {
        if (g_strcmp0(path, "/green.png") == 0) {
          soup_server_message_set_status(msg, 200, nullptr);
          soup_server_message_set_response(
              msg, "image/png", SOUP_MEMORY_COPY,
              static_cast<const char *>(g_bytes_get_data(png, nullptr)),
              g_bytes_get_size(png));
        } else {
          soup_server_message_set_status(msg, 404, nullptr);
        }
      },
      nullptr, nullptr);
  GError *error = nullptr;
  if (!soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY,
                                &error)) {
    fprintf(stderr, "image server: %s\n", error->message);
    g_clear_error(&error);
    return "";
  }
  GSList *uris = soup_server_get_uris(server);
  gchar *uri = g_uri_to_string(static_cast<GUri *>(uris->data));
  std::string base = uri;
  g_free(uri);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  if (!base.empty() && base.back() == '/') base.pop_back();
  return base;
}

void verify_images(GdkTexture *tex) {
  check(app.host->jsErrorCount() == 0, "no JS errors");
  if (!tex) return;
  // Pixel (x, y) inside the image view `id`.
  auto at = [&](const char *id, float x, float y) {
    GtkWidget *v = by_id(id);
    if (!v) return rngtk::Rgba8{};
    graphene_rect_t b = bounds_in_root(v);
    return px(tex, b.origin.x + x, b.origin.y + y);
  };
  auto red = [](rngtk::Rgba8 p) { return near_color(p, 255, 0, 0, 30); };
  auto blue = [](rngtk::Rgba8 p) { return near_color(p, 0, 0, 255, 30); };
  auto white = [](rngtk::Rgba8 p) { return near_color(p, 255, 255, 255, 20); };
  // halves.png is 100x50: 25px red, then blue; frames are 160x100.
  check(red(at("mode-stretch", 20, 50)) && blue(at("mode-stretch", 60, 50)) &&
            red(at("mode-stretch", 5, 3)),
        "resizeMode stretch fills the frame (red until x=40)");
  check(white(at("mode-contain", 80, 3)) && red(at("mode-contain", 20, 50)) &&
            blue(at("mode-contain", 60, 50)),
        "resizeMode contain letterboxes (scaled 1.6x, white above)");
  check(red(at("mode-cover", 15, 50)) && blue(at("mode-cover", 35, 50)) &&
            red(at("mode-cover", 15, 2)),
        "resizeMode cover fills and crops (scaled 2x, red until x=30)");
  check(white(at("mode-center", 10, 50)) && red(at("mode-center", 40, 50)) &&
            blue(at("mode-center", 70, 50)),
        "resizeMode center keeps the natural size, centred");
  check(red(at("mode-repeat", 5, 5)) && white(at("mode-repeat", 20, 5)) &&
            red(at("mode-repeat", 35, 5)),
        "resizeMode repeat tiles the 30x30 image");
  check(near_color(at("tint", 20, 50), 0x34, 0xC7, 0x59, 12) &&
            near_color(at("tint", 120, 50), 0x34, 0xC7, 0x59, 12),
        "tintColor recolors the image");
  check(near_color(at("rounded", 2, 2), 0xF5, 0xF5, 0xF7, 12) &&
            blue(at("rounded", 80, 50)),
        "borderRadius clips the image");
  check(near_color(at("data", 80, 50), 0, 200, 0, 12), "data: URI image");
  check(near_color(at("http", 80, 50), 0, 200, 0, 12), "http image (libsoup)");
}

void add_images_steps() {
  app.steps.push_back(Step{
      "onLoad reports the asset's size", [] {},
      [] { return has_text(app.root, "asset loaded 100x50"); }});
  app.steps.push_back(Step{
      "http image: onLoadStart, onLoad (40x30), onLoadEnd", [] {},
      [] {
        return has_text(app.root, "http loaded 40x30") &&
               has_text(app.root, "start,load,end");
      }});
  app.steps.push_back(Step{
      "404 image fires onError", [] {},
      [] { return has_text(app.root, "error HTTP 404"); }});
  app.steps.push_back(Step{
      "defaultSource shows while (and since) the image failed", [] {},
      [] {
        GtkWidget *v = by_id("missing");
        if (!v) return false;
        GdkTexture *tex = rngtk::render_widget(app.root);
        if (!tex) return false;
        graphene_rect_t b = bounds_in_root(v);
        bool red = near_color(px(tex, b.origin.x + 5, b.origin.y + 5), 255, 0, 0, 30);
        pixels.tex = nullptr;
        g_object_unref(tex);
        return red;
      }});
}


// ---------------------------------------------------------------------------
// GalleryAppearance checks

bool is_appearance() { return opts.module == "GalleryAppearance"; }

bool prefer_dark() {
  gboolean dark = FALSE;
  g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
               &dark, nullptr);
  return dark;
}

// The swatches, Button and window_fg_color text show `dark`'s palette.
bool palette_is(bool dark) {
  struct Expect {
    const char *id;
    uint32_t light, dark;
  };
  static const Expect expected[] = {
      {"sw-window", 0xFAFAFA, 0x242424}, {"sw-accent", 0x3584E4, 0x3584E4},
      {"sw-fallback", 0x2EC27E, 0x26A269}, {"sw-unknown", 0x000000, 0x000000},
      {"sw-css", 0xFFFFFF, 0x1E1E1E},    {"sw-at", 0xC01C28, 0xFF7B63},
  };
  GdkTexture *tex = rngtk::render_widget(app.root);
  if (!tex) return false;
  bool ok = true;
  for (const Expect &e : expected) {
    GtkWidget *v = by_id(e.id);
    if (!v) {
      ok = false;
      continue;
    }
    graphene_point_t c = center_of(v);
    uint32_t want = dark ? e.dark : e.light;
    ok &= near_color(px(tex, c.x, c.y), want >> 16, (want >> 8) & 0xFF,
                     want & 0xFF, 6);
  }
  // Button.linux.js: Adwaita's light or dark neutral button.
  if (GtkWidget *b = by_id("button")) {
    graphene_rect_t r = bounds_in_root(b);
    auto p = px(tex, r.origin.x + 6, r.origin.y + r.size.height / 2);
    ok &= dark ? near_color(p, 0x3A, 0x3A, 0x3A, 6)
               : near_color(p, 0xE6, 0xE6, 0xE6, 6);
  } else {
    ok = false;
  }
  // The glyphs on the gray box: near white when dark, near black when
  // light (window_fg_color is white, or 80% black).
  if (GtkWidget *t = by_id("fg-text")) {
    ok &= any_pixel(tex, bounds_in_root(t), [dark](rngtk::Rgba8 p) {
      return dark ? p.r > 230 && p.g > 230 && p.b > 230
                  : p.r < 60 && p.g < 60 && p.b < 60;
    });
  } else {
    ok = false;
  }
  pixels.tex = nullptr;
  g_object_unref(tex);
  return ok;
}

// A light app never runs a "-dark" GTK theme (Yaru-dark -> Yaru).
bool theme_fits(bool dark) {
  gchar *theme = nullptr;
  g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme, nullptr);
  std::string name = theme ? theme : "";
  g_free(theme);
  return dark || name.find("-dark") == std::string::npos;
}

// JS, the palette and GTK's theme variant all agree on `dark`.
bool scheme_is(bool dark) {
  const char *name = dark ? "dark" : "light";
  return has_text(app.root, std::string("scheme: ") + name) && theme_fits(dark) &&
         has_text(app.root, std::string("getColorScheme: ") + name) &&
         prefer_dark() == dark && app.host->appearance().isDark() == dark &&
         palette_is(dark);
}

Step scheme_step(std::string name, std::function<void()> start, bool dark) {
  return Step{std::move(name), std::move(start),
              [dark] { return scheme_is(dark); }};
}

// GNOME's color-scheme setting (what Settings > Appearance writes), to
// restore after --system-appearance flips it.
GSettings *interface_settings() {
  GSettingsSchemaSource *source = g_settings_schema_source_get_default();
  GSettingsSchema *schema =
      source ? g_settings_schema_source_lookup(
                   source, "org.gnome.desktop.interface", TRUE)
             : nullptr;
  if (!schema) return nullptr;
  bool has_key = g_settings_schema_has_key(schema, "color-scheme");
  g_settings_schema_unref(schema);
  return has_key ? g_settings_new("org.gnome.desktop.interface") : nullptr;
}
std::string saved_color_scheme;

void set_desktop_color_scheme(const char *value) {
  GSettings *settings = interface_settings();
  if (!settings) return;
  if (saved_color_scheme.empty()) {
    gchar *old = g_settings_get_string(settings, "color-scheme");
    saved_color_scheme = old;
    g_free(old);
  }
  g_settings_set_string(settings, "color-scheme", value);
  g_settings_sync();
  g_object_unref(settings);
}

void restore_desktop_color_scheme() {
  if (saved_color_scheme.empty()) return;
  if (GSettings *settings = interface_settings()) {
    g_settings_set_string(settings, "color-scheme", saved_color_scheme.c_str());
    g_settings_sync();
    g_object_unref(settings);
  }
}

// ShowcaseDesktop: a harness-only TurboModule for the Showcase's
// Appearance page, which flips GNOME's own dark style (what Settings >
// Appearance writes) so following the system can be tried from the app.
class ShowcaseDesktopModule : public facebook::react::TurboModule {
 public:
  explicit ShowcaseDesktopModule(
      std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : TurboModule("ShowcaseDesktop", std::move(jsInvoker)) {
    using facebook::jsi::Runtime;
    using facebook::jsi::Value;
    // GNOME's color-scheme: 'default', 'prefer-dark', 'prefer-light', or
    // null without GNOME's schema.
    methodMap_["getColorScheme"] = MethodMetadata{
        0, [](Runtime &rt, TurboModule &, const Value *, size_t) -> Value {
          GSettings *settings = interface_settings();
          if (!settings) return Value::null();
          gchar *value = g_settings_get_string(settings, "color-scheme");
          auto result = facebook::jsi::String::createFromUtf8(rt, value);
          g_free(value);
          g_object_unref(settings);
          return result;
        }};
    methodMap_["setColorScheme"] = MethodMetadata{
        1, [](Runtime &rt, TurboModule &, const Value *args,
              size_t count) -> Value {
          if (count < 1 || !args[0].isString()) return Value::undefined();
          auto *value = new std::string(args[0].asString(rt).utf8(rt));
          g_idle_add_once(
              [](gpointer data) {
                auto *value = static_cast<std::string *>(data);
                if (GSettings *settings = interface_settings()) {
                  g_settings_set_string(settings, "color-scheme",
                                        value->c_str());
                  g_settings_sync();
                  g_object_unref(settings);
                }
                delete value;
              },
              value);
          return Value::undefined();
        }};
    // Where the host reads the system's style from: "portal",
    // "gtk-settings" or "none".
    methodMap_["getAppearanceSource"] = MethodMetadata{
        0, [](Runtime &rt, TurboModule &, const Value *, size_t) -> Value {
          return facebook::jsi::String::createFromUtf8(
              rt, app.host->appearance().systemSource());
        }};
  }
};

void click(const char *id);

void add_appearance_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  if (opts.system_appearance) {
    // End to end: GNOME's setting -> the XDG portal -> the host -> JS.
    app.steps.push_back(Step{
        "the system's style comes from the XDG Settings portal", [] {},
        [] {
          printf("  source: %s\n", app.host->appearance().systemSource());
          if (!check(std::string(app.host->appearance().systemSource()) == "portal",
                     "  ...portal found")) {
            app.steps.resize(app.step + 1);  // nothing more to check
          }
          return true;
        }});
    if (!interface_settings()) {
      check(false, "org.gnome.desktop.interface color-scheme exists");
      return;
    }
    app.steps.push_back(scheme_step(
        "gsettings color-scheme prefer-dark: the app turns dark",
        [] { set_desktop_color_scheme("prefer-dark"); }, true));
    app.steps.push_back(scheme_step(
        "gsettings color-scheme prefer-light: the app turns light",
        [] { set_desktop_color_scheme("prefer-light"); }, false));
    app.steps.push_back(scheme_step(
        "an app override (dark) wins over the system",
        [] { click("set-dark"); }, true));
    app.steps.push_back(scheme_step(
        "  ...and 'unspecified' follows the system again",
        [] { click("set-system"); }, false));
    app.steps.push_back(Step{"restore the desktop's color-scheme",
                             [] { restore_desktop_color_scheme(); },
                             [] { return true; }});
    return;
  }
  app.steps.push_back(scheme_step(
      "starts light: PlatformColors resolve to Adwaita's light palette", [] {},
      false));
  app.steps.push_back(scheme_step(
      "setColorScheme('dark'): useColorScheme, PlatformColors, Button and "
      "GTK's dark variant follow",
      [] { click("set-dark"); }, true));
  app.steps.push_back(scheme_step("setColorScheme('light')",
                                  [] { click("set-light"); }, false));
  app.steps.push_back(Step{"setColorScheme('unspecified') clears the override",
                           [] { click("set-system"); },
                           [] {
                             return app.host->appearance().override() ==
                                        rngtk::Appearance::Scheme::Unspecified &&
                                    scheme_is(false);
                           }});
  app.steps.push_back(scheme_step(
      "the system turns dark (as from the portal): the app follows",
      [] { app.host->appearance().setSystemDark(true); }, true));
  app.steps.push_back(scheme_step("a light override wins over a dark system",
                                  [] { click("set-light"); }, false));
  app.steps.push_back(scheme_step("  ...and 'unspecified' is dark again",
                                  [] { click("set-system"); }, true));
  app.steps.push_back(scheme_step(
      "the system turns light again",
      [] { app.host->appearance().setSystemDark(false); }, false));
  app.steps.push_back(Step{"Appearance's change listener heard every change",
                           [] {}, [] { return has_text(app.root, "changes 6"); }});
}

// ---------------------------------------------------------------------------
// GallerySelection checks

bool is_selection() { return opts.module == "GallerySelection"; }

// Root coordinates inside byte `index` of paragraph `id`: its vertical
// middle, at `fx` of its width.
graphene_point_t text_point(const char *id, int index, float fx = 0.5f) {
  GtkWidget *v = by_id(id);
  if (!v || !RN_IS_TEXT(v)) return {};
  PangoRectangle r;
  pango_layout_index_to_pos(rn_text_get_layout(RN_TEXT(v)), index, &r);
  float in[4];
  rn_text_get_insets(RN_TEXT(v), in);
  graphene_point_t p{in[3] + (r.x + r.width * fx) / PANGO_SCALE,
                     in[0] + (r.y + r.height / 2.0f) / PANGO_SCALE},
      out{};
  if (!gtk_widget_compute_point(v, app.root, &p, &out)) return {};
  return out;
}

// Mouse input with its own clock: gestures 1 s apart, clicks within one
// 50 ms apart (a double or triple click).
uint32_t mouse_ms = 1000000;
void mouse(rngtk::GtkPointerHandler::Phase phase, graphene_point_t p,
           int button = 1, GdkModifierType mods = GdkModifierType(0)) {
  rngtk::GtkPointerHandler::Input input{};
  input.phase = phase;
  input.x = p.x;
  input.y = p.y;
  input.button = button;
  input.modifiers = mods;
  input.timeMs = mouse_ms;
  app.host->pointerHandler()->dispatch(input);
}
void clicks(graphene_point_t p, int n, int button = 1,
            GdkModifierType mods = GdkModifierType(0)) {
  using Phase = rngtk::GtkPointerHandler::Phase;
  mouse_ms += 1000;
  for (int i = 0; i < n; i++) {
    mouse(Phase::Down, p, button, mods);
    mouse_ms += 20;
    mouse(Phase::Up, p, button, mods);
    mouse_ms += 30;
  }
}
void drag(graphene_point_t from, graphene_point_t to) {
  using Phase = rngtk::GtkPointerHandler::Phase;
  mouse_ms += 1000;
  mouse(Phase::Down, from);
  for (int i = 1; i <= 4; i++) {
    mouse_ms += 16;
    mouse(Phase::Move, graphene_point_t{from.x + (to.x - from.x) * i / 4,
                                        from.y + (to.y - from.y) * i / 4});
  }
  mouse(Phase::Up, to);
}

std::string selected() { return app.host->pointerHandler()->selectedText(); }

std::string clipboard_text;
void read_clipboard() {
  clipboard_text = "(pending)";
  gdk_clipboard_read_text_async(
      gdk_display_get_clipboard(gdk_display_get_default()), nullptr,
      [](GObject *source, GAsyncResult *result, gpointer) {
        char *text =
            gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, nullptr);
        clipboard_text = text ? text : "";
        g_free(text);
      },
      nullptr);
}

// What Ctrl+C runs: the root's global shortcut.
bool press_ctrl_c() {
  GListModel *controllers = gtk_widget_observe_controllers(app.root);
  bool done = false;
  for (guint i = 0; !done && i < g_list_model_get_n_items(controllers); i++) {
    auto *c = static_cast<GtkEventController *>(g_list_model_get_item(controllers, i));
    if (GTK_IS_SHORTCUT_CONTROLLER(c)) {
      GListModel *items = G_LIST_MODEL(c);
      for (guint j = 0; !done && j < g_list_model_get_n_items(items); j++) {
        auto *shortcut = static_cast<GtkShortcut *>(g_list_model_get_item(items, j));
        char *trigger = gtk_shortcut_trigger_to_string(gtk_shortcut_get_trigger(shortcut));
        if (std::string(trigger) == "<Control>c") {
          done = gtk_shortcut_action_activate(gtk_shortcut_get_action(shortcut),
                                              GTK_SHORTCUT_ACTION_EXCLUSIVE,
                                              app.root, nullptr);
        }
        g_free(trigger);
        g_object_unref(shortcut);
      }
    }
    g_object_unref(c);
  }
  g_object_unref(controllers);
  return done;
}

// The right-click menu's action (copy, select-all) on the root's popover.
void activate_menu(const char *action) {
  for (GtkWidget *c = gtk_widget_get_first_child(app.root); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (GTK_IS_POPOVER(c) && gtk_widget_get_visible(c)) {
      gtk_widget_activate_action(c, action, nullptr);
      gtk_popover_popdown(GTK_POPOVER(c));
    }
  }
}

// Some pixel in the cell of paragraph `id`'s byte `index` is tinted
// (not white, gray or black: a highlight under the text), or `want`.
bool highlighted(const char *id, int index, const rngtk::Rgba8 *want = nullptr) {
  GtkWidget *v = by_id(id);
  GdkTexture *tex = v ? rngtk::render_widget(app.root) : nullptr;
  if (!tex) return false;
  PangoRectangle r;
  pango_layout_index_to_pos(rn_text_get_layout(RN_TEXT(v)), index, &r);
  graphene_point_t a = text_point(id, index, 0), b = text_point(id, index, 1);
  graphene_rect_t cell{{a.x, a.y - r.height / 2.0f / PANGO_SCALE},
                       {b.x - a.x, float(r.height) / PANGO_SCALE}};
  bool found = any_pixel(tex, cell, [want](rngtk::Rgba8 p) {
    if (want) return rngtk::near(p, *want, 12);
    int hi = std::max({p.r, p.g, p.b}), lo = std::min({p.r, p.g, p.b});
    return hi - lo > 25;
  });
  pixels.tex = nullptr;
  g_object_unref(tex);
  return found;
}

void add_selection_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "dragging across selectable text selects it, highlighted",
      [] { drag(text_point("para", 0, 0.1f), text_point("para", 9, 0.9f)); },
      [] {
        return selected() == "Alpha beta" && highlighted("para", 2) &&
               !highlighted("para", 13);
      }});
  app.steps.push_back(Step{
      "Ctrl+C copies the selection",
      [] {
        if (!press_ctrl_c()) check(false, "Ctrl+C handled");
        read_clipboard();
      },
      [] { return clipboard_text == "Alpha beta"; }});
  app.steps.push_back(Step{"double-click selects a word",
                           [] { clicks(text_point("para", 13), 2); },
                           [] { return selected() == "gamma"; }});
  app.steps.push_back(Step{"triple-click selects the paragraph",
                           [] { clicks(text_point("para", 7), 3); },
                           [] { return selected() == "Alpha beta gamma."; }});
  app.steps.push_back(Step{
      "a drag from the second line backwards selects across the newline",
      [] { drag(text_point("para", 23, 0.9f), text_point("para", 11, 0.1f)); },
      [] { return selected() == "gamma.\nSecond"; }});
  app.steps.push_back(Step{
      "Shift+click extends the selection",
      [] {
        clicks(text_point("para", 0, 0.1f), 1);
        clicks(text_point("para", 4, 0.9f), 1, 1, GDK_SHIFT_MASK);
      },
      [] { return selected() == "Alpha"; }});
  app.steps.push_back(Step{
      "right-click Copy copies the selection (not the whole text)",
      [] {
        clicks(text_point("para", 7), 2);  // "beta"
        clicks(text_point("para", 7), 1, 3);
        activate_menu("rngtk-text.copy");
        read_clipboard();
      },
      [] { return clipboard_text == "beta" && selected() == "beta"; }});
  app.steps.push_back(Step{
      "right-click Select All",
      [] {
        clicks(text_point("para", 2), 1, 3);
        activate_menu("rngtk-text.select-all");
      },
      [] { return selected() == "Alpha beta gamma.\nSecond paragraph here."; }});
  app.steps.push_back(Step{
      "a click on non-selectable text presses it and clears the selection",
      [] { clicks(text_point("plain", 4), 1); },
      [] { return selected().empty() && has_text(app.root, "plain presses 1"); }});
  app.steps.push_back(Step{
      "dragging over non-selectable text selects nothing",
      [] { drag(text_point("plain", 0, 0.1f), text_point("plain", 8, 0.9f)); },
      [] { return selected().empty(); }});
  app.steps.push_back(Step{"a click on selectable text in a Pressable presses it",
                           [] { clicks(text_point("wrapped", 3), 1); },
                           [] { return has_text(app.root, "wrapped presses 1"); }});
  app.steps.push_back(Step{
      "a drag over it selects instead (the press is cancelled)",
      [] { drag(text_point("wrapped", 0, 0.1f), text_point("wrapped", 9, 0.9f)); },
      [] { return selected() == "Selectable"; }});
  app.steps.push_back(after_frames("  ...wrapped presses stay 1", 15, [] {
    return has_text(app.root, "wrapped presses 1");
  }));
  app.steps.push_back(Step{
      "selectionColor paints the highlight",
      [] { clicks(text_point("red", 3), 2); },
      [] {
        rngtk::Rgba8 red{0xFF, 0, 0, 0xFF};
        return selected() == "Highlighted" && highlighted("red", 3, &red);
      }});
  app.steps.push_back(Step{
      "the wheel still scrolls a ScrollView of selectable text",
      [] { wheel("scroller", 0, 3); }, [] { return offset_of("scroller") > 0; }});
}

// ---------------------------------------------------------------------------
// GalleryControls checks

void add_resize_steps();

bool is_controls() { return opts.module == "GalleryControls"; }

GtkWidget *editor_of(const char *id) {
  GtkWidget *v = by_id(id);
  return v && RN_IS_TEXT_INPUT(v) ? rn_text_input_get_editor(RN_TEXT_INPUT(v))
                                  : nullptr;
}

std::string input_text(const char *id) {
  GtkWidget *v = by_id(id);
  if (!v || !RN_IS_TEXT_INPUT(v)) return "";
  gchar *t = rn_text_input_get_text(RN_TEXT_INPUT(v));
  std::string s = t;
  g_free(t);
  return s;
}

int caret(const char *id) {
  int start = -1, end = -1;
  if (GtkWidget *v = by_id(id)) rn_text_input_get_selection(RN_TEXT_INPUT(v), &start, &end);
  return end;
}

// Typing as GTK does it: the editable's insert at the caret (what an input
// method's commit and key presses end in).
void type_into(const char *id, const char *text) {
  GtkWidget *editor = editor_of(id);
  if (!editor) return;
  if (GTK_IS_TEXT_VIEW(editor)) {
    gtk_text_buffer_insert_interactive_at_cursor(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor)), text, -1, TRUE);
    return;
  }
  int pos = gtk_editable_get_position(GTK_EDITABLE(editor));
  gtk_editable_insert_text(GTK_EDITABLE(editor), text, -1, &pos);
  gtk_editable_set_position(GTK_EDITABLE(editor), pos);
}

void click(const char *id) {
  GtkWidget *v = by_id(id);
  if (!v) return;
  graphene_point_t c = center_of(v);
  double lx, ly;
  GtkWidget *hit = rn_widget_pick(app.root, c.x, c.y, &lx, &ly);
  if (getenv("RNGTK_DEBUG_CLICKS") && !(hit == v || (hit && gtk_widget_is_ancestor(hit, v)))) {
    printf("click %s at %.0f,%.0f hits %s %d:", id, c.x, c.y, hit ? G_OBJECT_TYPE_NAME(hit) : "nothing",
           hit ? app.host->mountingManager().targetForView(hit).tag : 0);
    for (GtkWidget *w = hit; w && w != app.root; w = gtk_widget_get_parent(w)) {
      printf(" %s(%d,m%d,cv%d)", G_OBJECT_TYPE_NAME(w), app.host->mountingManager().targetForView(w).tag,
             gtk_widget_get_mapped(w), gtk_widget_get_child_visible(w));
    }
    printf("\n");
  }
  send(rngtk::GtkPointerHandler::Phase::Down, c);
  send(rngtk::GtkPointerHandler::Phase::Up, c);
}

// Dark pixels drawn in a view (text ink).
int ink(const char *id) {
  GtkWidget *v = by_id(id);
  GdkTexture *tex = v ? rngtk::render_widget(app.root) : nullptr;
  if (!tex) return -1;
  graphene_rect_t b = bounds_in_root(v);
  int n = 0;
  for (int y = int(b.origin.y) + 3; y < int(b.origin.y + b.size.height) - 3; y++) {
    for (int x = int(b.origin.x) + 3; x < int(b.origin.x + b.size.width) - 3; x++) {
      auto p = px(tex, float(x), float(y));
      if (p.r < 100 && p.g < 100 && p.b < 100) n++;
    }
  }
  pixels.tex = nullptr;
  g_object_unref(tex);
  return n;
}

std::vector<uint8_t> region(const char *id) {
  GtkWidget *v = by_id(id);
  GdkTexture *tex = v ? rngtk::render_widget(app.root) : nullptr;
  std::vector<uint8_t> out;
  if (!tex) return out;
  graphene_rect_t b = bounds_in_root(v);
  for (int y = int(b.origin.y); y < int(b.origin.y + b.size.height); y++) {
    for (int x = int(b.origin.x); x < int(b.origin.x + b.size.width); x++) {
      auto p = px(tex, float(x), float(y));
      out.insert(out.end(), {p.r, p.g, p.b});
    }
  }
  pixels.tex = nullptr;
  g_object_unref(tex);
  return out;
}

void add_controls_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "typing into a controlled TextInput; JS uppercases it back",
      [] { type_into("upper", "hello"); },
      [] {
        return has_text(app.root, "upper: HELLO") && input_text("upper") == "HELLO" &&
               caret("upper") == 5;
      }});
  app.steps.push_back(Step{
      "typing mid-text: the caret stays after the insertion",
      [] {
        gtk_editable_set_position(GTK_EDITABLE(editor_of("upper")), 2);
        type_into("upper", "x");
      },
      [] {
        return has_text(app.root, "upper: HEXLLO") && input_text("upper") == "HEXLLO" &&
               caret("upper") == 3;
      }});
  app.steps.push_back(Step{
      "maxLength 5 stops at five characters",
      [] { type_into("limited", "abcdefgh"); },
      [] { return input_text("limited") == "abcde" && has_text(app.root, "max: abcde"); }});
  app.steps.push_back(Step{
      "secureTextEntry hides the characters", [] {},
      [] {
        int secure = ink("secure"), plain = ink("plain");
        if (secure < 0 || plain < 0) return false;
        printf("  ink: secure %d px, plain %d px\n", secure, plain);
        if (!(secure < plain / 2)) check(false, "secure input draws less ink");
        return true;
      }});
  static float multi_before = 0;
  app.steps.push_back(Step{
      "multiline grows with its content (onContentSizeChange)",
      [] {
        multi_before = rn_widget_get_frame(by_id("multi")).size.height;
        type_into("multi", "one\ntwo\nthree\nfour");
      },
      [] {
        float h = rn_widget_get_frame(by_id("multi")).size.height;
        if (h > multi_before + 30 && !has_text(app.root, "content height 0")) {
          printf("  multiline height %.0f -> %.0f\n", multi_before, h);
          return true;
        }
        return false;
      }});
  app.steps.push_back(Step{
      "Enter submits (onSubmitEditing)",
      [] {
        type_into("submit", "done");
        g_signal_emit_by_name(editor_of("submit"), "activate");
      },
      [] { return has_text(app.root, "submitted: done"); }});
  app.steps.push_back(Step{
      "focus() command focuses the input (onFocus)", [] { click("focus-btn"); },
      [] {
        return has_text(app.root, "focus: yes") && gtk_widget_has_focus(editor_of("focusable"));
      }});
  app.steps.push_back(Step{
      "blur() command (onBlur)", [] { click("blur-btn"); },
      [] {
        return has_text(app.root, "focus: no") && !gtk_widget_has_focus(editor_of("focusable"));
      }});
  app.steps.push_back(Step{
      "pressing a TextInput doesn't press its Pressable parent", [] { click("in-pressable"); },
      [] { return true; }});
  app.steps.push_back(after_frames("  ...parent presses stay 0", 15,
                                   [] { return has_text(app.root, "parent presses 0"); }));
  app.steps.push_back(Step{
      "Switch toggles (onValueChange)",
      [] { gtk_widget_activate(by_id("switch")); },
      [] {
        return has_text(app.root, "switch: on") &&
               gtk_switch_get_active(GTK_SWITCH(by_id("switch")));
      }});
  app.steps.push_back(Step{
      "a Switch controlled to off flips back (setValue)",
      [] { gtk_widget_activate(by_id("locked")); },
      [] {
        return has_text(app.root, "locked attempts 1") &&
               !gtk_switch_get_active(GTK_SWITCH(by_id("locked")));
      }});
  static std::vector<uint8_t> spinner_before;
  app.steps.push_back(Step{
      "ActivityIndicator animates", [] { spinner_before = region("spinner"); },
      [] {
        static int frames = 0;
        if (++frames < 10) return false;
        frames = 0;
        auto now = region("spinner");
        return !now.empty() && now != spinner_before;
      }});
  app.steps.push_back(Step{
      "a stopped ActivityIndicator hides, and shows when animating",
      [] {
        if (gtk_widget_get_visible(by_id("stopped"))) check(false, "stopped spinner hidden");
        click("toggle-spinner");
      },
      [] {
        GtkWidget *s = by_id("stopped");
        return s && gtk_widget_get_visible(s) && gtk_spinner_get_spinning(GTK_SPINNER(s));
      }});
  add_resize_steps();
}

// Dimensions: the window size reaches JS (GalleryControls shows it with
// useWindowDimensions), first as given, then after the window resizes:
// the surface follows the window and JS gets didUpdateDimensions.
void add_resize_steps() {
  app.steps.push_back(Step{
      "useWindowDimensions reports the surface size", [] {},
      [] {
        char text[64];
        snprintf(text, sizeof(text), "window %d x %d", opts.width, opts.height);
        return has_text(app.root, text);
      }});
  app.steps.push_back(Step{
      "resizing the window resizes the surface and reaches JS",
      [] {
        gtk_window_set_resizable(GTK_WINDOW(app.window), TRUE);
        app.host->setFollowsWindowSize(true);
        gtk_window_set_default_size(GTK_WINDOW(app.window), opts.width + 120,
                                    opts.height + 60);
      },
      [] {
        int w = gtk_widget_get_width(app.overlay);
        int h = gtk_widget_get_height(app.overlay);
        if (w <= opts.width || h <= opts.height) return false;
        char text[64];
        snprintf(text, sizeof(text), "window %d x %d", w, h);
        graphene_rect_t f = rn_widget_get_frame(app.root);
        if (!has_text(app.root, text) || f.size.width != w ||
            f.size.height != h) {
          return false;
        }
        printf("  window %dx%d -> %dx%d\n", opts.width, opts.height, w, h);
        return true;
      }});
  app.steps.push_back(Step{
      "  ...and shrinking it again",
      [] {
        gtk_window_set_default_size(GTK_WINDOW(app.window), opts.width - 100,
                                    opts.height - 80);
      },
      [] {
        int w = gtk_widget_get_width(app.overlay);
        int h = gtk_widget_get_height(app.overlay);
        if (w >= opts.width || h >= opts.height) return false;
        char text[64];
        snprintf(text, sizeof(text), "window %d x %d", w, h);
        return has_text(app.root, text) &&
               rn_widget_get_frame(app.root).size.width == w;
      }});
}

// ---------------------------------------------------------------------------
// GalleryKeyboard checks

bool is_keyboard() { return opts.module == "GalleryKeyboard"; }

GtkWidget *focus_widget() {
  return gtk_root_get_focus(GTK_ROOT(app.window));
}

// A key pressed and released through the keyboard handler; true if the
// press was handled (GTK would skip it).
bool key(guint keyval, guint keycode, GdkModifierType mods = GdkModifierType(0)) {
  auto *keys = app.host->keyboardHandler();
  bool handled = keys->keyPressed(keyval, keycode, mods);
  keys->keyReleased(keyval, keycode, mods);
  return handled;
}

// Tab or Shift+Tab: the key reaches the app, then (unless a view handled
// it) GTK moves focus the way GtkWindow's Tab binding does.
constexpr guint kTab = 23, kA = 38, kB = 56, kX = 53, kEnter = 36, kSpace = 65;
bool tab(bool back = false) {
  gtk_window_set_focus_visible(GTK_WINDOW(app.window), TRUE);
  bool handled = key(back ? GDK_KEY_ISO_Left_Tab : GDK_KEY_Tab, kTab,
                     back ? GDK_SHIFT_MASK : GdkModifierType(0));
  if (handled) return true;
  auto dir = back ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;
  if (!gtk_widget_child_focus(app.window, dir)) {
    gtk_window_set_focus(GTK_WINDOW(app.window), nullptr);
    gtk_widget_child_focus(app.window, dir);
  }
  return false;
}

void focus_by_keyboard(const char *id) {
  gtk_window_set_focus_visible(GTK_WINDOW(app.window), TRUE);
  if (GtkWidget *v = by_id(id)) gtk_widget_grab_focus(v);
}

// A focus ring's tint just inside the view's left edge.
bool has_ring(const char *id) {
  GtkWidget *v = by_id(id);
  GdkTexture *tex = v ? rngtk::render_widget(app.root) : nullptr;
  if (!tex) return false;
  graphene_rect_t b = bounds_in_root(v);
  auto p = px(tex, b.origin.x + 1, b.origin.y + b.size.height / 2);
  pixels.tex = nullptr;
  g_object_unref(tex);
  int hi = std::max({p.r, p.g, p.b}), lo = std::min({p.r, p.g, p.b});
  return hi - lo > 25;
}

Step focus_step(std::string name, std::function<void()> start, const char *id,
                const char *label = nullptr) {
  return Step{std::move(name), std::move(start), [id, label] {
                GtkWidget *want = std::string(id) == "t1" ? editor_of("t1") : by_id(id);
                bool ok = want && focus_widget() == want;
                if (label) ok = ok && has_text(app.root, std::string("focused: ") + label);
                return ok;
              }};
}

void add_keyboard_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(focus_step("autoFocus focuses a View (onFocus)", [] {}, "first", "first"));
  app.steps.push_back(focus_step("Tab: the next focusable view, a Pressable (onBlur, onFocus)",
                                 [] { tab(); }, "p1", "p1"));
  app.steps.push_back(focus_step("Tab: into the TextInput", [] { tab(); }, "t1", "t1"));
  app.steps.push_back(focus_step("Tab: a focusable View", [] { tab(); }, "v1", "v1"));
  app.steps.push_back(focus_step("Tab: the Switch", [] { tab(); }, "sw"));
  app.steps.push_back(focus_step("Tab: the Button", [] { tab(); }, "b1"));
  app.steps.push_back(focus_step(
      "Tab skips focusable={false} and plain views", [] { tab(); }, "vh", "vh"));
  // (vh handles Tab itself, Shift+Tab included: back from the Button.)
  app.steps.push_back(focus_step("Shift+Tab goes back", [] {
    focus_by_keyboard("b1");
    tab(true);
  }, "sw"));
  app.steps.push_back(focus_step("  ...and back again", [] { tab(true); }, "v1", "v1"));
  app.steps.push_back(focus_step("Shift+Tab into a TextInput", [] { tab(true); }, "t1", "t1"));
  app.steps.push_back(focus_step("  ...and out of it", [] { tab(true); }, "p1", "p1"));
  app.steps.push_back(focus_step("  ...and Tab into it again", [] { tab(); }, "t1", "t1"));
  app.steps.push_back(Step{"a keyboard-focused view draws the focus ring",
                           [] { focus_by_keyboard("v1"); },
                           [] { return focus_widget() == by_id("v1") && has_ring("v1"); }});
  app.steps.push_back(Step{"  ...not with enableFocusRing={false}",
                           [] { focus_by_keyboard("noring"); },
                           [] {
                             return focus_widget() == by_id("noring") &&
                                    has_text(app.root, "focused: noring") &&
                                    !has_ring("noring") && !has_ring("v1");
                           }});
  app.steps.push_back(Step{
      "keys reach the focused view: onKeyDown / onKeyUp with key and code",
      [] {
        focus_by_keyboard("v1");
        key(GDK_KEY_a, kA);
      },
      [] { return has_text(app.root, "keydown a KeyA · keyup a KeyA"); }});
  app.steps.push_back(Step{"  ...with modifiers",
                           [] { key(GDK_KEY_A, kA, GDK_SHIFT_MASK); },
                           [] { return has_text(app.root, "keydown A KeyA shift"); }});
  app.steps.push_back(Step{"Enter presses the focused Pressable",
                           [] {
                             focus_by_keyboard("p1");
                             key(GDK_KEY_Return, kEnter);
                           },
                           [] { return has_text(app.root, "presses 1 "); }});
  app.steps.push_back(Step{
      "Space presses it on release",
      [] {
        auto *keys = app.host->keyboardHandler();
        if (!keys->keyPressed(GDK_KEY_space, kSpace, GdkModifierType(0))) {
          check(false, "  ...Space handled");
        }
        keys->keyReleased(GDK_KEY_space, kSpace, GdkModifierType(0));
      },
      [] { return has_text(app.root, "presses 2 "); }});
  app.steps.push_back(Step{"Enter presses the focused Button",
                           [] {
                             focus_by_keyboard("b1");
                             key(GDK_KEY_Return, kEnter);
                           },
                           [] { return has_text(app.root, "button 1"); }});
  app.steps.push_back(Step{
      "keyDownEvents: a view that handles Tab keeps the focus",
      [] {
        focus_by_keyboard("vh");
        if (!tab()) check(false, "  ...Tab handled");
      },
      [] { return focus_widget() == by_id("vh") && has_text(app.root, "handled Tab Tab"); }});
  app.steps.push_back(Step{
      "  ...Ctrl+A matches {code: 'KeyA', ctrlKey: true}; A alone doesn't",
      [] {
        check(key(GDK_KEY_a, kA, GDK_CONTROL_MASK), "  ...Ctrl+A handled");
        check(!key(GDK_KEY_a, kA), "  ...A not handled");
      },
      [] { return has_text(app.root, "handled a KeyA"); }});
  app.steps.push_back(Step{
      "a TextInput gets onKeyDown and still types its keys",
      [] {
        gtk_widget_grab_focus(editor_of("t1"));
        check(!key(GDK_KEY_x, kX), "  ...x left to the TextInput");
      },
      [] { return has_text(app.root, "input x KeyX"); }});
  app.steps.push_back(focus_step("ref.focus() on a View", [] { click("focus-v1"); }, "v1", "v1"));
  app.steps.push_back(Step{"ref.blur() on a View (from its onKeyDown)",
                           [] { key(GDK_KEY_b, kB); },
                           [] {
                             return focus_widget() != by_id("v1") &&
                                    has_text(app.root, "focused: none");
                           }});
  app.steps.push_back(Step{"a click focuses a Pressable (and presses it)",
                           [] { click("p1"); },
                           [] {
                             return focus_widget() == by_id("p1") &&
                                    has_text(app.root, "focused: p1") &&
                                    has_text(app.root, "presses 3 ");
                           }});
}

// ---------------------------------------------------------------------------
// GalleryMouse checks

bool is_mouse() { return opts.module == "GalleryMouse"; }

void move_to(const char *id, float fx = 0.5f, float fy = 0.5f) {
  GtkWidget *v = by_id(id);
  if (!v) return;
  graphene_rect_t b = bounds_in_root(v);
  send(rngtk::GtkPointerHandler::Phase::Move,
       graphene_point_t{b.origin.x + b.size.width * fx, b.origin.y + b.size.height * fy});
}

void click_button(const char *id, int button) {
  GtkWidget *v = by_id(id);
  if (!v) return;
  graphene_point_t c = center_of(v);
  rngtk::GtkPointerHandler::Input input{};
  input.x = c.x;
  input.y = c.y;
  input.button = button;
  input.phase = rngtk::GtkPointerHandler::Phase::Down;
  app.host->pointerHandler()->dispatch(input);
  input.phase = rngtk::GtkPointerHandler::Phase::Up;
  app.host->pointerHandler()->dispatch(input);
  // Close the selectable-text menu or anything a right click opened.
  for (GtkWidget *c = gtk_widget_get_first_child(app.root); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (GTK_IS_POPOVER(c)) gtk_popover_popdown(GTK_POPOVER(c));
  }
}

void add_mouse_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{"onMouseEnter, with the point in the view",
                           [] { move_to("outer", 0.05f, 0.5f); },
                           [] { return has_text(app.root, "outer in at 15 · inner out"); }});
  app.steps.push_back(Step{
      "entering a child: the child's onMouseEnter; the parent stays entered",
      [] { move_to("inner"); },
      [] { return has_text(app.root, "outer in at 15 · inner in · enters 1"); }});
  app.steps.push_back(Step{"leaving both: onMouseLeave on each",
                           [] { move_to("away"); },
                           [] { return has_text(app.root, "outer out · inner out · enters 1"); }});
  app.steps.push_back(Step{
      "tooltip: a GTK tooltip that GTK's own picking finds under the pointer",
      [] {},
      [] {
        GtkWidget *tip = by_id("tip");
        GtkWidget *text = by_id("tip-text");
        if (!tip || !text) return false;
        // What GTK's tooltip code does: the widget under the pointer, then
        // up to the first one with a tooltip.
        graphene_point_t c = center_of(text), in_window;
        if (!gtk_widget_compute_point(app.root, app.window, &c, &in_window)) return false;
        GtkWidget *w = gtk_widget_pick(app.window, in_window.x, in_window.y, GTK_PICK_DEFAULT);
        while (w && !gtk_widget_get_has_tooltip(w)) w = gtk_widget_get_parent(w);
        const char *t = w ? gtk_widget_get_tooltip_text(w) : nullptr;
        return w == tip && t && std::string(t) == "Hello from a tooltip";
      }});
  app.steps.push_back(Step{"a middle click: onAuxClick (button 1), bubbling to the parent",
                           [] { click_button("aux", 2); },
                           [] { return has_text(app.root, "aux button 1 · parent aux 1 · presses 0"); }});
  app.steps.push_back(Step{"a right click: onAuxClick (button 2)",
                           [] { click_button("aux", 3); },
                           [] { return has_text(app.root, "aux button 2 · parent aux 2 · presses 0"); }});
  app.steps.push_back(Step{"a left click presses, with no auxclick",
                           [] { click_button("aux", 1); },
                           [] { return has_text(app.root, "aux button 2 · parent aux 2 · presses 1"); }});
  app.steps.push_back(Step{"an auxclick reaches a listener Fabric flattened",
                           [] { click_button("aux-text", 2); },
                           [] { return has_text(app.root, "parent aux 103"); }});
  app.steps.push_back(Step{"a View with only onMouseEnter still hears it",
                           [] { move_to("bare"); },
                           [] { return has_text(app.root, "bare onMouseEnter: in"); }});
  app.steps.push_back(Step{"  ...and a View with only a tooltip has it", [] {},
                           [] {
                             GtkWidget *w = by_id("bare-tip");
                             while (w && !gtk_widget_get_has_tooltip(w)) w = gtk_widget_get_parent(w);
                             return w && std::string(gtk_widget_get_tooltip_text(w)) == "A bare tooltip";
                           }});
}

// ---------------------------------------------------------------------------
// GalleryAccessibility checks

bool is_accessibility() { return opts.module == "GalleryAccessibility"; }

// The app's tree as a screen reader sees it: scripts/a11y-probe.py, run
// out of process (AT-SPI calls back into this main loop).
struct Probe {
  bool running = false, done = false;
  folly::dynamic result;
  std::string error;
} probe;

void run_probe(std::vector<std::string> extra = {}) {
  probe = Probe{};
  probe.running = true;
  std::string script = std::string(RNGTK_SOURCE_DIR) + "/scripts/a11y-probe.py";
  std::string pid = std::to_string(getpid());
  std::vector<const char *> argv = {"python3", script.c_str(), "--pid", pid.c_str()};
  for (const auto &a : extra) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  GError *error = nullptr;
  GSubprocess *proc = g_subprocess_newv(
      argv.data(), GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE),
      &error);
  if (!proc) {
    probe.error = error->message;
    probe.done = true;
    g_clear_error(&error);
    return;
  }
  g_subprocess_communicate_utf8_async(
      proc, nullptr, nullptr,
      [](GObject *source, GAsyncResult *res, gpointer) {
        char *out = nullptr;
        GError *err = nullptr;
        if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), res, &out,
                                                  nullptr, &err)) {
          probe.error = err->message;
          g_clear_error(&err);
        } else {
          try {
            probe.result = folly::parseJson(out ? out : "");
            if (probe.result.count("error")) {
              probe.error = probe.result["error"].asString();
            }
          } catch (const std::exception &e) {
            probe.error = std::string("bad probe output: ") + e.what();
          }
        }
        g_free(out);
        probe.done = true;
        g_object_unref(source);
      },
      nullptr);
}

const folly::dynamic *probe_node(const std::string &name, const char *role = nullptr) {
  if (!probe.result.count("nodes")) return nullptr;
  for (const auto &n : probe.result["nodes"]) {
    if (n.count("name") && n["name"].asString() == name &&
        (!role || n["role"].asString() == role)) {
      return &n;
    }
  }
  return nullptr;
}

bool has(const folly::dynamic &list, const char *item) {
  for (const auto &v : list) {
    if (v.isString() && v.asString() == item) return true;
  }
  return false;
}

bool a11y_bus = true;  // false: no AT-SPI here, skip the tree checks

// Waits for the probe (reporting its error once); on no AT-SPI bus, says
// so and skips.
bool probe_finished(const char *what) {
  if (!probe.done) return false;
  if (!probe.error.empty() && probe.running) {
    probe.running = false;
    if (probe.error.find("AT-SPI") != std::string::npos) {
      printf("SKIP %s: %s\n", what, probe.error.c_str());
      a11y_bus = false;
    } else {
      check(false, (std::string(what) + ": " + probe.error).c_str());
    }
  }
  return true;
}

// The AT-SPI bus's ScreenReaderEnabled, which GNOME sets while Orca runs.
void set_bus_screen_reader(bool enabled) {
  g_dbus_connection_call(
      g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), "org.a11y.Bus",
      "/org/a11y/bus", "org.freedesktop.DBus.Properties", "Set",
      g_variant_new("(ssv)", "org.a11y.Status", "ScreenReaderEnabled",
                    g_variant_new_boolean(enabled)),
      nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr, nullptr);
}
bool restore_screen_reader = false;

void add_accessibility_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  if (opts.system_accessibility) {
    app.steps.push_back(Step{"the AT-SPI bus's ScreenReaderEnabled reaches isScreenReaderEnabled",
                             [] {
                               restore_screen_reader = true;
                               set_bus_screen_reader(true);
                             },
                             [] { return has_text(app.root, "screen reader: on"); }});
    app.steps.push_back(Step{"  ...and off again (screenReaderChanged)",
                             [] {
                               set_bus_screen_reader(false);
                               restore_screen_reader = false;
                             },
                             [] { return has_text(app.root, "screen reader: off · reduce motion"); }});
    return;
  }
  app.steps.push_back(Step{
      "AT-SPI: roles, names, descriptions, states, values, actions",
      [] { run_probe(); },
      [] {
        if (!probe_finished("the AT-SPI tree")) return false;
        if (!a11y_bus || !probe.error.empty()) return true;
        auto expect = [](bool ok, const char *what) {
          check(ok, (std::string("  ...") + what).c_str());
        };
        const folly::dynamic *n;
        expect(probe_node("Settings", "heading"), "accessibilityRole header: a heading named by its text");
        n = probe_node("Save", "push button");
        expect(n && (*n)["description"] == "Saves the file" &&
                   has((*n)["actions"], "a11y.activate"),
               "Button: a push button, accessibilityHint as its description, an activate action");
        n = probe_node("Wi-Fi", "check box");
        expect(n && has((*n)["states"], "checked"), "role checkbox + aria-checked: a checked check box");
        n = probe_node("Bold", "toggle button");
        expect(n && has((*n)["states"], "pressed"), "togglebutton + checked: a pressed toggle button");
        n = probe_node("Disabled action", "push button");
        expect(n && !has((*n)["states"], "sensitive"), "aria-disabled: not sensitive");
        expect(probe_node("Battery 80%"), "an accessible View is named by its text");
        n = probe_node("Volume", "slider");
        expect(n && (*n)["value"].isObject() && (*n)["value"]["now"] == 3.0 &&
                   (*n)["value"]["max"] == 10.0 && has((*n)["actions"], "a11y.increment"),
               "adjustable: a slider with accessibilityValue and the accessibilityActions");
        expect(!probe_node("Hidden from screen readers"), "accessibilityElementsHidden hides the subtree");
        expect(probe_node("A plain paragraph", "label"), "Text: a label");
        expect(probe_node("Name field", "text"), "TextInput accessibilityLabel: a named text box");
        expect(probe_node("Email address", "text"), "accessibilityLabelledBy names the text box");
        expect(probe_node("Dark mode"), "Switch accessibilityLabel");
        return true;
      }});
  app.steps.push_back(Step{
      "AT-SPI actions: increment sends onAccessibilityAction, activate presses",
      [] {
        if (a11y_bus) run_probe({"--do", "Volume=a11y.increment", "--do", "Save=a11y.activate"});
      },
      [] {
        if (!a11y_bus) return true;
        if (!probe_finished("AT-SPI actions")) return false;
        if (!probe.error.empty()) return true;
        const folly::dynamic *n = probe_node("Volume", "slider");
        return has_text(app.root, "volume 4 · saves 1") && n &&
               (*n)["value"]["now"] == 4.0;
      }});
  app.steps.push_back(Step{
      "AT-SPI setting the value: an increment",
      [] {
        if (a11y_bus) run_probe({"--set", "Volume=9"});
      },
      [] {
        if (!a11y_bus) return true;
        if (!probe_finished("AT-SPI set value")) return false;
        return !probe.error.empty() || has_text(app.root, "volume 5");
      }});
  app.steps.push_back(Step{"Tab from one TextInput to the next, past a Text",
                           [] {
                             gtk_widget_grab_focus(editor_of("name-field"));
                             tab();
                           },
                           [] { return focus_widget() == editor_of("email-field"); }});
  app.steps.push_back(Step{"  ...and Shift+Tab back", [] { tab(true); },
                           [] { return focus_widget() == editor_of("name-field"); }});
  app.steps.push_back(Step{"AccessibilityInfo: no screen reader, reduce motion from GTK", [] {},
                           [] {
                             gboolean animations = TRUE;
                             g_object_get(gtk_settings_get_default(), "gtk-enable-animations",
                                          &animations, nullptr);
                             return has_text(app.root, std::string("screen reader: off · reduce motion: ") +
                                                           (animations ? "off" : "on"));
                           }});
  app.steps.push_back(Step{"no screen reader: accessible Views and Texts stay out of the Tab order",
                           [] {},
                           [] {
                             return !gtk_widget_get_focusable(by_id("battery")) &&
                                    !gtk_widget_get_focusable(by_id("plain-text"));
                           }});
  app.steps.push_back(Step{"screenReaderChanged when a screen reader starts",
                           [] { app.host->accessibilityStatus().setScreenReaderEnabled(true); },
                           [] { return has_text(app.root, "screen reader: on"); }});
  app.steps.push_back(Step{
      "  ...then accessible elements take focus, so Tab reaches them for Orca",
      [] {},
      [] {
        return gtk_widget_get_focusable(by_id("battery")) &&
               !gtk_widget_get_focusable(by_id("battery-text")) &&  // inside it
               gtk_widget_get_focusable(by_id("plain-text"));
      }});
  app.steps.push_back(Step{"  ...Tab from the Bold toggle goes on to the accessible group",
                           [] {
                             // Disabled action (a button), then Battery.
                             gtk_widget_grab_focus(by_id("disabled-action"));
                             tab();
                           },
                           [] { return focus_widget() == by_id("battery"); }});
  app.steps.push_back(Step{"  ...which AT-SPI shows as focused",
                           [] { if (a11y_bus) run_probe(); },
                           [] {
                             if (!a11y_bus) return true;
                             if (!probe_finished("the focused group")) return false;
                             const folly::dynamic *n = probe_node("Battery 80%");
                             return n && has((*n)["states"], "focused");
                           }});
  app.steps.push_back(Step{"  ...and leave the Tab order when it stops",
                           [] { app.host->accessibilityStatus().setScreenReaderEnabled(false); },
                           [] {
                             return has_text(app.root, "screen reader: off") &&
                                    !gtk_widget_get_focusable(by_id("plain-text")) &&
                                    !gtk_widget_get_focusable(by_id("battery"));
                           }});
  static gboolean animations_before = TRUE;
  app.steps.push_back(Step{
      "reduceMotionChanged follows gtk-enable-animations",
      [] {
        g_object_get(gtk_settings_get_default(), "gtk-enable-animations",
                     &animations_before, nullptr);
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations",
                     !animations_before, nullptr);
      },
      [] {
        return has_text(app.root, std::string("reduce motion: ") +
                                      (animations_before ? "on" : "off"));
      }});
  app.steps.push_back(Step{
      "  ...and back",
      [] {
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations",
                     animations_before, nullptr);
      },
      [] { return has_text(app.root, "changes 4"); }});
  app.steps.push_back(Step{"announceForAccessibility", [] { click("announce"); },
                           [] {
                             return app.host->mountingManager().lastAnnouncement() ==
                                    "Hello from React Native";
                           }});
  app.steps.push_back(Step{"a live region announces its new text", [] { click("count"); },
                           [] {
                             return app.host->mountingManager().lastAnnouncement() == "count 1";
                           }});
  app.steps.push_back(Step{"setAccessibilityFocus focuses the view for the screen reader",
                           [] { click("focus-note"); },
                           [] { return focus_widget() == by_id("note"); }});
  app.steps.push_back(Step{"  ...which stops being focusable once it loses focus",
                           [] { gtk_widget_grab_focus(by_id("count")); },
                           [] { return !gtk_widget_get_focusable(by_id("note")); }});
}

// ---------------------------------------------------------------------------
// GalleryPlatform checks

bool is_platform() { return opts.module == "GalleryPlatform"; }

// The self-test's own XDG data and config directories: a handler for
// rngtk-test: URLs (a script that writes the URL to opened.txt), and
// I18nManager's saved settings. Set up before GIO reads them.
std::string xdg_dir;

void write_file(const std::string &path, const std::string &content, int mode = 0644) {
  gchar *dir = g_path_get_dirname(path.c_str());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  g_file_set_contents(path.c_str(), content.c_str(), -1, nullptr);
  g_chmod(path.c_str(), mode);
}

void set_up_platform_xdg() {
  gchar *dir = g_dir_make_tmp("rngtk-platform-XXXXXX", nullptr);
  if (!dir) return;
  xdg_dir = dir;
  g_free(dir);
  std::string data = xdg_dir + "/data", config = xdg_dir + "/config";
  write_file(xdg_dir + "/handler.sh",
             "#!/bin/sh\nprintf '%s' \"$1\" > \"" + xdg_dir + "/opened.txt\"\n", 0755);
  write_file(data + "/applications/rngtk-test-handler.desktop",
             "[Desktop Entry]\nType=Application\nName=rngtk test URL handler\n"
             "Exec=" + xdg_dir + "/handler.sh %u\n"
             "MimeType=x-scheme-handler/rngtk-test;\nNoDisplay=true\n");
  write_file(config + "/mimeapps.list",
             "[Default Applications]\nx-scheme-handler/rngtk-test=rngtk-test-handler.desktop\n");
  if (opts.rtl) {
    write_file(config + "/react-native-gtk4/dev.curiosity26.RNGtk4/i18n.ini",
               "[I18n]\nforceRTL=true\n");
  }
  g_setenv("XDG_DATA_HOME", data.c_str(), TRUE);
  g_setenv("XDG_CONFIG_HOME", config.c_str(), TRUE);
}

void remove_tree(const std::string &path) {
  if (GDir *dir = g_dir_open(path.c_str(), 0, nullptr)) {
    while (const char *name = g_dir_read_name(dir)) remove_tree(path + "/" + name);
    g_dir_close(dir);
  }
  g_remove(path.c_str());
}

std::string read_text(const std::string &path) {
  gchar *content = nullptr;
  if (!g_file_get_contents(path.c_str(), &content, nullptr, nullptr)) return "";
  std::string s = content;
  g_free(content);
  return s;
}

float text_height(const char *id) {
  GtkWidget *v = by_id(id);
  return v ? rn_widget_get_frame(v).size.height : 0;
}

void add_platform_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  if (opts.rtl) {
    app.steps.push_back(Step{"I18nManager.forceRTL saved: isRTL, and rows lay out right to left",
                             [] {},
                             [] {
                               graphene_rect_t a = bounds_in_root(by_id("first"));
                               graphene_rect_t b = bounds_in_root(by_id("second"));
                               return has_text(app.root, "isRTL true") &&
                                      a.origin.x > b.origin.x &&
                                      gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL;
                             }});
    return;
  }
  app.steps.push_back(Step{"Linking.getInitialURL: the URL the app started with", [] {},
                           [] { return has_text(app.root, "initial: " + opts.url); }});
  app.steps.push_back(Step{"canOpenURL: a scheme with a handler, and one without",
                           [] { click("can-open"); },
                           [] {
                             return has_text(app.root, "can rngtk-test: yes · can nosuch: no");
                           }});
  app.steps.push_back(Step{"openURL launches the desktop's handler for the scheme",
                           [] { click("open"); },
                           [] {
                             return has_text(app.root, "opened: ok") &&
                                    read_text(xdg_dir + "/opened.txt") ==
                                        "rngtk-test:opened?x=1";
                           }});
  app.steps.push_back(Step{"openURL rejects a URL nothing handles", [] { click("open-bad"); },
                           [] { return has_text(app.root, "open bad: rejected"); }});
  app.steps.push_back(Step{"a URL passed to the running app: a Linking 'url' event",
                           [] { app.host->openURL("rngtk-test:later"); },
                           [] { return has_text(app.root, "last url: rngtk-test:later"); }});
  // (The real window may or may not be active while the test runs: it
  // starts from active, as the state the window reports.)
  static std::string focus_before;
  app.steps.push_back(Step{"AppState: active while the window is",
                           [] { app.host->setAppStateForTesting(0); },
                           [] { return has_text(app.root, "app state active · focus"); }});
  app.steps.push_back(Step{"AppState: inactive when another window is active (blur)",
                           [] { app.host->setAppStateForTesting(1); },
                           [] { return has_text(app.root, "app state inactive · focus"); }});
  app.steps.push_back(Step{"AppState: background when minimized (no focus event)",
                           [] { app.host->setAppStateForTesting(2); },
                           [] { return has_text(app.root, "app state background"); }});
  app.steps.push_back(Step{"AppState: active again (focus)",
                           [] { app.host->setAppStateForTesting(0); },
                           [] { return has_text(app.root, "app state active · focus"); }});
  app.steps.push_back(Step{"Clipboard.setString / getString", [] { click("clipboard"); },
                           [] { return has_text(app.root, "clipboard: clipboard from JS"); }});
  app.steps.push_back(Step{"Vibration: accepted, no-op", [] { click("vibrate"); },
                           [] { return has_text(app.root, "vibrated yes"); }});
  app.steps.push_back(Step{"Share.share: a stub that resolves dismissed", [] { click("share"); },
                           [] { return has_text(app.root, "share: dismissedAction"); }});
  app.steps.push_back(Step{"I18nManager: left to right by default", [] {},
                           [] {
                             graphene_rect_t a = bounds_in_root(by_id("first"));
                             graphene_rect_t b = bounds_in_root(by_id("second"));
                             return has_text(app.root, "isRTL false") && a.origin.x < b.origin.x;
                           }});
  app.steps.push_back(Step{
      "I18nManager.forceRTL(true) is saved for the next start",
      [] { click("force-rtl"); },
      [] {
        return read_text(xdg_dir + "/config/react-native-gtk4/dev.curiosity26.RNGtk4/i18n.ini")
                   .find("forceRTL=true") != std::string::npos;
      }});
  static int dpi_before = 0;
  static float scaled_before = 0, unscaled_before = 0;
  app.steps.push_back(Step{
      "GNOME text scaling (gtk-xft-dpi x1.5): fontScale, and Text grows (allowFontScaling)",
      [] {
        g_object_get(gtk_settings_get_default(), "gtk-xft-dpi", &dpi_before, nullptr);
        scaled_before = text_height("scaled");
        unscaled_before = text_height("unscaled");
        g_object_set(gtk_settings_get_default(), "gtk-xft-dpi", int(96 * 1024 * 1.5), nullptr);
      },
      [] {
        float scaled = text_height("scaled"), unscaled = text_height("unscaled");
        if (!has_text(app.root, "fontScale 1.50") || !has_text(app.root, "getFontScale 1.50")) {
          return false;
        }
        printf("  Text height %.0f -> %.0f, allowFontScaling={false} %.0f -> %.0f\n",
               scaled_before, scaled, unscaled_before, unscaled);
        return scaled > scaled_before * 1.3f && std::abs(unscaled - unscaled_before) < 1;
      }});
  app.steps.push_back(Step{"  ...and back",
                           [] {
                             g_object_set(gtk_settings_get_default(), "gtk-xft-dpi",
                                          dpi_before > 0 ? dpi_before : 96 * 1024, nullptr);
                           },
                           [] {
                             return std::abs(text_height("scaled") - scaled_before) < 1;
                           }});
}

// ---------------------------------------------------------------------------
// GalleryModal checks

bool is_modal() { return opts.module == "GalleryModal"; }

// The modal (ModalHostView tag) whose window shows `widget`, or 0.
facebook::react::Tag modal_of(GtkWidget *widget) {
  auto &mm = app.host->mountingManager();
  for (auto tag : mm.modalTags()) {
    GtkWidget *content = mm.viewForTag(tag);
    if (widget == content || gtk_widget_is_ancestor(widget, content)) return tag;
  }
  return 0;
}

// A click on view `id`, in the app or in a modal's window.
void click_anywhere(const char *id) {
  GtkWidget *v = by_id(id);
  if (!v) return;
  auto &mm = app.host->mountingManager();
  auto tag = modal_of(v);
  GtkWidget *root = tag ? mm.viewForTag(tag) : app.root;
  rngtk::GtkPointerHandler *handler =
      tag ? mm.modalPointerHandler(tag) : app.host->pointerHandler();
  graphene_rect_t b{};
  if (!gtk_widget_compute_bounds(v, root, &b)) return;
  rngtk::GtkPointerHandler::Input input{};
  input.x = b.origin.x + b.size.width / 2;
  input.y = b.origin.y + b.size.height / 2;
  input.timeMs = uint32_t(g_get_monotonic_time() / 1000);
  input.phase = rngtk::GtkPointerHandler::Phase::Down;
  handler->dispatch(input);
  input.phase = rngtk::GtkPointerHandler::Phase::Up;
  handler->dispatch(input);
}

GtkWindow *modal_window(size_t i) {
  auto tags = app.host->mountingManager().modalTags();
  return i < tags.size() ? app.host->mountingManager().modalWindow(tags[i]) : nullptr;
}

// Escape, as the modal window's key controller gets it.
void press_escape(GtkWindow *window) {
  GListModel *controllers = gtk_widget_observe_controllers(GTK_WIDGET(window));
  for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
    auto *c = static_cast<GObject *>(g_list_model_get_item(controllers, i));
    if (GTK_IS_EVENT_CONTROLLER_KEY(c) && g_object_get_data(c, "rngtk-modal-tag")) {
      gboolean handled = FALSE;
      g_signal_emit_by_name(c, "key-pressed", GDK_KEY_Escape, 9u, GdkModifierType(0),
                            &handled);
    }
    g_object_unref(c);
  }
  g_object_unref(controllers);
}

void add_modal_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "Modal: a window of its own, transient for and modal over the app's",
      [] { click_anywhere("open-full"); },
      [] {
        GtkWindow *w = modal_window(0);
        return w && gtk_widget_get_visible(GTK_WIDGET(w)) &&
               gtk_window_get_transient_for(w) == GTK_WINDOW(app.window) &&
               gtk_window_get_modal(w) && !gtk_window_get_decorated(w) &&
               has_text(app.root, "shown 1 ");
      }});
  app.steps.push_back(Step{
      "  ...focus moves in: its first focusable view", [] {},
      [] {
        GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(modal_window(0)));
        GtkWidget *first = by_id("open-nested");
        return focus && first && (focus == first || gtk_widget_is_ancestor(focus, first));
      }});
  app.steps.push_back(Step{
      "  ...a dialog to screen readers, named by accessibilityLabel", [] {},
      [] {
        GtkWindow *w = modal_window(0);
        return gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(w)) ==
                   GTK_ACCESSIBLE_ROLE_DIALOG &&
               std::string(gtk_window_get_title(w)) == "Full screen modal";
      }});
  app.steps.push_back(Step{
      "  ...full screen: laid out at the size of the app's content", [] {},
      [] {
        GtkWidget *body = by_id("full-body");
        printf("  modal body %dx%d, app content %dx%d\n", body ? gtk_widget_get_width(body) : 0,
               body ? gtk_widget_get_height(body) : 0, gtk_widget_get_width(app.overlay),
               gtk_widget_get_height(app.overlay));
        return body && gtk_widget_get_width(body) == gtk_widget_get_width(app.overlay) &&
               gtk_widget_get_height(body) == gtk_widget_get_height(app.overlay);
      }});
  app.steps.push_back(Step{
      "  ...AT-SPI (what Orca reads): a modal dialog named by accessibilityLabel",
      [] { run_probe(); },
      [] {
        if (!probe_finished("the modal's AT-SPI node")) return false;
        if (!a11y_bus) return true;
        const folly::dynamic *n = probe_node("Full screen modal", "dialog");
        if (n) printf("  dialog states: %s\n", folly::toJson((*n)["states"]).c_str());
        return check(n && has((*n)["states"], "modal"), "AT-SPI: a modal dialog");
      }});
  app.steps.push_back(Step{
      "a modal opened from a modal stacks over it (formSheet: a 540x620 dialog)",
      [] { click_anywhere("open-nested"); },
      [] {
        GtkWindow *outer = modal_window(0), *inner = modal_window(1);
        GtkWidget *body = by_id("nested-body");
        if (!inner || !body || !has_text(app.root, "shown 2 ")) return false;
        int h = gtk_widget_get_height(body);
        printf("  nested body %dx%d, its window's content %dx%d\n", gtk_widget_get_width(body), h,
               gtk_widget_get_width(gtk_window_get_child(inner)),
               gtk_widget_get_height(gtk_window_get_child(inner)));
        return gtk_window_get_transient_for(inner) == outer && gtk_window_get_decorated(inner) &&
               gtk_widget_get_width(body) == 540 &&
               h == 620;
      }});
  app.steps.push_back(Step{
      "Escape asks to close (onRequestClose); it closes, then onDismiss",
      [] { press_escape(modal_window(1)); },
      [] {
        return has_text(app.root, "dismissed 1 · requests 1 · last dismiss nested") &&
               app.host->mountingManager().modalTags().size() == 1;
      }});
  app.steps.push_back(Step{
      "a button inside closes the first one (after its slide): window gone",
      [] { click_anywhere("close-full"); },
      [] {
        GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(app.window));
        GtkWidget *opener = by_id("open-full");
        return has_text(app.root, "dismissed 2 · requests 1 · last dismiss full") &&
               app.host->mountingManager().modalTags().empty() &&
               check(focus && (focus == opener || gtk_widget_is_ancestor(focus, opener)),
                     "  focus is back on the button that opened it");
      }});
  app.steps.push_back(Step{
      "formSheet with fade: a decorated, resizable dialog, faded in",
      [] { click_anywhere("open-sheet"); },
      [] {
        GtkWindow *w = modal_window(0);
        GtkWidget *body = by_id("sheet-body");
        return w && gtk_window_get_decorated(w) && gtk_window_get_resizable(w) && body &&
               gtk_widget_get_width(body) == 540 &&
               gtk_widget_get_opacity(GTK_WIDGET(w)) > 0.99 && has_text(app.root, "shown 3 ");
      }});
  app.steps.push_back(Step{
      "the window's close button asks to close (onRequestClose), and it closes",
      [] { gtk_window_close(modal_window(0)); },
      [] {
        return has_text(app.root, "dismissed 3 · requests 2 · last dismiss sheet") &&
               app.host->mountingManager().modalTags().empty();
      }});
  app.steps.push_back(Step{
      "transparent: the window paints nothing; the app shows through the backdrop",
      [] { click_anywhere("open-clear"); },
      [] {
        GtkWindow *w = modal_window(0);
        if (!w || !has_text(app.root, "shown 4 ")) return false;
        GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(w));
        if (!gdk_display_is_composited(display)) {
          printf("  no compositor: a dimmed, opaque window\n");
          return bool(gtk_widget_has_css_class(GTK_WIDGET(w), "rngtk-modal-dim"));
        }
        GdkTexture *tex = rngtk::render_widget(GTK_WIDGET(w));
        if (!tex) return false;
        auto p = px(tex, 4, 4);
        pixels.tex = nullptr;
        g_object_unref(tex);
        printf("  backdrop pixel rgba(%d,%d,%d,%d)\n", p.r, p.g, p.b, p.a);
        return gtk_widget_has_css_class(GTK_WIDGET(w), "rngtk-modal-clear") && p.a > 60 &&
               p.a < 160;
      }});
  app.steps.push_back(Step{"  ...and closes", [] { click_anywhere("close-clear"); },
                           [] {
                             return has_text(app.root, "dismissed 4 ") &&
                                    app.host->mountingManager().modalTags().empty();
                           }});
  app.steps.push_back(Step{
      "a modal and one inside it, mounted together: the inner one opens over the outer",
      [] { click_anywhere("open-both"); },
      [] {
        // (The inner one has the lower tag: React creates children first.)
        GtkWindow *a = modal_window(0), *b = modal_window(1);
        if (!a || !b) return false;
        GtkWindow *outer = gtk_window_get_transient_for(a) == GTK_WINDOW(app.window) ? a : b;
        GtkWindow *inner = outer == a ? b : a;
        return gtk_window_get_transient_for(inner) == outer &&
               gtk_widget_get_visible(GTK_WIDGET(inner)) && has_text(app.root, "shown 6 ");
      }});
  app.steps.push_back(Step{"  ...closing the outer one closes both",
                           [] { click_anywhere("close-full"); },
                           [] {
                             return app.host->mountingManager().modalTags().empty() &&
                                    has_text(app.root, "dismissed 6 ");
                           }});
}

// ---------------------------------------------------------------------------
// GalleryDialogs checks

bool is_dialogs() { return opts.module == "GalleryDialogs"; }

std::string dialogs_dir;

// A folder for the file dialogs: a.txt, b.txt, pic.png and sub/.
std::string make_dialogs_folder() {
  char tmpl[] = "/tmp/rngtk-dialogs-XXXXXX";
  if (!g_mkdtemp(tmpl)) return "";
  dialogs_dir = tmpl;
  for (const char *name : {"a.txt", "b.txt", "pic.png"}) {
    g_file_set_contents((dialogs_dir + "/" + name).c_str(), "x", 1, nullptr);
  }
  g_mkdir((dialogs_dir + "/sub").c_str(), 0700);
  return dialogs_dir;
}

// The first visible toplevel `pred` accepts.
GtkWidget *find_toplevel(const std::function<bool(GtkWidget *)> &pred) {
  GListModel *toplevels = gtk_window_get_toplevels();
  GtkWidget *found = nullptr;
  for (guint i = 0; i < g_list_model_get_n_items(toplevels) && !found; i++) {
    auto *w = GTK_WIDGET(g_list_model_get_item(toplevels, i));
    if (gtk_widget_get_visible(w) && pred(w)) found = w;
    g_object_unref(w);
  }
  return found;
}

GtkWidget *alert_dialog() {
  return find_toplevel([](GtkWidget *w) { return GTK_IS_MESSAGE_DIALOG(w); });
}

GtkWidget *file_chooser() {
  return find_toplevel([](GtkWidget *w) { return GTK_IS_FILE_CHOOSER(w); });
}

// The button under `widget` whose label is `label`.
GtkWidget *button_labeled(GtkWidget *widget, const std::string &label) {
  if (GTK_IS_BUTTON(widget)) {
    const char *l = gtk_button_get_label(GTK_BUTTON(widget));
    if (l && label == l) return widget;
  }
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c)) {
    if (GtkWidget *b = button_labeled(c, label)) return b;
  }
  return nullptr;
}

std::string property_string(GObject *object, const char *name) {
  char *value = nullptr;
  g_object_get(object, name, &value, nullptr);
  std::string s = value ? value : "";
  g_free(value);
  return s;
}

GtkWidget *first_of_type(GtkWidget *widget, GType type) {
  int n = 0;
  return find_nth(widget, type, &n);
}

bool result_is(const std::string &text) { return has_text(app.root, text); }

// Answers the open file chooser once it has shown a few frames: `answer`
// picks (or not), then the response.
Step chooser_step(std::string name, const char *button, std::function<void(GtkFileChooser *)> answer,
                  int response, std::string expect) {
  // DIR: the self-test's folder.
  for (size_t at; (at = expect.find("DIR")) != std::string::npos;) {
    expect.replace(at, 3, dialogs_dir);
  }
  auto frames = std::make_shared<int>(0);
  return Step{name, [button, frames] {
                *frames = 0;
                click(button);
              },
              [answer, response, expect, frames] {
                GtkWidget *chooser = file_chooser();
                if (chooser) {
                  if (++*frames == 10) answer(GTK_FILE_CHOOSER(chooser));
                  if (*frames == 40) gtk_dialog_response(GTK_DIALOG(chooser), response);
                  return false;
                }
                return *frames >= 40 && result_is(expect);
              }};
}

void add_dialogs_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "Alert.alert: a modal alert dialog over the app, title and message",
      [] { click("alert-three"); },
      [] {
        GtkWidget *d = alert_dialog();
        if (!d || !gtk_widget_get_mapped(d)) return false;
        return gtk_window_get_transient_for(GTK_WINDOW(d)) == GTK_WINDOW(app.window) &&
               gtk_window_get_modal(GTK_WINDOW(d)) &&
               gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(d)) ==
                   GTK_ACCESSIBLE_ROLE_ALERT_DIALOG &&
               property_string(G_OBJECT(d), "text") == "Delete “notes.txt”?" &&
               property_string(G_OBJECT(d), "secondary-text") == "It will be gone for good.";
      }});
  app.steps.push_back(Step{
      "  ...buttons: Cancel first, then the app's order; Delete red, Keep the default", [] {},
      [] {
        GtkWidget *d = alert_dialog();
        GtkWidget *cancel = button_labeled(d, "Cancel"), *keep = button_labeled(d, "Keep"),
                  *del = button_labeled(d, "Delete");
        if (!cancel || !keep || !del) return false;
        graphene_rect_t c{}, k{}, x{};
        (void)gtk_widget_compute_bounds(cancel, d, &c);
        (void)gtk_widget_compute_bounds(keep, d, &k);
        (void)gtk_widget_compute_bounds(del, d, &x);
        return c.origin.x < k.origin.x && k.origin.x < x.origin.x &&
               gtk_widget_has_css_class(del, "destructive-action") &&
               gtk_widget_has_css_class(keep, "suggested-action") &&
               gtk_window_get_default_widget(GTK_WINDOW(d)) == keep;
      }});
  app.steps.push_back(Step{"  ...pressing Delete calls its onPress and closes it",
                           [] { gtk_widget_activate(button_labeled(alert_dialog(), "Delete")); },
                           [] { return !alert_dialog() && result_is("#1 delete"); }});
  app.steps.push_back(Step{"Escape presses the cancel button",
                           [] { click("alert-three"); },
                           [] {
                             GtkWidget *d = alert_dialog();
                             if (d && gtk_widget_get_mapped(d)) g_signal_emit_by_name(d, "close");
                             return !alert_dialog() && result_is("#2 cancel");
                           }});
  static int frames = 0;
  app.steps.push_back(Step{"without a cancel button or cancelable, Escape leaves it open",
                           [] {
                             frames = 0;
                             click("alert-ok");
                           },
                           [] {
                             GtkWidget *d = alert_dialog();
                             if (!d || !gtk_widget_get_mapped(d)) return false;
                             if (++frames == 3) g_signal_emit_by_name(d, "close");
                             return frames > 15 && result_is("#2 cancel");
                           }});
  app.steps.push_back(Step{"  ...its button closes it",
                           [] { gtk_widget_activate(button_labeled(alert_dialog(), "OK")); },
                           [] { return !alert_dialog() && result_is("#3 ok"); }});
  app.steps.push_back(Step{"cancelable: Escape dismisses it (onDismiss)",
                           [] { click("alert-cancelable"); },
                           [] {
                             GtkWidget *d = alert_dialog();
                             if (d && gtk_widget_get_mapped(d)) g_signal_emit_by_name(d, "close");
                             return !alert_dialog() && result_is("#4 dismissed");
                           }});
  app.steps.push_back(Step{
      "no buttons: one GTK OK (translated, with its mnemonic)",
      [] { click("alert-default"); },
      [] {
        GtkWidget *d = alert_dialog();
        GtkWidget *ok = d ? button_labeled(d, g_dgettext("gtk40", "_OK")) : nullptr;
        if (!ok) return false;
        bool underline = gtk_button_get_use_underline(GTK_BUTTON(ok));
        gtk_widget_activate(ok);
        return underline;
      }});
  app.steps.push_back(Step{"  ...which closes it", [] {},
                           [] { return !alert_dialog() && result_is("#4 dismissed"); }});
  app.steps.push_back(Step{
      "Alert.prompt: a text field with defaultValue; Enter presses OK with the text",
      [] { click("prompt"); },
      [] {
        GtkWidget *d = alert_dialog();
        if (!d || !gtk_widget_get_mapped(d)) return false;
        GtkWidget *entry = first_of_type(d, GTK_TYPE_ENTRY);
        if (!entry || std::string(gtk_editable_get_text(GTK_EDITABLE(entry))) != "Ada") return false;
        gtk_editable_set_text(GTK_EDITABLE(entry), "Grace");
        // Enter: GtkText's key binding emits its activate signal.
        g_signal_emit_by_name(first_of_type(entry, GTK_TYPE_TEXT), "activate");
        return true;
      }});
  app.steps.push_back(Step{"  ...onPress got it", [] {},
                           [] { return !alert_dialog() && result_is("#5 name: Grace"); }});
  app.steps.push_back(Step{
      "secure-text: a password field",
      [] { click("prompt-secure"); },
      [] {
        GtkWidget *d = alert_dialog();
        if (!d || !gtk_widget_get_mapped(d)) return false;
        GtkWidget *entry = first_of_type(d, GTK_TYPE_PASSWORD_ENTRY);
        if (!entry) return false;
        gtk_editable_set_text(GTK_EDITABLE(entry), "hunter2");
        gtk_widget_activate(button_labeled(d, "Unlock"));
        return true;
      }});
  app.steps.push_back(Step{"  ...onPress got it", [] {},
                           [] { return !alert_dialog() && result_is("#6 password: hunter2"); }});
  app.steps.push_back(Step{
      "login-password: login and password fields; onPress gets {login, password}",
      [] { click("prompt-login"); },
      [] {
        GtkWidget *d = alert_dialog();
        if (!d || !gtk_widget_get_mapped(d)) return false;
        GtkWidget *login = first_of_type(d, GTK_TYPE_ENTRY);
        GtkWidget *password = first_of_type(d, GTK_TYPE_PASSWORD_ENTRY);
        if (!login || !password ||
            std::string(gtk_editable_get_text(GTK_EDITABLE(login))) != "ada") {
          return false;
        }
        gtk_editable_set_text(GTK_EDITABLE(password), "s3cret");
        gtk_widget_activate(button_labeled(d, "Sign in"));
        return true;
      }});
  app.steps.push_back(Step{"  ...onPress got both", [] {},
                           [] { return !alert_dialog() && result_is("#7 login: ada / s3cret"); }});
  app.steps.push_back(chooser_step(
      "Dialogs.openFile: GTK's file chooser, modal; the picked file's path comes back",
      "open-file",
      [](GtkFileChooser *chooser) {
        printf("  chooser over the app: %s, filters %u, title \"%s\"\n",
               gtk_window_get_transient_for(GTK_WINDOW(chooser)) == GTK_WINDOW(app.window)
                   ? "yes" : "no",
               g_list_model_get_n_items(gtk_file_chooser_get_filters(chooser)),
               gtk_window_get_title(GTK_WINDOW(chooser)));
        GFile *file = g_file_new_for_path((dialogs_dir + "/a.txt").c_str());
        gtk_file_chooser_set_file(chooser, file, nullptr);
        g_object_unref(file);
      },
      GTK_RESPONSE_ACCEPT, "#8 open: [\"DIR/a.txt\"]"));
  app.steps.push_back(chooser_step("  ...cancelled: []", "open-file", [](GtkFileChooser *) {},
                                   GTK_RESPONSE_CANCEL, "#9 open: []"));
  app.steps.push_back(chooser_step(
      "  ...multiple: an array of the files picked", "open-files",
      [](GtkFileChooser *chooser) {
        check(gtk_file_chooser_get_select_multiple(chooser),
              "  the chooser lets you pick several files");
        GFile *file = g_file_new_for_path((dialogs_dir + "/b.txt").c_str());
        gtk_file_chooser_set_file(chooser, file, nullptr);
        g_object_unref(file);
      },
      GTK_RESPONSE_ACCEPT, "#10 open several: [\"DIR/b.txt\"]"));
  app.steps.push_back(chooser_step(
      "Dialogs.saveFile: the name typed, in the folder shown",
      "save-file",
      [](GtkFileChooser *chooser) {
        gtk_file_chooser_set_current_name(chooser, "saved.txt");
      },
      GTK_RESPONSE_ACCEPT, "#11 save: \"DIR/saved.txt\""));
  app.steps.push_back(chooser_step(
      "Dialogs.openFolder: the folder picked",
      "open-folder",
      [](GtkFileChooser *chooser) {
        GFile *file = g_file_new_for_path((dialogs_dir + "/sub").c_str());
        gtk_file_chooser_set_file(chooser, file, nullptr);
        g_object_unref(file);
      },
      GTK_RESPONSE_ACCEPT, "#12 folder: [\"DIR/sub\"]"));
}

// ---------------------------------------------------------------------------
// GalleryMenus checks

bool is_menus() { return opts.module == "GalleryMenus"; }

GtkWidget *context_menu() { return app.host->mountingManager().contextMenuPopover(); }

// A right-click (press and release of button 3) on view `id`.
void right_click(const char *id) {
  GtkWidget *v = by_id(id);
  if (!v) return;
  graphene_point_t c = center_of(v);
  rngtk::GtkPointerHandler::Input input{};
  input.x = c.x;
  input.y = c.y;
  input.button = 3;
  input.timeMs = uint32_t(g_get_monotonic_time() / 1000);
  input.phase = rngtk::GtkPointerHandler::Phase::Down;
  app.host->pointerHandler()->dispatch(input);
  input.phase = rngtk::GtkPointerHandler::Phase::Up;
  app.host->pointerHandler()->dispatch(input);
}

// The labels of a menu model's items, sections flattened, "-" between
// sections, ">" before a submenu's label.
std::string menu_labels(GMenuModel *model) {
  std::string out;
  for (int i = 0; i < g_menu_model_get_n_items(model); i++) {
    if (GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION)) {
      if (!out.empty()) out += " - ";
      out += menu_labels(section);
      g_object_unref(section);
      continue;
    }
    char *label = nullptr;
    g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
    if (!out.empty() && out.back() != ' ') out += ", ";
    if (GMenuModel *sub = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU)) {
      out += std::string(">") + (label ? label : "") + "(" + menu_labels(sub) + ")";
      g_object_unref(sub);
    } else {
      out += label ? label : "";
    }
    g_free(label);
  }
  return out;
}

// Finds the item labelled `label` (sections and submenus too): its action,
// target and accel.
bool menu_item(GMenuModel *model, const std::string &label, std::string *action,
               GVariant **target, std::string *accel) {
  for (int i = 0; i < g_menu_model_get_n_items(model); i++) {
    for (const char *link : {G_MENU_LINK_SECTION, G_MENU_LINK_SUBMENU}) {
      if (GMenuModel *sub = g_menu_model_get_item_link(model, i, link)) {
        bool found = menu_item(sub, label, action, target, accel);
        g_object_unref(sub);
        if (found) return true;
      }
    }
    char *l = nullptr;
    g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &l);
    bool match = l && label == l;
    g_free(l);
    if (!match) continue;
    char *a = nullptr, *k = nullptr;
    g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &a);
    g_menu_model_get_item_attribute(model, i, "accel", "s", &k);
    if (action) *action = a ? a : "";
    if (accel) *accel = k ? k : "";
    if (target) *target = g_menu_model_get_item_attribute_value(model, i, G_MENU_ATTRIBUTE_TARGET, nullptr);
    g_free(a);
    g_free(k);
    return true;
  }
  return false;
}

GMenuModel *popover_model(GtkWidget *popover) {
  return popover ? gtk_popover_menu_get_menu_model(GTK_POPOVER_MENU(popover)) : nullptr;
}

// Picks `label` in the context menu showing, as its menu button does.
bool choose(const std::string &label) {
  GtkWidget *popover = context_menu();
  std::string action;
  GVariant *target = nullptr;
  if (!popover || !menu_item(popover_model(popover), label, &action, &target, nullptr)) {
    return false;
  }
  gtk_popover_popdown(GTK_POPOVER(popover));
  bool ok = gtk_widget_activate_action_variant(popover, action.c_str(), target);
  if (target) g_variant_unref(target);
  return ok;
}

// The state of `label`'s action in the context menu showing (a checkbox's
// boolean, a radio group's checked id), or null.
GVariant *item_state(const std::string &label) {
  GtkWidget *popover = context_menu();
  std::string action;
  if (!popover || !menu_item(popover_model(popover), label, &action, nullptr, nullptr)) {
    return nullptr;
  }
  auto *actions = G_ACTION_GROUP(g_object_get_data(G_OBJECT(popover), "rngtk-menu-actions"));
  std::string name = action.substr(action.find('.') + 1);
  return actions ? g_action_group_get_action_state(actions, name.c_str()) : nullptr;
}

bool item_enabled(const std::string &label) {
  GtkWidget *popover = context_menu();
  std::string action;
  if (!popover || !menu_item(popover_model(popover), label, &action, nullptr, nullptr)) return false;
  auto *actions = G_ACTION_GROUP(g_object_get_data(G_OBJECT(popover), "rngtk-menu-actions"));
  std::string name = action.substr(action.find('.') + 1);
  return actions && g_action_group_get_action_enabled(actions, name.c_str());
}

void close_menu() {
  if (GtkWidget *p = context_menu()) gtk_popover_popdown(GTK_POPOVER(p));
}

GtkApplication *gtk_app() { return gtk_window_get_application(GTK_WINDOW(app.window)); }

void add_menus_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "right-click: the view's contextMenu pops up (GtkPopoverMenu) at the pointer",
      [] { right_click("menu-box"); },
      [] {
        GtkWidget *p = context_menu();
        if (!p || !gtk_widget_get_visible(p)) return false;
        std::string labels = menu_labels(popover_model(p));
        printf("  menu: %s\n", labels.c_str());
        return labels ==
               "Open, Rename… - Show hidden files, >Sort by(Name, Date) - Delete";
      }});
  app.steps.push_back(Step{
      "  ...shortcut labels, disabled items, a checkbox's state", [] {},
      [] {
        std::string accel;
        bool ok = menu_item(popover_model(context_menu()), "Open", nullptr, nullptr, &accel) &&
                  accel == "<Control>o";
        GVariant *hidden = item_state("Show hidden files");
        ok = ok && hidden && g_variant_is_of_type(hidden, G_VARIANT_TYPE_BOOLEAN) &&
             !g_variant_get_boolean(hidden) && !item_enabled("Delete") && item_enabled("Open");
        if (hidden) g_variant_unref(hidden);
        return ok;
      }});
  app.steps.push_back(Step{"choosing an item runs its onSelect (and ContextMenu's)",
                           [] { choose("Open"); },
                           [] { return !context_menu() && has_text(app.root, "#1 via Open ·"); }});
  app.steps.push_back(Step{"a checkbox item: JS flips its state, the next menu shows it",
                           [] {
                             right_click("menu-box");
                             choose("Show hidden files");
                           },
                           [] {
                             if (!has_text(app.root, "hidden true")) return false;
                             if (!context_menu()) right_click("menu-box");
                             GVariant *v = item_state("Show hidden files");
                             bool on = v && g_variant_get_boolean(v);
                             if (v) g_variant_unref(v);
                             return on;
                           }});
  app.steps.push_back(Step{"radio items in a submenu: one group, the checked one shown",
                           [] { choose("Date"); },
                           [] {
                             if (!has_text(app.root, "sort date")) return false;
                             if (!context_menu()) right_click("menu-box");
                             GVariant *v = item_state("Date");
                             bool on = v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) &&
                                       std::string(g_variant_get_string(v, nullptr)) == "4/1";
                             if (v) g_variant_unref(v);
                             close_menu();
                             return on;
                           }});
  app.steps.push_back(Step{"a view without a menu shows its nearest ancestor's",
                           [] { right_click("plain-inner"); },
                           [] {
                             GtkWidget *p = context_menu();
                             bool ok = p && menu_labels(popover_model(p)).rfind("Open,", 0) == 0;
                             if (ok) close_menu();
                             return ok;
                           }});
  app.steps.push_back(Step{"a view with a menu of its own shows its own",
                           [] { right_click("own-inner"); },
                           [] {
                             GtkWidget *p = context_menu();
                             return p && menu_labels(popover_model(p)) == "Inner action" &&
                                    choose("Inner action");
                           }});
  app.steps.push_back(Step{"  ...and its onSelect runs", [] {},
                           [] { return has_text(app.root, "Inner action ·"); }});
  static int frames = 0;
  app.steps.push_back(Step{"a TextInput keeps GTK's own menu (not the box's)",
                           [] {
                             frames = 0;
                             right_click("plain-input");
                           },
                           [] { return ++frames > 5 && !context_menu(); }});
  app.steps.push_back(Step{"  ...unless it has a contextMenu itself",
                           [] { right_click("menu-input"); },
                           [] {
                             GtkWidget *p = context_menu();
                             if (!p || menu_labels(popover_model(p)) != "Insert date") return false;
                             // One item, no empty space under it (GTK's
                             // scrolled window would keep a scrollbar's
                             // length).
                             GtkWidget *item = first_of_type(p, GTK_TYPE_SCROLLED_WINDOW);
                             int h = item ? gtk_widget_get_height(item) : 0;
                             if (h <= 0) return false;
                             printf("  one-item menu: %d px tall\n", h);
                             check(h < 40, "  ...without empty space under its item");
                             return choose("Insert date");
                           }});
  app.steps.push_back(Step{"  ...whose onSelect runs", [] {},
                           [] { return has_text(app.root, "Insert date ·"); }});
  app.steps.push_back(Step{"the Menu key opens the focused view's menu, pointing at it",
                           [] {
                             gtk_widget_grab_focus(by_id("focus-me"));
                             app.host->keyboardHandler()->keyPressed(GDK_KEY_Menu, 135,
                                                                     GdkModifierType(0));
                             app.host->keyboardHandler()->keyReleased(GDK_KEY_Menu, 135,
                                                                      GdkModifierType(0));
                           },
                           [] {
                             GtkWidget *p = context_menu();
                             bool ok = p && menu_labels(popover_model(p)).rfind("Open,", 0) == 0 &&
                                       gtk_popover_get_has_arrow(GTK_POPOVER(p));
                             if (ok) close_menu();
                             return ok;
                           }});
  app.steps.push_back(Step{"  ...and so does Shift+F10",
                           [] {
                             // (Focus comes back from the closed menu
                             // asynchronously on X11.)
                             gtk_widget_grab_focus(by_id("focus-me"));
                             app.host->keyboardHandler()->keyPressed(GDK_KEY_F10, 76,
                                                                     GDK_SHIFT_MASK);
                             app.host->keyboardHandler()->keyReleased(GDK_KEY_F10, 76,
                                                                      GDK_SHIFT_MASK);
                           },
                           [] {
                             GtkWidget *p = context_menu();
                             bool ok = p != nullptr;
                             if (ok) close_menu();
                             return ok;
                           }});
  app.steps.push_back(Step{
      "MenuBar.setMenu: the app's menu bar, shown in its window",
      [] {},
      [] {
        GMenuModel *bar = gtk_application_get_menubar(gtk_app());
        if (!bar) return false;
        std::string labels = menu_labels(bar);
        printf("  menu bar: %s\n", labels.c_str());
        return labels == ">File(New, Open… - Close), >View(Sidebar)" &&
               gtk_application_window_get_show_menubar(GTK_APPLICATION_WINDOW(app.window)) &&
               first_of_type(app.window, GTK_TYPE_POPOVER_MENU_BAR) != nullptr;
      }});
  app.steps.push_back(Step{
      "  ...its shortcuts are the app's accelerators",
      [] {},
      [] {
        gchar **actions = gtk_application_get_actions_for_accel(gtk_app(), "<Control>n");
        bool ok = actions && actions[0];
        g_strfreev(actions);
        actions = gtk_application_get_actions_for_accel(gtk_app(), "<Control>w");
        // Close is disabled, but its accelerator is still registered.
        ok = ok && actions && actions[0];
        g_strfreev(actions);
        return ok;
      }});
  app.steps.push_back(Step{
      "  ...Ctrl+N fires New with the menu closed (the window's shortcut)",
      [] {
        // What a real Ctrl+N press reaches: the window's application
        // shortcuts.
        GListModel *controllers = gtk_widget_observe_controllers(app.window);
        guint keyval = GDK_KEY_n;
        for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
          auto *c = G_OBJECT(g_list_model_get_item(controllers, i));
          if (GTK_IS_SHORTCUT_CONTROLLER(c)) {
            GListModel *shortcuts = G_LIST_MODEL(c);
            for (guint j = 0; j < g_list_model_get_n_items(shortcuts); j++) {
              auto *sc = GTK_SHORTCUT(g_list_model_get_item(shortcuts, j));
              GtkShortcutTrigger *trigger = gtk_shortcut_get_trigger(sc);
              GtkShortcutTrigger *want = gtk_keyval_trigger_new(keyval, GDK_CONTROL_MASK);
              if (gtk_shortcut_trigger_equal(trigger, want)) {
                gtk_shortcut_action_activate(gtk_shortcut_get_action(sc),
                                             GTK_SHORTCUT_ACTION_EXCLUSIVE, app.window,
                                             gtk_shortcut_get_arguments(sc));
                keyval = 0;
              }
              g_object_unref(want);
              g_object_unref(sc);
            }
          }
          g_object_unref(c);
        }
        g_object_unref(controllers);
      },
      [] { return has_text(app.root, "menubar New ·"); }});
  app.steps.push_back(Step{
      "  ...a checkbox in the bar: chosen from the app's actions, its state follows JS",
      [] {
        std::string action;
        menu_item(gtk_application_get_menubar(gtk_app()), "Sidebar", &action, nullptr, nullptr);
        g_action_group_activate_action(G_ACTION_GROUP(gtk_app()), action.substr(4).c_str(),
                                       nullptr);
      },
      [] {
        if (!has_text(app.root, "sidebar false")) return false;
        std::string action;
        if (!menu_item(gtk_application_get_menubar(gtk_app()), "Sidebar", &action, nullptr,
                       nullptr)) {
          return false;
        }
        GVariant *v = g_action_group_get_action_state(G_ACTION_GROUP(gtk_app()),
                                                      action.substr(4).c_str());
        bool off = v && !g_variant_get_boolean(v);
        if (v) g_variant_unref(v);
        return off;
      }});
}

// ---------------------------------------------------------------------------
// GalleryWindows checks

bool is_windows() { return opts.module == "GalleryWindows"; }

// A click on view `id` in window `window` (its surface id).
void click_in_window(SurfaceId window, const char *id) {
  GtkWidget *v = by_id(id);
  if (v && modal_of(v)) {
    click_anywhere(id);  // a Modal's, over the window
    return;
  }
  GtkWidget *root = app.host->rootFor(window);
  rngtk::GtkPointerHandler *handler = app.host->pointerHandlerFor(window);
  graphene_rect_t b{};
  if (!v || !root || !handler || !gtk_widget_compute_bounds(v, root, &b)) return;
  handler->setRealInputEnabled(false);
  rngtk::GtkPointerHandler::Input input{};
  input.x = b.origin.x + b.size.width / 2;
  input.y = b.origin.y + b.size.height / 2;
  input.timeMs = uint32_t(g_get_monotonic_time() / 1000);
  input.phase = rngtk::GtkPointerHandler::Phase::Down;
  handler->dispatch(input);
  input.phase = rngtk::GtkPointerHandler::Phase::Up;
  handler->dispatch(input);
}

size_t window_count() { return app.host->windowList().size(); }

void add_windows_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  constexpr SurfaceId a = 11, b = 21;
  app.steps.push_back(Step{
      "Windows.open: another app window, the component as a surface of its own",
      [] { click("open-a"); },
      [] {
        GtkWindow *w = app.host->windowFor(a);
        GtkWidget *child = app.host->rootFor(a);
        return w && gtk_widget_get_mapped(GTK_WIDGET(w)) && GTK_IS_APPLICATION_WINDOW(w) &&
               std::string(gtk_window_get_title(w)) == "Child A" && child &&
               has_text(child, "A · window 11 ·") && window_count() == 2;
      }});
  app.steps.push_back(Step{
      "  ...useWindowDimensions: each window's own size", [] {},
      [] {
        GtkWidget *child = app.host->rootFor(a);
        return has_text(child, "420 x 300") && has_text(app.root, "main window 1 · 940 x 680");
      }});
  app.steps.push_back(Step{
      "  ...one JS runtime: state changed in the main window shows in the other",
      [] { click("increment"); },
      [] { return has_text(app.host->rootFor(a), "count 1") && has_text(app.root, "count 1"); }});
  app.steps.push_back(Step{
      "setSize: the window resizes, its surface relays out, 'resize' events",
      [] { app.host->setWindowSize(a, 500, 360); },
      [] {
        return has_text(app.host->rootFor(a), "500 x 360") &&
               has_text(app.root, "A resize 500x360");
      }});
  app.steps.push_back(Step{"setTitle, from inside the window (useWindow)",
                           [] { click_in_window(a, "title-A"); },
                           [] {
                             return std::string(gtk_window_get_title(app.host->windowFor(a))) ==
                                    "A (renamed)";
                           }});
  app.steps.push_back(Step{"  ...and the main window's (Windows.main)",
                           [] { click("main-title"); },
                           [] {
                             return std::string(gtk_window_get_title(GTK_WINDOW(app.window))) ==
                                    "Windows gallery";
                           }});
  app.steps.push_back(Step{
      "a Modal in a window opens over that window",
      [] { click_in_window(a, "modal-A"); },
      [] {
        auto tags = app.host->mountingManager().modalTags();
        GtkWindow *m = tags.empty() ? nullptr : app.host->mountingManager().modalWindow(tags[0]);
        return m && gtk_window_get_transient_for(m) == app.host->windowFor(a);
      }});
  app.steps.push_back(Step{"  ...and closes",
                           [] { click_in_window(a, "modal-close-A"); },
                           [] { return app.host->mountingManager().modalTags().empty(); }});
  app.steps.push_back(Step{
      "the close button: 'close-requested', then the window closes ('closed')",
      [] { gtk_window_close(app.host->windowFor(a)); },
      [] {
        return !app.host->windowFor(a) && window_count() == 1 &&
               has_text(app.root, "A close-requested | A closed") && has_text(app.root, "open []");
      }});
  app.steps.push_back(Step{
      "interceptClose: the close button only asks",
      [] { click("open-b"); },
      [] {
        GtkWindow *w = app.host->windowFor(b);
        if (!w || !gtk_widget_get_mapped(GTK_WIDGET(w))) return false;
        static bool asked = false;
        if (!asked) {
          asked = true;
          gtk_window_close(w);
          return false;
        }
        return has_text(app.root, "B close-requested") && app.host->windowFor(b) &&
               window_count() == 2;
      }});
  app.steps.push_back(Step{"  ...requestClose() from inside asks the same way, and it stays",
                           [] { click_in_window(b, "ask-B"); },
                           [] {
                             return has_text(app.root, "B close-requested | B close-requested") &&
                                    app.host->windowFor(b);
                           }});
  app.steps.push_back(Step{"  ...and the app closes it (from inside: useWindow().close())",
                           [] { click_in_window(b, "close-B"); },
                           [] { return !app.host->windowFor(b) && has_text(app.root, "B closed"); }});
  app.steps.push_back(Step{
      "closing the main window while another is open hides it (the app keeps running)",
      [] {
        click("open-a");
      },
      [] {
        static bool closed = false;
        GtkWindow *w = app.host->windowFor(31);
        if (!w || !gtk_widget_get_mapped(GTK_WIDGET(w))) return false;
        if (!closed) {
          closed = true;
          gtk_window_close(GTK_WINDOW(app.window));
          return false;
        }
        return !gtk_widget_get_visible(app.window) &&
               has_text(app.root, "main close-requested | main closed") && window_count() == 2;
      }});
  app.steps.push_back(Step{
      "closing the last window quits the app",
      [] {
        g_signal_connect(g_application_get_default(), "shutdown",
                         G_CALLBACK(+[](GApplication *, gpointer) {
                           check(true, "  ...the app quit");
                         }),
                         nullptr);
        gtk_window_close(app.host->windowFor(31));
      },
      [] { return false; }});
}

// ---------------------------------------------------------------------------
// GalleryTitleBar checks

bool is_titlebar() { return opts.module == "GalleryTitleBar"; }

// Input on window `window`'s surface at (x, y) in its root, with `button`.
void send_in(SurfaceId window, rngtk::GtkPointerHandler::Phase phase, graphene_point_t p,
             int button = 1) {
  rngtk::GtkPointerHandler *handler = app.host->pointerHandlerFor(window);
  if (!handler) return;
  rngtk::GtkPointerHandler::Input input{};
  input.phase = phase;
  input.x = p.x;
  input.y = p.y;
  input.button = button;
  input.timeMs = uint32_t(g_get_monotonic_time() / 1000);
  handler->dispatch(input);
}

// The centre of view `id` in window `window`'s root.
graphene_point_t center_in(SurfaceId window, const char *id) {
  GtkWidget *v = by_id(id), *root = app.host->rootFor(window);
  graphene_rect_t b{};
  if (!v || !root || !gtk_widget_compute_bounds(v, root, &b)) return {-1, -1};
  return {b.origin.x + b.size.width / 2, b.origin.y + b.size.height / 2};
}

// The GtkWindowControls under `widget`.
std::vector<GtkWidget *> window_controls(GtkWidget *widget) {
  std::vector<GtkWidget *> out;
  std::function<void(GtkWidget *)> walk = [&](GtkWidget *w) {
    if (GTK_IS_WINDOW_CONTROLS(w)) out.push_back(w);
    for (GtkWidget *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) {
      walk(c);
    }
  };
  if (widget) walk(widget);
  return out;
}

// What a titlebar click does, as GtkSettings says ("" for none).
std::string titlebar_setting(const char *name) {
  char *value = nullptr;
  g_object_get(gtk_widget_get_settings(app.window), name, &value, nullptr);
  std::string out = value ? value : "";
  g_free(value);
  if (out != "toggle-maximize" && out != "minimize" && out != "lower" && out != "menu") out = "";
  return out;
}

void add_titlebar_steps() {
  using P = rngtk::GtkPointerHandler::Phase;
  constexpr SurfaceId mainId = 1, ownId = 11, clearId = 21;
  auto *ptr = app.host->pointerHandler();
  ptr->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "titleBar 'hidden': no title bar, the frame stays, the content starts at the top",
      [] {},
      [] {
        GtkWidget *bar = gtk_window_get_titlebar(GTK_WINDOW(app.window));
        GtkWidget *tb = by_id("titlebar");
        return bar && !gtk_widget_get_visible(bar) &&
               gtk_window_get_decorated(GTK_WINDOW(app.window)) && tb &&
               bounds_in_root(tb).origin.y == 0 && gtk_widget_get_width(tb) == 940;
      }});
  app.steps.push_back(Step{
      "<TitleBar>: the window's buttons (GtkWindowControls) at each end, as the layout says",
      [] {},
      [] {
        auto controls = window_controls(by_id("titlebar"));
        if (controls.size() != 2) return false;
        bool some = false;
        for (GtkWidget *c : controls) {
          int natural = 0, unused;
          gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &unused, &natural, nullptr,
                             nullptr);
          bool empty = gtk_window_controls_get_empty(GTK_WINDOW_CONTROLS(c));
          // (This window isn't resizable: no maximize button; the frame
          // is measured for one that is.)
          if (!empty && (natural <= 0 || gtk_widget_get_width(c) < natural)) return false;
          some |= !empty;
        }
        char *layout = nullptr;
        g_object_get(gtk_widget_get_settings(app.window), "gtk-decoration-layout", &layout,
                     nullptr);
        printf("  decoration layout \"%s\"\n", layout ? layout : "");
        g_free(layout);
        return some;
      }});
  app.steps.push_back(Step{
      "a drag on the title bar moves the window",
      [ptr] {
        ptr->clearLastWindowAction();
        auto p = center_in(mainId, "title");
        send_in(mainId, P::Down, p);
        send_in(mainId, P::Move, {p.x + 30, p.y + 4});
        send_in(mainId, P::Up, {p.x + 30, p.y + 4});
      },
      [ptr] { return ptr->lastWindowAction() == "move"; }});
  app.steps.push_back(Step{
      "a press on a Pressable in it is the Pressable's (no move)",
      [ptr] {
        ptr->clearLastWindowAction();
        auto p = center_in(mainId, "tb-button");
        send_in(mainId, P::Down, p);
        send_in(mainId, P::Move, {p.x + 12, p.y});
        send_in(mainId, P::Up, {p.x + 12, p.y});
      },
      [ptr] { return ptr->lastWindowAction().empty() && has_text(app.root, "presses 1"); }});
  app.steps.push_back(Step{
      "a double-click does gtk-titlebar-double-click",
      [ptr] {
        ptr->clearLastWindowAction();
        auto p = center_in(mainId, "title");
        for (int i = 0; i < 2; i++) {
          send_in(mainId, P::Down, p);
          send_in(mainId, P::Up, p);
        }
      },
      [ptr] {
        std::string want = titlebar_setting("gtk-titlebar-double-click");
        printf("  \"%s\"\n", want.c_str());
        return ptr->lastWindowAction() == want;
      }});
  app.steps.push_back(Step{
      "a right-click does gtk-titlebar-right-click (the window menu)",
      [ptr] {
        ptr->clearLastWindowAction();
        auto p = center_in(mainId, "title");
        send_in(mainId, P::Down, p, 3);
        send_in(mainId, P::Up, p, 3);
      },
      [ptr] {
        std::string want = titlebar_setting("gtk-titlebar-right-click");
        printf("  \"%s\"\n", want.c_str());
        return ptr->lastWindowAction() == want;
      }});
  app.steps.push_back(Step{
      "Windows.open titleBar 'hidden': a resizable window with a <TitleBar> of its own",
      [] { click("open-hidden"); },
      [] {
        GtkWindow *w = app.host->windowFor(ownId);
        if (!w || !gtk_widget_get_mapped(GTK_WIDGET(w))) return false;
        GtkWidget *bar = gtk_window_get_titlebar(w);
        auto controls = window_controls(by_id("child-titlebar"));
        if (!bar || gtk_widget_get_visible(bar) || controls.size() != 2) return false;
        // Its buttons fill the frame they were measured for (the frame
        // includes the theme's CSS margin, border and padding;
        // gtk_widget_get_width doesn't).
        for (GtkWidget *c : controls) {
          int natural = 0, unused;
          gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &unused, &natural, nullptr,
                             nullptr);
          if (int(rn_widget_get_frame(c).size.width) != natural) return false;
        }
        return true;
      }});
  app.steps.push_back(Step{
      "  ...its title bar drags that window",
      [] {
        auto *p = app.host->pointerHandlerFor(ownId);
        p->setRealInputEnabled(false);
        p->clearLastWindowAction();
        GtkWidget *tb = by_id("child-titlebar");
        GtkWidget *root = app.host->rootFor(ownId);
        graphene_rect_t b{};
        (void)gtk_widget_compute_bounds(tb, root, &b);
        // Past the start buttons, left of the title.
        graphene_point_t at{b.origin.x + b.size.width / 2, b.origin.y + 6};
        send_in(ownId, P::Down, at);
        send_in(ownId, P::Move, {at.x + 20, at.y + 20});
        send_in(ownId, P::Up, {at.x + 20, at.y + 20});
      },
      [] { return app.host->pointerHandlerFor(ownId)->lastWindowAction() == "move"; }});
  app.steps.push_back(Step{
      "transparent + titleBar 'none': no frame; only the views paint",
      [] { click("open-clear"); },
      [] {
        GtkWindow *w = app.host->windowFor(clearId);
        if (!w || !gtk_widget_get_mapped(GTK_WIDGET(w)) || !by_id("clear-panel")) return false;
        if (gtk_window_get_decorated(w)) return false;
        if (!gdk_display_is_composited(gtk_widget_get_display(GTK_WIDGET(w)))) {
          printf("  no compositor: an opaque window\n");
          return !gtk_widget_has_css_class(GTK_WIDGET(w), "rngtk-transparent");
        }
        GdkTexture *tex = rngtk::render_widget(GTK_WIDGET(w));
        if (!tex) return false;
        auto corner = px(tex, 4, 4);
        auto panel = px(tex, 180, 120);
        pixels.tex = nullptr;
        g_object_unref(tex);
        printf("  corner rgba(%d,%d,%d,%d), panel rgba(%d,%d,%d,%d)\n", corner.r, corner.g,
               corner.b, corner.a, panel.r, panel.g, panel.b, panel.a);
        return gtk_widget_has_css_class(GTK_WIDGET(w), "rngtk-transparent") && corner.a == 0 &&
               panel.a > 200;
      }});
  app.steps.push_back(Step{
      "  ...a windowDragRegion View drags it",
      [] {
        auto *p = app.host->pointerHandlerFor(clearId);
        p->setRealInputEnabled(false);
        p->clearLastWindowAction();
        auto at = center_in(clearId, "clear-panel");
        at.y -= 40;  // above the button
        send_in(clearId, P::Down, at);
        send_in(clearId, P::Move, {at.x - 20, at.y});
        send_in(clearId, P::Up, {at.x - 20, at.y});
      },
      [] { return app.host->pointerHandlerFor(clearId)->lastWindowAction() == "move"; }});
}

// ---------------------------------------------------------------------------
// GalleryDragDrop checks

bool is_dragdrop() { return opts.module == "GalleryDragDrop"; }

std::string dnd_dir;

// A folder with hello.txt (5 bytes) and pic.png, for the drops and the
// draggable image (passed as the `image` prop).
std::string make_dnd_folder() {
  char tmpl[] = "/tmp/rngtk-dnd-XXXXXX";
  if (!g_mkdtemp(tmpl)) return "";
  dnd_dir = tmpl;
  g_file_set_contents((dnd_dir + "/hello.txt").c_str(), "hello", 5, nullptr);
  std::string png = std::string(RNGTK_SOURCE_DIR) + "/examples/hello-world/assets/tile.png";
  gchar *data = nullptr;
  gsize size = 0;
  if (g_file_get_contents(png.c_str(), &data, &size, nullptr)) {
    g_file_set_contents((dnd_dir + "/pic.png").c_str(), data, gssize(size), nullptr);
    g_free(data);
  }
  gchar *uri = g_filename_to_uri((dnd_dir + "/pic.png").c_str(), nullptr, nullptr);
  std::string out = uri ? uri : "";
  g_free(uri);
  return out;
}

graphene_point_t center_in_root(const char *id) {
  GtkWidget *v = by_id(id);
  return v ? center_of(v) : graphene_point_t{-1, -1};
}

bool zone_says(const char *zone, const std::string &text) {
  GtkWidget *v = by_id(zone);
  return v && has_text(v, text);
}

void add_dragdrop_steps() {
  using Data = rngtk::GtkPointerHandler::DropData;
  auto *ptr = app.host->pointerHandler();
  ptr->setRealInputEnabled(false);
  static GdkTexture *picture = nullptr;
  app.steps.push_back(Step{
      "a file drag over a view whose draggedTypes take files: onDragEnter, with the types",
      [ptr] {
        auto p = center_in_root("zone-files");
        ptr->dragMotion(p.x, p.y, {"fileUrl"}, {"text/uri-list"});
      },
      [] { return zone_says("zone-files", "enter text/uri-list"); }});
  app.steps.push_back(Step{
      "  ...over one that takes only text: onDragLeave on the first, nothing on it",
      [ptr] {
        auto p = center_in_root("zone-text");
        check(!ptr->dragMotion(p.x, p.y, {"fileUrl"}, {"text/uri-list"}),
              "  the text zone doesn't take files");
      },
      [] { return zone_says("zone-files", "enter text/uri-list · leave") && !zone_says("zone-text", "enter"); }});
  app.steps.push_back(Step{
      "onDrop: files with name, type and size; links that aren't files as urls",
      [ptr] {
        auto p = center_in_root("zone-files");
        ptr->dragMotion(p.x, p.y, {"fileUrl"}, {"text/uri-list"});
        Data data;
        gchar *uri = g_filename_to_uri((dnd_dir + "/hello.txt").c_str(), nullptr, nullptr);
        data.uris = {uri, "https://reactnative.dev/"};
        g_free(uri);
        check(ptr->drop(p.x, p.y, data), "  the drop was taken");
      },
      [] {
        return zone_says("zone-files",
                         "files [hello.txt:text/plain:5] urls [https://reactnative.dev/] text null "
                         "types [text/uri-list]");
      }});
  app.steps.push_back(Step{
      "a text drop: dataTransfer.text",
      [ptr] {
        auto p = center_in_root("zone-text");
        ptr->dragMotion(p.x, p.y, {"string"}, {"text/plain;charset=utf-8"});
        Data data;
        data.hasText = true;
        data.text = "dropped words";
        ptr->drop(p.x, p.y, data);
      },
      [] {
        return zone_says("zone-text", "enter text/plain;charset=utf-8 · drop") &&
               zone_says("zone-text", "text \"dropped words\" types [text/plain]");
      }});
  app.steps.push_back(Step{
      "an image dropped as data: saved to a PNG the app can read",
      [ptr] {
        picture = gdk_texture_new_from_filename((dnd_dir + "/pic.png").c_str(), nullptr);
        auto p = center_in_root("zone-any");
        Data data;
        data.texture = picture;
        ptr->drop(p.x, p.y, data);
      },
      [] { return zone_says("zone-any", "files [drop-") && zone_says("zone-any", ".png:image/png:"); }});
  app.steps.push_back(Step{
      "an image offered both as a file and as image data arrives once, as the file",
      [ptr] {
        auto p = center_in_root("zone-any");
        Data data;
        gchar *uri = g_filename_to_uri((dnd_dir + "/pic.png").c_str(), nullptr, nullptr);
        data.uris = {uri};
        g_free(uri);
        data.texture = picture;
        ptr->drop(p.x, p.y, data);
      },
      [] {
        return zone_says("zone-any", "files [pic.png:image/png:") &&
               !zone_says("zone-any", ",drop-");
      }});
  app.steps.push_back(Step{"a view without draggedTypes takes nothing",
                           [ptr] {
                             auto p = center_in_root("no-zone");
                             Data data;
                             data.hasText = true;
                             data.text = "x";
                             check(!ptr->drop(p.x, p.y, data), "  not taken");
                           },
                           [] { return true; }});
  static std::string word;
  app.steps.push_back(Step{
      "selected text drags out as text (a press inside the selection keeps it)",
      [] {
        // Double-click "some", then press inside it.
        clicks(text_point("drag-text", 8), 2);
        word = selected();
        mouse(rngtk::GtkPointerHandler::Phase::Down, text_point("drag-text", 9));
      },
      [ptr] {
        if (word != "some" || selected() != "some") return false;
        auto at = text_point("drag-text", 9);
        GdkContentProvider *content = ptr->dragContentAt(at.x, at.y);
        if (!content) return false;
        GValue value = G_VALUE_INIT;
        g_value_init(&value, G_TYPE_STRING);
        bool ok = gdk_content_provider_get_value(content, &value, nullptr) &&
                  std::string(g_value_get_string(&value)) == "some";
        g_value_unset(&value);
        g_object_unref(content);
        return ok;
      }});
  app.steps.push_back(Step{"  ...a release without a drag puts the caret there instead",
                           [] { mouse(rngtk::GtkPointerHandler::Phase::Up, text_point("drag-text", 9)); },
                           [] { return selected().empty(); }});
  app.steps.push_back(Step{
      "outside a selection, text doesn't drag", [] {},
      [ptr] {
        auto at = text_point("drag-text", 30);
        GdkContentProvider *content = ptr->dragContentAt(at.x, at.y);
        if (content) g_object_unref(content);
        return content == nullptr;
      }});
  app.steps.push_back(Step{
      "an Image with draggable drags out its picture and its file",
      [] {},
      [ptr] {
        auto p = center_in_root("drag-image");
        GdkContentProvider *content = ptr->dragContentAt(p.x, p.y);
        if (!content) return false;
        GdkContentFormats *formats = gdk_content_provider_ref_formats(content);
        bool ok = gdk_content_formats_contain_gtype(formats, GDK_TYPE_TEXTURE) &&
                  gdk_content_formats_contain_gtype(formats, G_TYPE_FILE);
        GValue value = G_VALUE_INIT;
        g_value_init(&value, G_TYPE_FILE);
        if (ok && gdk_content_provider_get_value(content, &value, nullptr)) {
          char *path = g_file_get_path(G_FILE(g_value_get_object(&value)));
          ok = path && std::string(path) == dnd_dir + "/pic.png";
          g_free(path);
        }
        g_value_unset(&value);
        gdk_content_formats_unref(formats);
        g_object_unref(content);
        return ok;
      }});
  app.steps.push_back(Step{"  ...an Image without it doesn't", [] {},
                           [ptr] {
                             auto p = center_in_root("still-image");
                             GdkContentProvider *content = ptr->dragContentAt(p.x, p.y);
                             if (content) g_object_unref(content);
                             return content == nullptr;
                           }});
  app.steps.push_back(Step{
      "the window's root has GTK's drop target and drag source",
      [] {},
      [] {
        bool drop = false, drag = false;
        GListModel *controllers = gtk_widget_observe_controllers(app.root);
        for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
          auto *c = G_OBJECT(g_list_model_get_item(controllers, i));
          drop |= GTK_IS_DROP_TARGET_ASYNC(c);
          drag |= GTK_IS_DRAG_SOURCE(c);
          g_object_unref(c);
        }
        g_object_unref(controllers);
        g_clear_object(&picture);
        return drop && drag;
      }});
}

// ---------------------------------------------------------------------------
// GalleryNotifications checks: on a private session bus (dbus-run-session),
// with a stand-in freedesktop notification server.

bool is_notifications() { return opts.module == "GalleryNotifications"; }

struct FakeNotification {
  guint id;
  guint replaces;
  std::string summary, body, icon;
  std::vector<std::string> actions;  // key, label, key, label...
  int urgency = -1;
};
struct FakeServer {
  GDBusConnection *connection = nullptr;
  bool owned = false, lost = false;
  std::vector<FakeNotification> notes;
  std::vector<guint> closed;
  guint next = 1;
} fake;

const char *kNotificationsXml =
    "<node><interface name='org.freedesktop.Notifications'>"
    "<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
    "<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
    "<arg type='as' direction='in'/><arg type='a{sv}' direction='in'/>"
    "<arg type='i' direction='in'/><arg type='u' direction='out'/></method>"
    "<method name='CloseNotification'><arg type='u' direction='in'/></method>"
    "<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
    "<method name='GetServerInformation'><arg type='s' direction='out'/>"
    "<arg type='s' direction='out'/><arg type='s' direction='out'/>"
    "<arg type='s' direction='out'/></method>"
    "<signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal>"
    "<signal name='ActionInvoked'><arg type='u'/><arg type='s'/></signal>"
    "</interface></node>";

void fake_call(GDBusConnection *, const char *, const char *, const char *, const char *method,
               GVariant *params, GDBusMethodInvocation *invocation, gpointer) {
  if (!strcmp(method, "Notify")) {
    const char *app = nullptr, *icon = nullptr, *summary = nullptr, *body = nullptr;
    guint replaces = 0;
    GVariantIter *actions = nullptr;
    GVariant *hints = nullptr;
    gint32 timeout = 0;
    g_variant_get(params, "(&su&s&s&sas@a{sv}i)", &app, &replaces, &icon, &summary, &body,
                  &actions, &hints, &timeout);
    FakeNotification n{replaces ? replaces : fake.next++, replaces, summary, body, icon};
    const char *a = nullptr;
    while (g_variant_iter_next(actions, "&s", &a)) n.actions.emplace_back(a);
    g_variant_iter_free(actions);
    guchar urgency = 0;
    if (g_variant_lookup(hints, "urgency", "y", &urgency)) n.urgency = urgency;
    // GLib sends the icon as a hint ("image-path") when it isn't a name.
    if (n.icon.empty()) {
      const char *path = nullptr;
      if (g_variant_lookup(hints, "image-path", "&s", &path)) n.icon = path;
    }
    g_variant_unref(hints);
    guint id = n.id;
    fake.notes.push_back(std::move(n));
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", id));
  } else if (!strcmp(method, "CloseNotification")) {
    guint id = 0;
    g_variant_get(params, "(u)", &id);
    fake.closed.push_back(id);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (!strcmp(method, "GetCapabilities")) {
    const char *caps[] = {"actions", "body", "icon-static", "persistence", nullptr};
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(^as)", caps));
  } else {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(ssss)", "rngtk-test", "react-native-gtk4", "1", "1.2"));
  }
}

void start_fake_notification_server() {
  g_bus_own_name(
      G_BUS_TYPE_SESSION, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE,
      [](GDBusConnection *connection, const char *, gpointer) {
        static GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(kNotificationsXml, nullptr);
        static const GDBusInterfaceVTable vtable = {fake_call, nullptr, nullptr, {}};
        fake.connection = connection;
        g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                          info->interfaces[0], &vtable, nullptr, nullptr,
                                          nullptr);
      },
      [](GDBusConnection *, const char *, gpointer) { fake.owned = true; },
      [](GDBusConnection *, const char *, gpointer) { fake.lost = true; }, nullptr, nullptr);
}

// The notification server's click on notification `id` (its action key).
void fake_invoke(guint id, const std::string &key) {
  g_dbus_connection_emit_signal(fake.connection, nullptr, "/org/freedesktop/Notifications",
                                "org.freedesktop.Notifications", "ActionInvoked",
                                g_variant_new("(us)", id, key.c_str()), nullptr);
}

std::string action_key(const FakeNotification &n, const std::string &label) {
  for (size_t i = 0; i + 1 < n.actions.size(); i += 2) {
    if (n.actions[i + 1] == label) return n.actions[i];
  }
  return "";
}

void add_notifications_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "a private bus with a stand-in notification server (run under dbus-run-session)", [] {},
      [] {
        if (fake.lost) {
          check(false, "  another notification server owns the name: run under dbus-run-session");
          return true;
        }
        return fake.owned;
      }});
  app.steps.push_back(Step{
      "Notifications.show: the server gets the title, body, icon and buttons",
      [] { click("notify"); },
      [] {
        if (fake.notes.size() != 1) return false;
        const auto &n = fake.notes[0];
        std::string actions;
        for (const auto &a : n.actions) actions += a + ";";
        printf("  summary \"%s\" body \"%s\" icon \"%s\" actions %s\n", n.summary.c_str(),
               n.body.c_str(), n.icon.c_str(), actions.c_str());
        return n.summary == "Download finished" && n.body == "notes.pdf (2 MB)" &&
               n.icon == "folder-download-symbolic" && action_key(n, "Open") != "" &&
               action_key(n, "Show in Files") != "" && n.actions[0] == "default";
      }});
  app.steps.push_back(Step{"a button: 'press' with the button's id, and onPress",
                           [] { fake_invoke(fake.notes[0].id, action_key(fake.notes[0], "Open")); },
                           [] {
                             return has_text(app.root, "press download open") &&
                                    has_text(app.root, "onPress open");
                           }});
  // (Once one of its actions ran, GLib forgets a notification, as servers
  // close it then: the next show is a new one.)
  app.steps.push_back(Step{
      "clicking the notification raises the app's window; 'press' with 'default'",
      [] { click("notify"); },
      [] {
        if (fake.notes.size() < 2) return false;
        static bool clicked = false;
        if (!clicked) {
          clicked = true;
          gtk_widget_set_visible(app.window, FALSE);
          fake_invoke(fake.notes[1].id, "default");
          return false;
        }
        return has_text(app.root, "press download default") &&
               gtk_widget_get_visible(app.window);
      }});
  app.steps.push_back(Step{"showing the same id again replaces it",
                           [] { click("notify"); },
                           [] {
                             // Again once the server has numbered the first.
                             static bool again = false;
                             if (fake.notes.size() == 3 && !again) {
                               again = true;
                               click("notify");
                             }
                             return fake.notes.size() == 4 &&
                                    fake.notes[3].replaces == fake.notes[2].id;
                           }});
  app.steps.push_back(Step{"close: withdrawn from the server",
                           [] { click("close"); },
                           [] {
                             return !fake.closed.empty() && fake.closed.back() == fake.notes[2].id;
                           }});
  app.steps.push_back(Step{"priority 'urgent': the urgency hint is critical",
                           [] { click("urgent"); },
                           [] {
                             return fake.notes.size() == 5 && fake.notes[4].urgency == 2 &&
                                    fake.notes[4].summary == "Battery low";
                           }});
}

// ---------------------------------------------------------------------------
// GalleryNativeModule checks: the native library template's module and
// component, registered as a package.

bool is_native_module() { return opts.module == "GalleryNativeModule"; }

GtkCalendar *calendar() {
  GtkWidget *w = by_id("calendar");
  return w && GTK_IS_CALENDAR(w) ? GTK_CALENDAR(w) : nullptr;
}

std::string calendar_day() {
  GtkCalendar *c = calendar();
  if (!c) return "";
  GDateTime *date = gtk_calendar_get_date(c);
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", g_date_time_get_year(date),
           g_date_time_get_month(date), g_date_time_get_day_of_month(date));
  g_date_time_unref(date);
  return buffer;
}

// RNGtkAnimatedProbe: a library component (registered as a package, like
// the template's) whose update() records the distinct `value`s it gets.
// GalleryNativeModule animates `value` on the native driver: they must
// arrive frame by frame (synchronouslyUpdateViewOnUIThread), not only with
// the next commit.
namespace animated_probe {
extern const char ComponentName[] = "RNGtkAnimatedProbe";
class Props final : public facebook::react::ViewProps {
 public:
  Props() = default;
  Props(const facebook::react::PropsParserContext &context, const Props &source,
        const facebook::react::RawProps &raw)
      : ViewProps(context, source, raw),
        value(facebook::react::convertRawProp(context, raw, "value", source.value, 0.0f)) {}
  float value = 0;
};
using ShadowNode = facebook::react::ConcreteViewShadowNode<ComponentName, Props>;
using Descriptor = facebook::react::ConcreteComponentDescriptor<ShadowNode>;
std::set<float> values;

std::shared_ptr<const rngtk::Package> package() {
  auto p = std::make_shared<rngtk::Package>();
  p->name = "animated-probe";
  rngtk::NativeComponent c;
  c.descriptor = facebook::react::concreteComponentDescriptorProvider<Descriptor>();
  c.create = [](const facebook::react::ShadowView &) { return gtk_drawing_area_new(); };
  c.update = [](GtkWidget *, const facebook::react::ShadowView &, const facebook::react::ShadowView &view) {
    if (auto props = std::dynamic_pointer_cast<const Props>(view.props)) values.insert(props->value);
  };
  p->components.push_back(c);
  return p;
}
}  // namespace animated_probe

void add_native_module_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "a library's TurboModule: a sync method, a promise (from the main thread), dynamic values",
      [] {},
      [] {
        std::string version = std::to_string(gtk_get_major_version()) + "." +
                              std::to_string(gtk_get_minor_version()) + "." +
                              std::to_string(gtk_get_micro_version());
        return has_text(app.root, "greet: Hello, GTK, from C++! · gtkVersion: " + version) &&
               has_text(app.root, "\"type\":\"object\"") && has_text(app.root, "\"size\":2");
      }});
  app.steps.push_back(Step{
      "a library's native component: its GTK widget, at its Yoga frame, with its props",
      [] {},
      [] {
        GtkCalendar *c = calendar();
        if (!c) return false;
        graphene_rect_t f = rn_widget_get_frame(GTK_WIDGET(c));
        return f.size.width == 320 && f.size.height == 300 && calendar_day() == "2026-10-09" &&
               !gtk_calendar_get_show_week_numbers(c);
      }});
  app.steps.push_back(Step{"  ...props from JS update it", [] {
                             click("set-date");
                             click("weeks");
                           },
                           [] {
                             return calendar_day() == "2027-01-15" &&
                                    gtk_calendar_get_show_week_numbers(calendar()) &&
                                    has_text(app.root, "picked -");
                           }});
  app.steps.push_back(Step{
      "  ...its event: picking a day in the calendar reaches onDateChange",
      [] {
        GDateTime *d = g_date_time_new_local(2027, 1, 20, 0, 0, 0);
        gtk_calendar_select_day(calendar(), d);
        g_date_time_unref(d);
      },
      [] { return has_text(app.root, "picked 2027-01-20"); }});
  app.steps.push_back(Step{
      "  ...its own prop on the native driver reaches update() each frame, not at the next commit",
      [] {
        animated_probe::values.clear();
        click("probe-animate");
      },
      [] {
        // 1.5 s at 60 fps: about 90 values; with them only at commits, 2.
        if (!has_text(app.root, "probe idle") || animated_probe::values.size() < 20) return false;
        printf("  (%zu distinct values)\n", animated_probe::values.size());
        return true;
      }});
  app.steps.push_back(Step{
      "  ...a command from JS (Commands.showToday)",
      [] { click("today"); },
      [] {
        GDateTime *now = g_date_time_new_now_local();
        char today[16];
        snprintf(today, sizeof(today), "%04d-%02d-%02d", g_date_time_get_year(now),
                 g_date_time_get_month(now), g_date_time_get_day_of_month(now));
        g_date_time_unref(now);
        return calendar_day() == today && has_text(app.root, std::string("picked ") + today);
      }});
}

// ---------------------------------------------------------------------------
// GalleryNavigation checks (React Navigation's native-stack and bottom tabs
// on react-native-screens)

bool is_navigation() { return opts.module == "GalleryNavigation"; }

// A Text showing `text` (exactly, or containing it), on screen: the stack
// keeps the screens below the top one mounted, with display: none (web
// components) or in pages libadwaita unmaps (the native port).
GtkWidget *shown_text(GtkWidget *widget, const std::string &text, bool exact = true) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (!gtk_widget_get_mapped(c)) continue;
    if (RN_IS_TEXT(c)) {
      std::string t = rn_text_get_text(RN_TEXT(c));
      if (exact ? t == text : t.find(text) != std::string::npos) return c;
    }
    if (GtkWidget *found = shown_text(c, text, exact)) return found;
  }
  return nullptr;
}

bool shows(const std::string &text) { return shown_text(app.root, text) != nullptr; }

// A GTK label (a native header's title) showing `text`, on screen.
GtkWidget *shown_label(GtkWidget *widget, const std::string &text) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (!gtk_widget_get_mapped(c)) continue;
    if (GTK_IS_LABEL(c) && text == gtk_label_get_text(GTK_LABEL(c))) return c;
    if (GtkWidget *found = shown_label(c, text)) return found;
  }
  return nullptr;
}

// The header's title: a Text (web components) or the header bar's label.
GtkWidget *header_title(const std::string &title) {
  GtkWidget *t = shown_text(app.root, title);
  return t ? t : shown_label(app.root, title);
}

// The native port's header back button (libadwaita's, which activates
// navigation.pop), on screen.
GtkWidget *native_back_button(GtkWidget *widget) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (!gtk_widget_get_mapped(c)) continue;
    if (GTK_IS_BUTTON(c) && g_strcmp0(gtk_actionable_get_action_name(GTK_ACTIONABLE(c)),
                                      "navigation.pop") == 0) {
      return c;
    }
    if (GtkWidget *found = native_back_button(c)) return found;
  }
  return nullptr;
}

// The react-native-screens port's components are on screen (else its web
// fallback: a bundle made with RNGTK_IGNORE_PORTS).
bool find_type(GtkWidget *widget, const char *type) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c)) {
    if (!strcmp(G_OBJECT_TYPE_NAME(c), type) || find_type(c, type)) return true;
  }
  return false;
}
bool native_screens() { return find_type(app.root, "AdwNavigationView"); }

bool stack_is(const std::string &stack) { return shows("Stack: " + stack); }

void click_label(const char *label) {
  GtkWidget *v = app.host->mountingManager().viewForAccessibilityLabel(label);
  if (!v) return;
  graphene_point_t c = center_of(v);
  send(rngtk::GtkPointerHandler::Phase::Down, c);
  send(rngtk::GtkPointerHandler::Phase::Up, c);
}

constexpr guint kLeft = 113;

// The view `id` takes clicks: on screen, with no navigation view in a
// transition (libadwaita's pages take no input while they slide).
bool nav_settled(GtkWidget *widget) {
  bool nav = !strcmp(G_OBJECT_TYPE_NAME(widget), "AdwNavigationView");
  int pages = 0;
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (nav && gtk_widget_get_mapped(c) && !strcmp(G_OBJECT_TYPE_NAME(c), "AdwNavigationPage")) pages++;
    if (!nav_settled(c)) return false;
  }
  return pages <= 1;
}

bool targetable(const char *id) {
  if (!nav_settled(app.root)) return false;
  GtkWidget *v = by_id(id);
  if (!v || !gtk_widget_get_mapped(v)) return false;
  for (GtkWidget *w = v; w; w = gtk_widget_get_parent(w)) {
    if (!gtk_widget_get_can_target(w)) return false;
  }
  return true;
}

// Feed's red title is visible: shown, and not covered.
bool feed_drawn() {
  GtkWidget *title = shown_text(app.root, "Feed tab");
  if (!title) return false;
  GdkTexture *tex = rngtk::render_widget(app.root);
  if (!tex) return true;
  bool red = any_pixel(tex, bounds_in_root(title), [](rngtk::Rgba8 p) {
    return p.r > 200 && p.g < 120 && p.b < 120;
  });
  pixels.tex = nullptr;
  g_object_unref(tex);
  return red;
}

void add_navigation_steps() {
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{
      "native-stack: the first screen, with its header title and button",
      [] {},
      [] { return shows("Home screen") && header_title("Navigation") && targetable("nav-header-right") &&
                  stack_is("Home"); }});
  app.steps.push_back(Step{"  ...the header button works", [] { click("nav-header-right"); },
                           [] { return shown_text(app.root, "header button", false); }});
  app.steps.push_back(Step{
      "push: the new screen shows (the one below hides), with its title and header color",
      [] { click("nav-push"); },
      [] {
        GtkWidget *title = header_title("Details 1");
        if (!title || shows("Home screen") || !targetable("nav-push-more") ||
            !stack_is("Home > Details 1")) {
          return false;
        }
        GdkTexture *tex = rngtk::render_widget(app.root);
        if (!tex) return false;
        graphene_rect_t b = bounds_in_root(title);
        auto p = px(tex, b.origin.x + b.size.width + 24, b.origin.y + 2);
        pixels.tex = nullptr;
        g_object_unref(tex);
        return near_color(p, 0x35, 0x84, 0xE4);
      }});
  app.steps.push_back(Step{"  ...the screens' focus events", [] {},
                           [] { return shown_text(app.root, "blur Home", false) &&
                                       shown_text(app.root, "focus Details 1", false); }});
  if (native_screens()) {
    // headerSearchBarOptions: RNSSearchBar, a GtkSearchEntry under the
    // header; typing reaches onChangeText.
    app.steps.push_back(Step{"  ...its header's search bar (onChangeText)",
                             [] {
                               GtkWidget *entry = nullptr;
                               std::function<void(GtkWidget *)> find = [&](GtkWidget *w) {
                                 for (GtkWidget *c = gtk_widget_get_first_child(w); c && !entry;
                                      c = gtk_widget_get_next_sibling(c)) {
                                   if (gtk_widget_get_mapped(c) && GTK_IS_SEARCH_ENTRY(c)) entry = c;
                                   find(c);
                                 }
                               };
                               find(app.root);
                               if (check(entry != nullptr, "  the header has a search entry")) {
                                 gtk_editable_set_text(GTK_EDITABLE(entry), "gtk");
                               }
                             },
                             [] { return shown_text(app.root, "search \"gtk\"", false) != nullptr; }});
  }
  app.steps.push_back(Step{"push another (navigation.push)", [] { click("nav-push-more"); },
                           [] { return shows("Details screen #2") && !shows("Details screen #1") &&
                                       targetable("nav-push-more") &&
                                       stack_is("Home > Details 1 > Details 2"); }});
  app.steps.push_back(Step{native_screens() ? "the header's back button (libadwaita's) pops"
                                            : "the header's back button pops",
                           [] {
                             if (!native_screens()) return click_label("Go back");
                             GtkWidget *back = native_back_button(app.root);
                             if (check(back != nullptr, "  the header has a back button")) {
                               g_signal_emit_by_name(back, "clicked");
                             }
                           },
                           [] { return shows("Details screen #1") && !shows("Details screen #2") &&
                                       targetable("nav-push-more") && stack_is("Home > Details 1"); }});
  app.steps.push_back(Step{
      "Alt+Left pops (BackHandler's hardwareBackPress)",
      [] { check(key(GDK_KEY_Left, kLeft, GDK_ALT_MASK), "  Alt+Left handled"); },
      [] { return shows("Home screen") && !shows("Details screen #1") && targetable("nav-push") &&
                  stack_is("Home"); }});
  app.steps.push_back(Step{"  ...and does nothing on the first screen",
                           [] { key(GDK_KEY_Left, kLeft, GDK_ALT_MASK); },
                           [] { return shows("Home screen") && stack_is("Home"); }});
  app.steps.push_back(Step{"push, then the mouse's back button pops",
                           [] { click("nav-push"); },
                           [] { return targetable("nav-push-more"); }});
  app.steps.push_back(Step{"  ...(button 8)",
                           [] {
                             graphene_point_t c = center_of(by_id("nav-push-more"));
                             mouse(rngtk::GtkPointerHandler::Phase::Down, c, 8);
                             mouse(rngtk::GtkPointerHandler::Phase::Up, c, 8);
                           },
                           [] { return shows("Home screen") && !shows("Details screen #1") &&
                                       targetable("nav-push") && stack_is("Home"); }});
  app.steps.push_back(Step{"pop to top", [] { click("nav-push"); },
                           [] { return shows("Details screen #1") && targetable("nav-push-more"); }});
  app.steps.push_back(Step{"  ...", [] { click("nav-push-more"); },
                           [] { return shows("Details screen #2") && targetable("nav-pop-top"); }});
  app.steps.push_back(Step{"  ...popToTop()", [] { click("nav-pop-top"); },
                           [] { return shows("Home screen") && !shows("Details screen #2") &&
                                       targetable("nav-modal") && stack_is("Home"); }});
  app.steps.push_back(Step{"a modal screen (presentation: 'modal')", [] { click("nav-modal"); },
                           [] { return shows("Modal screen") && targetable("nav-close") &&
                                       stack_is("Home > Modal"); }});
  app.steps.push_back(Step{"  ...closes", [] { click("nav-close"); },
                           [] { return shows("Home screen") && targetable("nav-modal") && stack_is("Home"); }});
  app.steps.push_back(Step{"  ...opens again: its header has headerLeft and headerRight",
                           [] { click("nav-modal"); },
                           [] { return shows("Modal screen") && targetable("nav-cancel") &&
                                       targetable("nav-done") && stack_is("Home > Modal"); }});
  app.steps.push_back(Step{"  ...headerLeft's Cancel closes it", [] { click("nav-cancel"); },
                           [] { return !shows("Modal screen") && targetable("nav-sheet") &&
                                       stack_is("Home"); }});
  app.steps.push_back(Step{"a form sheet (presentation: 'formSheet', half the height)",
                           [] { click("nav-sheet"); },
                           [] {
                             GtkWidget *close = by_id("nav-sheet-close");
                             if (!shows("Sheet screen") || !targetable("nav-sheet-close") ||
                                 !stack_is("Home > Sheet")) {
                               return false;
                             }
                             if (!native_screens()) return true;
                             // A card over the stack, not all of it.
                             GtkWidget *screen = close;
                             while (screen && strcmp(G_OBJECT_TYPE_NAME(gtk_widget_get_parent(screen)),
                                                     "AdwToolbarView")) {
                               screen = gtk_widget_get_parent(screen);
                             }
                             return screen && gtk_widget_get_width(screen) < 700 &&
                                    gtk_widget_get_height(screen) < 400;
                           }});
  app.steps.push_back(Step{"  ...closes", [] { click("nav-sheet-close"); },
                           [] { return !shows("Sheet screen") && targetable("nav-modal") &&
                                       stack_is("Home"); }});
  if (native_screens()) {
    // A modal is a layer of its own over the stack; Escape dismisses it and
    // tells React (onDismissed).
    app.steps.push_back(Step{"  ...opens again, in a layer with its own header",
                             [] { click("nav-modal"); },
                             [] { return shows("Modal screen") && header_title("Modal") &&
                                         targetable("nav-close") && stack_is("Home > Modal"); }});
    app.steps.push_back(Step{"  ...Escape dismisses it (onDismissed)",
                             [] {
                               GtkWidget *w = by_id("nav-close");
                               while (w && !GTK_IS_REVEALER(w)) w = gtk_widget_get_parent(w);
                               if (!check(w != nullptr, "  the modal is in a layer")) return;
                               // The layer's Escape shortcut, as a key press would trigger it.
                               GListModel *controllers = gtk_widget_observe_controllers(w);
                               for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
                                 auto *c = GTK_EVENT_CONTROLLER(g_list_model_get_item(controllers, i));
                                 if (GTK_IS_SHORTCUT_CONTROLLER(c)) {
                                   auto *model = G_LIST_MODEL(c);
                                   for (guint j = 0; j < g_list_model_get_n_items(model); j++) {
                                     auto *sc = GTK_SHORTCUT(g_list_model_get_item(model, j));
                                     gtk_shortcut_action_activate(gtk_shortcut_get_action(sc),
                                                                  GTK_SHORTCUT_ACTION_EXCLUSIVE, w,
                                                                  nullptr);
                                     g_object_unref(sc);
                                   }
                                 }
                                 g_object_unref(c);
                               }
                               g_object_unref(controllers);
                             },
                             [] { return !shows("Modal screen") && targetable("nav-bare") &&
                                         stack_is("Home"); }});
  }
  // usePreventRemove: going back asks React, which refuses.
  app.steps.push_back(Step{"a guarded screen (usePreventRemove)", [] { click("nav-guarded"); },
                           [] { return shows("Guarded screen") && targetable("nav-guard-leave") &&
                                       stack_is("Home > Guarded"); }});
  app.steps.push_back(Step{native_screens() ? "  ...its back button (ours: libadwaita's can't pop it) is refused"
                                            : "  ...its back button is refused",
                           [] {
                             if (!native_screens()) return click_label("Go back");
                             check(native_back_button(app.root) == nullptr,
                                   "  no libadwaita back button");
                             GtkWidget *back = nullptr;
                             std::function<void(GtkWidget *)> find = [&](GtkWidget *w) {
                               for (GtkWidget *c = gtk_widget_get_first_child(w); c && !back;
                                    c = gtk_widget_get_next_sibling(c)) {
                                 if (gtk_widget_get_mapped(c) && GTK_IS_BUTTON(c) &&
                                     gtk_widget_has_css_class(c, "back")) {
                                   back = c;
                                 }
                                 find(c);
                               }
                             };
                             find(app.root);
                             if (check(back != nullptr, "  the header has our back button")) {
                               g_signal_emit_by_name(back, "clicked");
                             }
                           },
                           [] { return shown_text(app.root, "remove prevented", false) &&
                                       shows("Guarded screen") && stack_is("Home > Guarded"); }});
  app.steps.push_back(Step{"  ...Leave", [] { click("nav-guard-leave"); },
                           [] { return !shows("Guarded screen") && targetable("nav-bare") &&
                                       stack_is("Home"); }});
  app.steps.push_back(Step{"a screen without a header (headerShown: false)", [] { click("nav-bare"); },
                           [] { return shows("Screen without a header") && !header_title("Navigation") &&
                                       targetable("nav-bare-back") && stack_is("Home > Bare"); }});
  app.steps.push_back(Step{"  ...back", [] { click("nav-bare-back"); },
                           [] { return shows("Home screen") && !shows("Screen without a header") &&
                                       targetable("nav-tabs"); }});
  app.steps.push_back(Step{"bottom tabs inside the stack", [] { click("nav-tabs"); },
                           [] { return feed_drawn() && nav_settled(app.root) && stack_is("Home > Tabs / Feed"); }});
  // Without native screens the inactive tab stays mounted under the
  // active one (zIndex -1), as on the web: Feed's red title is covered.
  app.steps.push_back(Step{"  ...switching tabs", [] { click_label("Settings tab button"); },
                           [] { return shows("Settings tab") && !feed_drawn() &&
                                       stack_is("Home > Tabs / Settings") &&
                                       shown_text(app.root, "blur Feed", false) &&
                                       shown_text(app.root, "focus Settings", false); }});
  // Tabs' backBehavior 'firstRoute': back goes to the first tab, then
  // leaves the tabs.
  app.steps.push_back(Step{"  ...Alt+Left goes back to the first tab",
                           [] { key(GDK_KEY_Left, kLeft, GDK_ALT_MASK); },
                           [] { return feed_drawn() && stack_is("Home > Tabs / Feed"); }});
  app.steps.push_back(Step{"  ...then leaves the tabs",
                           [] { key(GDK_KEY_Left, kLeft, GDK_ALT_MASK); },
                           [] { return shows("Home screen") && stack_is("Home"); }});
  app.steps.push_back(Step{"no JS errors", [] {}, [] {
                             return check(app.host->jsErrorCount() == 0, "  no JS errors");
                           }});
}

// ---------------------------------------------------------------------------
// GalleryGestures checks (react-native-gesture-handler, packages/gesture-handler)

bool is_gestures() { return opts.module == "GalleryGestures"; }

// A press, moves of (dx, dy) in `n` steps `ms` apart, and (optionally) the
// release, with the mouse, on the view `id`.
void gh_drag(const char *id, double fx, double fy, double dx, double dy, int n, int ms, bool release = true) {
  using Phase = rngtk::GtkPointerHandler::Phase;
  GtkWidget *v = by_id(id);
  if (!v) return;
  graphene_rect_t b = bounds_in_root(v);
  graphene_point_t p{float(b.origin.x + b.size.width * fx), float(b.origin.y + b.size.height * fy)};
  mouse_ms += 1000;
  mouse(Phase::Down, p);
  for (int i = 1; i <= n; i++) {
    mouse_ms += ms;
    mouse(Phase::Move, graphene_point_t{float(p.x + dx * i / n), float(p.y + dy * i / n)});
  }
  if (release) {
    mouse_ms += ms;
    mouse(Phase::Up, graphene_point_t{float(p.x + dx), float(p.y + dy)});
  }
}

bool gh_status(const std::string &needle) { return shown_text(app.root, needle, false) != nullptr; }

void add_gestures_steps() {
  using Phase = rngtk::GtkPointerHandler::Phase;
  app.host->pointerHandler()->setRealInputEnabled(false);
  app.steps.push_back(Step{"a tap (useTapGesture on a GestureDetector)",
                           [] { clicks(center_of(by_id("gh-tap")), 1); },
                           [] { return gh_status("taps 1 ·"); }});
  app.steps.push_back(Step{"a double tap (numberOfTaps: 2)",
                           [] { clicks(center_of(by_id("gh-double")), 2); },
                           [] { return gh_status("double taps 1 ·"); }});
  app.steps.push_back(after_frames("  ...a single click isn't one", 40, [] {
    static bool clicked = false;
    if (!clicked) {
      clicked = true;
      clicks(center_of(by_id("gh-double")), 1);
    }
    return gh_status("double taps 1 ·");
  }));
  app.steps.push_back(Step{"a long press (minDuration 300 ms)",
                           [] {
                             mouse_ms += 1000;
                             mouse(Phase::Down, center_of(by_id("gh-long")));
                           },
                           [] { return gh_status("long presses 1 ·"); }});
  app.steps.push_back(Step{"  ...released", [] { mouse(Phase::Up, center_of(by_id("gh-long"))); },
                           [] { return true; }});
  // The pan box holds a Pressable: pressing on it and dragging pans, and
  // the press is cancelled.
  static float pan_x0 = 0;
  // Translation counts from where the pan activated (past the 15 px touch
  // slop: the second move, at 20,10), as on Android and the web.
  app.steps.push_back(Step{"a pan (usePanGesture) moves the box, by translationX/Y",
                           [] {
                             pan_x0 = bounds_in_root(by_id("gh-pan")).origin.x;
                             gh_drag("gh-press", 0.5, 0.5, 60, 30, 6, 16, false);
                           },
                           [] {
                             return gh_status("pan 40,20 active") &&
                                    std::abs(bounds_in_root(by_id("gh-pan")).origin.x - pan_x0 - 40) < 1;
                           }});
  app.steps.push_back(Step{"  ...released: it ends, and the Pressable under it wasn't pressed",
                           [] {
                             graphene_point_t c = center_of(by_id("gh-press"));
                             mouse_ms += 16;
                             mouse(Phase::Up, c);
                           },
                           [] { return gh_status("pan 40,20 ended") && gh_status("presses 0 ·"); }});
  app.steps.push_back(Step{"a click on that Pressable presses it (the pan fails)",
                           [] { clicks(center_of(by_id("gh-press")), 1); },
                           [] { return gh_status("presses 1 ·") && gh_status("ended"); }});
  app.steps.push_back(Step{"a fling to the right (useFlingGesture)",
                           [] { gh_drag("gh-fling", 0.2, 0.5, 90, 0, 6, 8); },
                           [] { return gh_status("flings 1 ·"); }});
  app.steps.push_back(Step{"  ...a slow drag isn't one", [] { gh_drag("gh-fling", 0.2, 0.5, 60, 0, 6, 120); },
                           [] { return gh_status("flings 1 ·"); }});
  app.steps.push_back(Step{"hover (useHoverGesture): the mouse over it",
                           [] {
                             mouse_ms += 50;
                             mouse(Phase::Move, center_of(by_id("gh-hover")));
                           },
                           [] { return gh_status("hover in"); }});
  app.steps.push_back(Step{"  ...and away", [] {
                             graphene_rect_t b = bounds_in_root(by_id("gh-hover"));
                             mouse_ms += 50;
                             mouse(Phase::Move, graphene_point_t{b.origin.x + b.size.width + 40, b.origin.y + 10});
                           },
                           [] { return gh_status("hover out"); }});
  app.steps.push_back(Step{"the builder API: Gesture.Tap() (device events)",
                           [] { clicks(center_of(by_id("gh-legacy-tap")), 1); },
                           [] { return gh_status("legacy taps 1 ·"); }});
  app.steps.push_back(Step{"  ...Gesture.Pan()", [] { gh_drag("gh-legacy-pan", 0.5, 0.5, 40, -20, 4, 16); },
                           [] { return gh_status("legacy pan 20,-10"); }});
  app.steps.push_back(Step{"no JS errors", [] {}, [] {
                             return check(app.host->jsErrorCount() == 0, "  no JS errors");
                           }});
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
  if (phase == Phase::Steps) app.steps_after = app.phase;
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

// --test-animation: TouchableOpacity's fade is a native-driver
// Animated.timing that C++ Animated runs on GTK's frame clock. Each JS
// instance needs its own Animated provider; one kept from before a reload
// would drive the destroyed instance and the card would never fade.
void add_animation_steps() {
  using Phase = rngtk::GtkPointerHandler::Phase;
  app.host->pointerHandler()->setRealInputEnabled(false);
  int instance = app.host->instanceCount();
  app.steps.push_back(Step{
      "JS instance " + std::to_string(instance) +
          ": holding the card fades it (native Animated)",
      [] {
        if (GtkWidget *v = by_id("card")) send(Phase::Down, center_of(v));
      },
      [] {
        GtkWidget *v = by_id("card");
        return v && gtk_widget_get_opacity(v) < 0.5;
      }});
  app.steps.push_back(Step{
      "  ...and it fades back after release",
      [] {
        if (GtkWidget *v = by_id("card")) send(Phase::Up, center_of(v));
      },
      [] {
        GtkWidget *v = by_id("card");
        return v && gtk_widget_get_opacity(v) > 0.99;
      }});
}

// The dev-loop check after `done`, or quit.
void next_check(Phase done) {
  if (!opts.self_test) {
    quit();
  } else if (opts.test_animation && done <= Phase::Reloading &&
             app.animated_instance != app.host->instanceCount()) {
    app.animated_instance = app.host->instanceCount();
    add_animation_steps();
    enter(Phase::Steps);
  } else if (done <= Phase::Reloading && opts.dev &&
             app.reloads_done <
                 (opts.test_reload ? opts.reloads : 0) + int(opts.expect_reload)) {
    auto &mm = app.host->mountingManager();
    app.views_before = mm.mountedViewCount();
    app.mounts_before = mm.mountCount();
    app.instances_before = app.host->instanceCount();
    // The host's own reload first, then one from outside.
    // The host's own reloads first, then one from outside.
    app.reload_external = !opts.test_reload || app.reloads_done >= opts.reloads;
    printf("reloading %s (views %zu, JS instances %d)\n",
           app.reload_external ? "from outside" : "via the dev menu action",
           app.views_before, app.instances_before);
    if (!app.reload_external) app.host->reload();
    enter(Phase::Reloading);
  } else if (done == Phase::Initial && is_gallery() && app.steps.empty()) {
    add_gallery_input_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_lists() && app.steps.empty()) {
    add_lists_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_controls() && app.steps.empty()) {
    add_controls_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_platform() && app.steps.empty()) {
    add_platform_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_accessibility() && app.steps.empty()) {
    add_accessibility_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_gestures() && app.steps.empty()) {
    add_gestures_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_navigation() && app.steps.empty()) {
    add_navigation_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_native_module() && app.steps.empty()) {
    add_native_module_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_notifications() && app.steps.empty()) {
    add_notifications_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_dragdrop() && app.steps.empty()) {
    add_dragdrop_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_windows() && app.steps.empty()) {
    add_windows_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_titlebar() && app.steps.empty()) {
    add_titlebar_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_menus() && app.steps.empty()) {
    add_menus_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_dialogs() && app.steps.empty()) {
    add_dialogs_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_modal() && app.steps.empty()) {
    add_modal_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_mouse() && app.steps.empty()) {
    add_mouse_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_keyboard() && app.steps.empty()) {
    add_keyboard_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_selection() && app.steps.empty()) {
    add_selection_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_appearance() && app.steps.empty()) {
    add_appearance_steps();
    enter(Phase::Steps);
  } else if (done == Phase::Initial && is_images() && app.steps.empty()) {
    add_images_steps();
    enter(Phase::Steps);
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
  if (opts.self_test) {
    if (is_gallery()) {
      verify_gallery(tex);
    } else if (is_lists()) {
      check(app.host->jsErrorCount() == 0, "no JS errors");
    } else if (is_images()) {
      verify_images(tex);
    } else if (is_controls() || is_appearance() || is_selection() || is_keyboard() ||
               is_mouse() || is_accessibility() || is_platform() || is_modal() ||
               is_dialogs() || is_menus() || is_windows() || is_titlebar() ||
               is_dragdrop() ||
               is_notifications() || is_native_module() || is_navigation() || is_gestures()) {
      check(app.host->jsErrorCount() == 0, "no JS errors");
    } else {
      verify_hello_world(tex);
    }
  }
  g_clear_object(&tex);
}

// The self-test runs after each frame is painted: widgets are allocated
// then (animated scrollbars queue allocations every frame, so a tick
// callback could see a half-laid-out tree).
void on_after_paint(GdkFrameClock *, gpointer);

gint64 last_paint_us = 0;

gboolean on_tick(GtkWidget *, GdkFrameClock *clock, gpointer) {
  // Keeps frames coming; the work happens in on_after_paint.
  static bool connected = false;
  if (!connected) {
    connected = true;
    g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), nullptr);
    // Steps go on while the main window paints nothing (GalleryWindows
    // hides it).
    g_timeout_add(50, [](gpointer) -> gboolean {
      if (app.phase == Phase::Done) return G_SOURCE_REMOVE;
      if (app.phase == Phase::Steps && g_get_monotonic_time() - last_paint_us > 200000) {
        on_after_paint(nullptr, nullptr);
      }
      return G_SOURCE_CONTINUE;
    }, nullptr);
  }
  return app.phase == Phase::Done ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

void on_after_paint(GdkFrameClock *, gpointer) {
  last_paint_us = g_get_monotonic_time();
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
      {
        // Each reload replaces the JS thread (and Hermes' helpers): the
        // process's thread count must not grow from one reload to the next.
        int threads = thread_count();
        printf("threads after reload %d: %d\n", app.reloads_done, threads);
        if (app.reloads_done == 1) {
          app.threads_after_first_reload = threads;
        } else {
          check(threads <= app.threads_after_first_reload,
                "no threads leaked across reloads");
        }
      }
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
      if (opts.dismiss_logbox) {
        // LogBox's own Dismiss button, clicked through LogBox's surface
        // input handler.
        app.host->logBoxPointerHandler()->setRealInputEnabled(false);
        app.steps.push_back(Step{
            "LogBox Dismiss button closes LogBox",
            [] {
              GtkWidget *root = app.host->logBoxRoot();
              GtkWidget *dismiss = find_text(root, "Dismiss");
              if (!dismiss) return;
              graphene_rect_t b{};
              if (!gtk_widget_compute_bounds(dismiss, root, &b)) return;
              rngtk::GtkPointerHandler::Input input{};
              input.x = b.origin.x + b.size.width / 2;
              input.y = b.origin.y + b.size.height / 2;
              input.phase = rngtk::GtkPointerHandler::Phase::Down;
              app.host->logBoxPointerHandler()->dispatch(input);
              input.phase = rngtk::GtkPointerHandler::Phase::Up;
              app.host->logBoxPointerHandler()->dispatch(input);
            },
            [] { return !app.host->isLogBoxShowing(); }});
        enter(Phase::Steps);
        break;
      }
      next_check(Phase::ExpectLogBox);
      break;
    case Phase::Steps: {
      if (app.step >= app.steps.size()) {
        next_check(app.steps_after);
        break;
      }
      Step &step = app.steps[app.step];
      if (!app.step_started) {
        static gint64 waiting_since = 0;
        if (opts.step_delay_ms > 0) {
          gint64 now = g_get_monotonic_time();
          if (!waiting_since) waiting_since = now;
          if (now - waiting_since < gint64(opts.step_delay_ms) * 1000) break;
          waiting_since = 0;
          printf("STEP %s\n", step.name.c_str());
        }
        app.step_started = true;
        restart_timeout();
        step.start();
        break;
      }
      if (!app.host->isIdle() || !step.done()) break;
      check(true, step.name.c_str());
      app.step++;
      app.step_started = false;
      break;
    }
    case Phase::Done:
      break;
  }
}

gboolean on_timeout(gpointer) {
  const char *what = app.phase == Phase::Reloading      ? "the reload"
                     : app.phase == Phase::ExpectText   ? "the expected text"
                     : app.phase == Phase::ExpectLogBox ? "LogBox"
                     : app.phase == Phase::Steps && app.step < app.steps.size()
                         ? app.steps[app.step].name.c_str()
                         : "the first mount";
  fprintf(stderr, "FAIL timed out after %d ms waiting for %s\n",
          opts.timeout_ms, what);
  if (app.phase == Phase::Steps) {
    // What the page shows, to see why.
    std::function<void(GtkWidget *)> dump = [&](GtkWidget *w) {
      if (RN_IS_TEXT(w)) fprintf(stderr, "  text: %s\n", rn_text_get_text(RN_TEXT(w)));
      for (GtkWidget *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) {
        dump(c);
      }
    };
    dump(app.root);
  }
  if (app.host->devUI() && !app.host->devUI()->bannerText().empty()) {
    fprintf(stderr, "dev banner: %s\n", app.host->devUI()->bannerText().c_str());
  }
  app.exit_code = 1;
  app.timeout_id = 0;
  g_application_quit(g_application_get_default());
  return G_SOURCE_REMOVE;
}

void activate(GtkApplication *gtk_app, gpointer) {
  GtkWidget *window = gtk_application_window_new(gtk_app);
  gtk_window_set_title(GTK_WINDOW(window), opts.module.c_str());
  // Interactive runs resize with the window. Self-tests keep the size they
  // were given, even when the window manager makes the window bigger
  // (mutter maximizes near-screen-size windows); a step can turn resizing
  // on (see add_resize_steps).
  gtk_window_set_resizable(GTK_WINDOW(window), !opts.self_test);
  {
    std::string bar = opts.title_bar.empty() && is_titlebar() ? "hidden" : opts.title_bar;
    rngtk::apply_window_style(GTK_WINDOW(window),
                              bar == "hidden" ? rngtk::TitleBar::Hidden
                              : bar == "none" ? rngtk::TitleBar::None
                                              : rngtk::TitleBar::Default,
                              opts.transparent);
  }
  app.window = window;
  app.overlay = gtk_overlay_new();
  app.root = rn_view_new();
  gtk_widget_set_halign(app.root, GTK_ALIGN_START);
  gtk_widget_set_valign(app.root, GTK_ALIGN_START);
  gtk_overlay_set_child(GTK_OVERLAY(app.overlay), app.root);
  gtk_window_set_child(GTK_WINDOW(window), app.overlay);

  rngtk::RNGtkHostOptions host_options{
      .isTesting = opts.self_test,
      .devMode = opts.dev,
      .devServerHost = opts.dev_host,
      .devServerPort = opts.dev_port,
      .inspector = opts.inspector,
      .followsWindowSize = !opts.self_test,
      .followSystemAppearance = !opts.self_test || opts.system_appearance,
      .followSystemAccessibility = !opts.self_test || opts.system_accessibility,
      .extraTurboModules = {[](const std::string &name,
                               const std::shared_ptr<facebook::react::CallInvoker>
                                   &jsInvoker)
                                -> std::shared_ptr<facebook::react::TurboModule> {
        if (name != "ShowcaseDesktop") return nullptr;
        return std::make_shared<ShowcaseDesktopModule>(jsInvoker);
      }},
      .initialURL = opts.url,
  };
  // The library template's package (template-library/linux), as an app's
  // autolinked libraries are.
  host_options.packages = {example_package(), animated_probe::package(),
                           rngtk_gesture_handler_package()};
#ifdef RNGTK_HARNESS_SCREENS
  host_options.packages.push_back(rngtk_screens_package());
#endif
  app.host = new rngtk::RNGtkHost(host_options, GTK_OVERLAY(app.overlay));
  if (opts.dev) rngtk::addDevControls(window, app.host, opts.verbose);
  folly::dynamic props = folly::dynamic::object();
  if (!opts.initial_props.empty()) {
    try {
      props = folly::parseJson(opts.initial_props);
    } catch (const std::exception &e) {
      fprintf(stderr, "--initial-props: %s\n", e.what());
    }
  }
  // GalleryImages' self-test serves its http images itself.
  if (opts.self_test && is_images()) props["imageServer"] = start_image_server();
  // GalleryDialogs' file dialogs open in a folder of the self-test's own.
  if (opts.self_test && is_dialogs()) props["folder"] = make_dialogs_folder();
  if (opts.self_test && is_dragdrop()) props["image"] = make_dnd_folder();
  if (!app.host->run(opts.dev ? opts.entry : opts.bundle, kSurfaceId,
                     opts.module, app.root, opts.width, opts.height,
                     std::move(props))) {
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

int usage() {
  fprintf(stderr,
          "usage: rn-gtk-host --bundle FILE [options]\n"
          "       rn-gtk-host --dev-server [HOST:PORT] [--entry index]\n"
          "                   [--no-inspector] [options]\n"
          "options: [--module NAME] [--initial-props JSON] [--width N]\n"
          "         [--height N] [--self-test]\n"
          "         [--screenshot PNG] [--timeout MS] [--test-reload]\n"
          "         [--expect-reload]\n"
          "         [--expect-text TEXT] [--expect-logbox]\n"
          "         [--logbox-screenshot PNG] [--dismiss-logbox]\n"
          "         [--test-animation] [--system-appearance]\n"
          "         [--system-accessibility] [--url URL] [--rtl]\n"
          "         [--title-bar default|hidden|none] [--transparent]\n"
          "         [--step-delay MS] [--verbose]\n");
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
    else if (arg("--step-delay")) opts.step_delay_ms = atoi(argv[++i]);
    else if (arg("--entry")) opts.entry = argv[++i];
    else if (arg("--initial-props")) opts.initial_props = argv[++i];
    else if (arg("--expect-text")) opts.expect_text = argv[++i];
    else if (arg("--logbox-screenshot")) opts.logbox_screenshot = argv[++i];
    else if (!strcmp(argv[i], "--self-test")) opts.self_test = true;
    else if (!strcmp(argv[i], "--test-reload")) opts.test_reload = true;
    else if (arg("--reloads")) opts.reloads = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--expect-reload")) opts.expect_reload = true;
    else if (!strcmp(argv[i], "--expect-logbox")) opts.expect_logbox = true;
    else if (!strcmp(argv[i], "--dismiss-logbox")) opts.dismiss_logbox = true;
    else if (!strcmp(argv[i], "--test-animation")) opts.test_animation = true;
    else if (!strcmp(argv[i], "--system-appearance")) opts.system_appearance = true;
    else if (!strcmp(argv[i], "--system-accessibility")) opts.system_accessibility = true;
    else if (arg("--url")) opts.url = argv[++i];
    else if (!strcmp(argv[i], "--rtl")) opts.rtl = true;
    else if (arg("--title-bar")) opts.title_bar = argv[++i];
    else if (!strcmp(argv[i], "--transparent")) opts.transparent = true;
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
  rngtk::setUpFeatureFlags();
  if (opts.self_test && is_platform()) set_up_platform_xdg();
  // GalleryDialogs answers GTK's own file chooser; the desktop's (the
  // FileChooser portal, another process) can't be driven from here.
  // (GDK_DEBUG=portals shows the portal's, to watch with --step-delay.)
  if (opts.self_test && is_dialogs()) g_setenv("GDK_DEBUG", "no-portals", FALSE);

  if (opts.self_test && is_notifications()) {
    // A private bus has no desktop portal or accessibility bus to talk to
    // (asking for them waits on timeouts).
    g_setenv("GDK_DEBUG", "no-portals", FALSE);
    g_setenv("GTK_A11Y", "none", FALSE);
    start_fake_notification_server();
  }
  GtkApplication *gtk_app = gtk_application_new(
      "dev.curiosity26.RNGtk4.Host", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), nullptr);
  int status = g_application_run(G_APPLICATION(gtk_app), 1, argv);
  restore_desktop_color_scheme();
  if (restore_screen_reader) set_bus_screen_reader(false);
  delete app.host;
  if (!xdg_dir.empty()) remove_tree(xdg_dir);
  if (!dialogs_dir.empty()) remove_tree(dialogs_dir);
  if (!dnd_dir.empty()) remove_tree(dnd_dir);
  g_object_unref(gtk_app);
  return status ? status : app.exit_code;
}
