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
//   --dismiss-logbox       then click LogBox's Dismiss button and wait for
//                          LogBox to close
#include <glog/logging.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDynamicProvider.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "DevUI.h"
#include "GtkMountingManager.h"
#include "GtkPointerHandler.h"
#include "PangoText.h"
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
  bool dismiss_logbox = false;
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
  Phase phase = Phase::Initial;
  int frames = 0;
  int mounts_before = 0;
  size_t views_before = 0;
  int instances_before = 0;
  bool reload_external = false;
  int reloads_done = 0;
  guint timeout_id = 0;
  int exit_code = 0;
  std::vector<Step> steps;
  size_t step = 0;
  bool step_started = false;
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
  } else if (done == Phase::Initial && is_gallery() && app.steps.empty()) {
    add_gallery_input_steps();
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
    } else {
      verify_hello_world(tex);
    }
  }
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
        quit();
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
      return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
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
  // The surface has a fixed size: keep the root at it even when the window
  // manager makes the window bigger (mutter maximizes near-screen-size
  // windows).
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
  // Pressable's onHoverIn/onHoverOut from W3C pointerenter/pointerleave,
  // which GtkPointerHandler sends for the mouse.
  flags["shouldPressibilityUseW3CPointerEventsForHover"] = true;
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
          "         [--logbox-screenshot PNG] [--dismiss-logbox] [--verbose]\n");
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
    else if (!strcmp(argv[i], "--dismiss-logbox")) opts.dismiss_logbox = true;
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
