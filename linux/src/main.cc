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
//   --test-animation       before the first reload and after each one,
//                          hold the card (a TouchableOpacity) and check
//                          that its native-driver fade runs, then release
#include <ReactCommon/TurboModule.h>
#include <glog/logging.h>
#include <folly/json.h>
#include <libsoup/soup.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <functional>
#include <string>
#include <vector>

#include "Appearance.h"
#include "DevControls.h"
#include "DevUI.h"
#include "FeatureFlags.h"
#include "GtkMountingManager.h"
#include "GtkPointerHandler.h"
#include "PangoText.h"
#include "rn_scroll_view.h"
#include "rn_text_input.h"
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
  int timeout_ms = 20000;
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

GtkWidget *by_id(const char *id) {
  return app.host->mountingManager().viewForNativeId(id);
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
    } else if (is_controls() || is_appearance()) {
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

gboolean on_tick(GtkWidget *, GdkFrameClock *clock, gpointer) {
  // Keeps frames coming; the work happens in on_after_paint.
  static bool connected = false;
  if (!connected) {
    connected = true;
    g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), nullptr);
  }
  return app.phase == Phase::Done ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

void on_after_paint(GdkFrameClock *, gpointer) {
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
      .extraTurboModules = {[](const std::string &name,
                               const std::shared_ptr<facebook::react::CallInvoker>
                                   &jsInvoker)
                                -> std::shared_ptr<facebook::react::TurboModule> {
        if (name != "ShowcaseDesktop") return nullptr;
        return std::make_shared<ShowcaseDesktopModule>(jsInvoker);
      }},
  };
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
          "         [--test-animation] [--system-appearance] [--verbose]\n");
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

  GtkApplication *gtk_app = gtk_application_new(
      "dev.curiosity26.RNGtk4.Host", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), nullptr);
  int status = g_application_run(G_APPLICATION(gtk_app), 1, argv);
  restore_desktop_color_scheme();
  delete app.host;
  g_object_unref(gtk_app);
  return status ? status : app.exit_code;
}
