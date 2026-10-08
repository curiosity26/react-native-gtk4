// Phase 0 Hello World: an RN-style view tree (root view, rounded card,
// two text nodes) mounted into a GTK4 window with no GTK layout managers.
//
//   hello-world                 open the window
//   hello-world --self-test     render, verify pixels and frames, exit 0/1
//   hello-world --screenshot F  also save a PNG of the window content
#include <cstdio>
#include <cstring>

#include "harness.h"
#include "rn_text.h"
#include "rn_view.h"

namespace {

constexpr int kWidth = 640, kHeight = 400;
constexpr GdkRGBA kRootBg{0.961f, 0.961f, 0.969f, 1};    // #F5F5F7
constexpr GdkRGBA kCardBg{1, 1, 1, 1};
constexpr GdkRGBA kCardBorder{0.000f, 0.478f, 1.000f, 1};  // #007AFF
constexpr GdkRGBA kTitle{0.110f, 0.110f, 0.118f, 1};
constexpr GdkRGBA kSubtitle{0.431f, 0.431f, 0.451f, 1};
constexpr float kCardX = 120, kCardY = 100, kCardW = 400, kCardH = 200;

struct Options {
  bool self_test = false;
  const char *screenshot = nullptr;
};
Options opts;
int exit_code = 0;
GtkWidget *title_text = nullptr;

GtkWidget *build_tree() {
  GtkWidget *root = rn_view_new();
  rn_widget_set_frame(root, 0, 0, kWidth, kHeight);
  RNViewStyle root_style{};
  root_style.background = kRootBg;
  rn_view_set_style(RN_VIEW(root), &root_style);

  GtkWidget *card = rn_view_new();
  rn_widget_set_frame(card, kCardX, kCardY, kCardW, kCardH);
  RNViewStyle card_style{};
  card_style.background = kCardBg;
  card_style.border_radius = 16;
  card_style.border_width = 2;
  card_style.border_color = kCardBorder;
  card_style.clip_children = TRUE;
  rn_view_set_style(RN_VIEW(card), &card_style);
  rn_view_insert_child(RN_VIEW(root), card, -1);

  // Frames come from the same measure call Yoga will use, then get centered.
  title_text = rn_text_new("Hello, World!");
  rn_text_set_font(RN_TEXT(title_text), "Sans", 36, PANGO_WEIGHT_BOLD);
  rn_text_set_color(RN_TEXT(title_text), &kTitle);
  graphene_size_t ts =
      rn_text_measure("Hello, World!", "Sans", 36, PANGO_WEIGHT_BOLD, kCardW);
  rn_widget_set_frame(title_text, (kCardW - ts.width) / 2, 56, ts.width,
                      ts.height);
  rn_view_insert_child(RN_VIEW(card), title_text, -1);

  const char *sub = "React Native on GTK4";
  GtkWidget *subtitle = rn_text_new(sub);
  rn_text_set_font(RN_TEXT(subtitle), "Sans", 16, PANGO_WEIGHT_NORMAL);
  rn_text_set_color(RN_TEXT(subtitle), &kSubtitle);
  graphene_size_t ss =
      rn_text_measure(sub, "Sans", 16, PANGO_WEIGHT_NORMAL, kCardW);
  rn_widget_set_frame(subtitle, (kCardW - ss.width) / 2, 56 + ts.height + 8,
                      ss.width, ss.height);
  rn_view_insert_child(RN_VIEW(card), subtitle, -1);
  return root;
}

bool check(bool ok, const char *what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) exit_code = 1;
  return ok;
}

rngtk::Rgba8 to8(GdkRGBA c) {
  auto b = [](float f) { return static_cast<uint8_t>(f * 255 + 0.5f); };
  return {b(c.red), b(c.green), b(c.blue), b(c.alpha)};
}

void after_first_frame(GtkWidget *root) {
  GdkTexture *tex = rngtk::render_widget(root);
  printf("backend=%s renderer=%s\n", rngtk::backend_name(root).c_str(),
         rngtk::renderer_name(root).c_str());
  if (opts.screenshot) {
    check(rngtk::save_png(tex, opts.screenshot), "screenshot saved");
  }
  if (opts.self_test) {
    check(tex && gdk_texture_get_width(tex) == kWidth &&
              gdk_texture_get_height(tex) == kHeight,
          "root rendered at 640x400");
    if (tex) {
      check(rngtk::near(rngtk::texture_pixel(tex, 10, 10), to8(kRootBg)),
            "root background color");
      check(rngtk::near(rngtk::texture_pixel(tex, kCardX + 30, kCardY + 30),
                        to8(kCardBg)),
            "card background color");
      check(rngtk::near(rngtk::texture_pixel(tex, kCardX + kCardW / 2,
                                             kCardY + 1),
                        to8(kCardBorder), 40),
            "card border drawn");
      // Rounded corner: the very corner pixel shows the root, not the card.
      check(rngtk::near(rngtk::texture_pixel(tex, kCardX + 1, kCardY + 1),
                        to8(kRootBg), 12),
            "card corner is rounded");
      // Some pixel in the title's frame must be dark text.
      graphene_rect_t f = rn_widget_get_frame(title_text);
      bool dark = false;
      for (int y = 0; y < (int)f.size.height && !dark; y++) {
        for (int x = 0; x < (int)f.size.width && !dark; x++) {
          auto p = rngtk::texture_pixel(tex, kCardX + f.origin.x + x,
                                        kCardY + f.origin.y + y);
          dark = p.r < 80 && p.g < 80 && p.b < 80;
        }
      }
      check(dark, "title text drawn inside its frame");
    }
    graphene_rect_t f = rn_widget_get_frame(title_text);
    int min_w, nat_w;
    gtk_widget_measure(title_text, GTK_ORIENTATION_HORIZONTAL, -1, &min_w,
                       &nat_w, nullptr, nullptr);
    printf("title: thread measure %.0fpx, widget measure %dpx\n",
           f.size.width, nat_w);
    check(nat_w == (int)f.size.width,
          "thread-safe measure matches the widget's own Pango layout");
    graphene_rect_t bounds;
    check(gtk_widget_compute_bounds(title_text, gtk_widget_get_parent(title_text),
                                    &bounds) &&
              graphene_rect_equal(&bounds, &f),
          "title allocated exactly at its Yoga frame");
  }
  g_clear_object(&tex);
}

gboolean on_tick(GtkWidget *root, GdkFrameClock *, gpointer data) {
  // Wait for two frames so the window has actually painted once.
  int *frames = static_cast<int *>(data);
  if (++*frames < 2) return G_SOURCE_CONTINUE;
  after_first_frame(root);
  if (opts.self_test) {
    g_application_quit(g_application_get_default());
  }
  return G_SOURCE_REMOVE;
}

void activate(GtkApplication *app, gpointer) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Hello, React Native GTK4");
  // No default size: a non-resizable window takes the root view's frame as
  // its content size, so client-side title bars do not eat into it.
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  GtkWidget *root = build_tree();
  gtk_window_set_child(GTK_WINDOW(window), root);
  if (opts.self_test || opts.screenshot) {
    gtk_widget_add_tick_callback(root, on_tick, g_new0(int, 1), g_free);
  }
  gtk_window_present(GTK_WINDOW(window));
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--self-test")) opts.self_test = true;
    else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
      opts.screenshot = argv[++i];
  }
  GtkApplication *app = gtk_application_new("dev.curiosity26.RNGtk4.HelloWorld",
                                            G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
  int status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return status ? status : exit_code;
}
