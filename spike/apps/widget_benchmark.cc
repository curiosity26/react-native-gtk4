// Phase 0 widget-count benchmark.
//
// Mounts N views (every 10th one a text node) in rows of RN views, the way a
// React Native mount transaction would, then animates every frame of every
// view for F frames and unmounts. Prints one JSON object.
//
//   widget-benchmark --count 10000 --frames 120 [--mode rnview|fixed]
//                    [--update 0.01] [--json out.json] [--screenshot out.png]
//
// --update sets the fraction of views that move each frame (default 1: all).
//
// --mode fixed builds the same tree from GtkFixed containers as a baseline.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "harness.h"
#include "rn_text.h"
#include "rn_view.h"

namespace {

constexpr int kWidth = 1280, kHeight = 800;

struct Options {
  int count = 1000;
  int frames = 120;
  double update = 1.0;
  bool fixed = false;
  const char *json = nullptr;
  const char *screenshot = nullptr;
} opts;

struct Leaf {
  GtkWidget *widget;
  GtkWidget *row;
  float x, y, size;
};

struct Bench {
  GtkWidget *window = nullptr;
  GtkWidget *root = nullptr;
  std::vector<GtkWidget *> rows;
  std::vector<Leaf> leaves;
  float pitch = 0;

  enum class Phase { WarmUp, Mounted, Animating, Done } phase = Phase::WarmUp;
  int warm_frames = 0;
  int anim_frame = 0;

  double mount_start = 0, mount_ms = 0, mount_to_paint_ms = 0;
  double unmount_ms = 0, text_measure_ms = 0;
  long rss_before = 0, rss_mounted = 0;
  double paint_start = 0, update_end = 0, layout_end = 0, paint_end = 0;
  std::vector<double> frame_work_ms, frame_interval_ms;
  // Per-phase split of frame_work_ms: our mount updates (tick callback),
  // GTK size allocation, snapshot + render, and swap/present.
  std::vector<double> update_ms, layout_ms, paint_ms, present_ms;
  double last_after_paint = 0;
  std::string backend, renderer;
  int exit_code = 0;
} b;

GtkWidget *make_container() {
  if (opts.fixed) return gtk_fixed_new();
  return rn_view_new();
}

void place(GtkWidget *parent, GtkWidget *child, float x, float y, float w,
           float h) {
  if (opts.fixed) {
    gtk_widget_set_size_request(child, (int)w, (int)h);
    gtk_fixed_move(GTK_FIXED(parent), child, x, y);
  } else {
    rn_widget_set_frame(child, x, y, w, h);
  }
}

void mount() {
  b.rss_before = rngtk::rss_kb();
  b.mount_start = rngtk::now_ms();

  int cols = std::max(1, (int)std::floor(std::sqrt(
                             (double)opts.count * kWidth / kHeight)));
  b.pitch = (float)kWidth / cols;
  float size = std::max(2.0f, b.pitch - 2);
  int nrows = (opts.count + cols - 1) / cols;

  char label[16];
  for (int r = 0; r < nrows; r++) {
    GtkWidget *row = make_container();
    if (opts.fixed) {
      gtk_fixed_put(GTK_FIXED(b.root), row, 0, r * b.pitch);
      gtk_widget_set_size_request(row, kWidth, (int)b.pitch);
    } else {
      rn_view_insert_child(RN_VIEW(b.root), row, -1);
      rn_widget_set_frame(row, 0, r * b.pitch, kWidth, b.pitch);
    }
    b.rows.push_back(row);
    for (int c = 0; c < cols && r * cols + c < opts.count; c++) {
      int i = r * cols + c;
      GtkWidget *leaf;
      if (i % 10 == 9) {
        snprintf(label, sizeof label, "%d", i % 1000);
        leaf = rn_text_new(label);
        rn_text_set_font(RN_TEXT(leaf), "Sans", std::max(4.0f, size * 0.5f),
                         PANGO_WEIGHT_NORMAL);
      } else {
        leaf = rn_view_new();
        RNViewStyle s{};
        float hue = (float)i / opts.count;
        s.background = GdkRGBA{0.2f + 0.6f * hue, 0.5f, 0.8f - 0.6f * hue, 1};
        s.border_radius = size * 0.2f;
        rn_view_set_style(RN_VIEW(leaf), &s);
      }
      if (opts.fixed) {
        gtk_fixed_put(GTK_FIXED(row), leaf, c * b.pitch, 0);
        gtk_widget_set_size_request(leaf, (int)size, (int)size);
      } else {
        rn_view_insert_child(RN_VIEW(row), leaf, -1);
        rn_widget_set_frame(leaf, c * b.pitch, 0, size, size);
      }
      b.leaves.push_back({leaf, row, c * b.pitch, 0, size});
    }
  }
  b.mount_ms = rngtk::now_ms() - b.mount_start;
}

void unmount() {
  double t = rngtk::now_ms();
  for (GtkWidget *row : b.rows) {
    if (opts.fixed) gtk_fixed_remove(GTK_FIXED(b.root), row);
    else rn_view_remove_child(RN_VIEW(b.root), row);
  }
  b.rows.clear();
  b.leaves.clear();
  b.unmount_ms = rngtk::now_ms() - t;
}

// What a cxx TextLayoutManager does for N text nodes, off the main thread.
void measure_text_off_thread() {
  int n = opts.count / 10;
  std::thread worker([n] {
    double t = rngtk::now_ms();
    char label[64];
    for (int i = 0; i < n; i++) {
      snprintf(label, sizeof label, "Item %d of a list of text", i);
      rn_text_measure(label, "Sans", 14, PANGO_WEIGHT_NORMAL, 200);
    }
    b.text_measure_ms = rngtk::now_ms() - t;
  });
  worker.join();
}

void report() {
  auto work = rngtk::summarize(b.frame_work_ms);
  auto interval = rngtk::summarize(b.frame_interval_ms);
  auto mean = [](const std::vector<double> &v) {
    return rngtk::summarize(v).mean;
  };
  char buf[2048];
  snprintf(
      buf, sizeof buf,
      "{\"mode\":\"%s\",\"backend\":\"%s\",\"renderer\":\"%s\","
      "\"gtk\":\"%u.%u.%u\",\"count\":%d,\"update\":%g,\"frames\":%d,"
      "\"mount_ms\":%.2f,\"mount_to_paint_ms\":%.2f,\"unmount_ms\":%.2f,"
      "\"text_measure_ms\":%.2f,\"text_measured\":%d,"
      "\"rss_mounted_delta_kb\":%ld,"
      "\"frame_work_ms\":{\"mean\":%.2f,\"p50\":%.2f,\"p95\":%.2f,"
      "\"max\":%.2f},"
      "\"frame_interval_ms\":{\"mean\":%.2f,\"p50\":%.2f,\"p95\":%.2f,"
      "\"max\":%.2f},"
      "\"phase_mean_ms\":{\"update\":%.2f,\"layout\":%.2f,\"paint\":%.2f,"
      "\"present\":%.2f}}\n",
      opts.fixed ? "fixed" : "rnview", b.backend.c_str(), b.renderer.c_str(),
      gtk_get_major_version(), gtk_get_minor_version(),
      gtk_get_micro_version(), opts.count, opts.update,
      (int)b.frame_work_ms.size(),
      b.mount_ms, b.mount_to_paint_ms, b.unmount_ms, b.text_measure_ms,
      opts.count / 10, b.rss_mounted - b.rss_before, work.mean, work.p50,
      work.p95, work.max, interval.mean, interval.p50, interval.p95,
      interval.max, mean(b.update_ms), mean(b.layout_ms), mean(b.paint_ms),
      mean(b.present_ms));
  fputs(buf, stdout);
  if (opts.json) {
    if (FILE *f = fopen(opts.json, "w")) {
      fputs(buf, f);
      fclose(f);
    }
  }
}

void on_before_paint(GdkFrameClock *, gpointer) {
  b.paint_start = rngtk::now_ms();
}

void on_update(GdkFrameClock *, gpointer) { b.update_end = rngtk::now_ms(); }
void on_layout(GdkFrameClock *, gpointer) { b.layout_end = rngtk::now_ms(); }
void on_paint(GdkFrameClock *, gpointer) { b.paint_end = rngtk::now_ms(); }

void on_after_paint(GdkFrameClock *, gpointer) {
  double now = rngtk::now_ms();
  if (b.phase == Bench::Phase::Mounted && b.mount_to_paint_ms == 0) {
    b.mount_to_paint_ms = now - b.mount_start;
    b.rss_mounted = rngtk::rss_kb();
    if (opts.screenshot) {
      GdkTexture *tex = rngtk::render_widget(b.root);
      if (!rngtk::save_png(tex, opts.screenshot)) b.exit_code = 1;
      g_clear_object(&tex);
    }
    b.phase = Bench::Phase::Animating;
  } else if (b.phase == Bench::Phase::Animating) {
    b.frame_work_ms.push_back(now - b.paint_start);
    b.update_ms.push_back(b.update_end - b.paint_start);
    b.layout_ms.push_back(b.layout_end - b.update_end);
    b.paint_ms.push_back(b.paint_end - b.layout_end);
    b.present_ms.push_back(now - b.paint_end);
    if (b.last_after_paint > 0) {
      b.frame_interval_ms.push_back(now - b.last_after_paint);
    }
  }
  b.last_after_paint = now;
}

gboolean on_tick(GtkWidget *, GdkFrameClock *, gpointer) {
  switch (b.phase) {
    case Bench::Phase::WarmUp:
      if (++b.warm_frames >= 3) {
        b.backend = rngtk::backend_name(b.root);
        b.renderer = rngtk::renderer_name(b.root);
        measure_text_off_thread();
        mount();
        b.phase = Bench::Phase::Mounted;
      }
      return G_SOURCE_CONTINUE;
    case Bench::Phase::Mounted:
      return G_SOURCE_CONTINUE;
    case Bench::Phase::Animating:
      if (b.anim_frame++ < opts.frames) {
        // Every stride-th view gets a new frame; --update 1 moves all of them,
        // the worst case for one transaction.
        float dx = std::sin(b.anim_frame * 0.2f) * b.pitch * 0.25f;
        size_t stride = std::max<size_t>(1, std::lround(1.0 / opts.update));
        for (size_t i = 0; i < b.leaves.size(); i += stride) {
          const Leaf &l = b.leaves[i];
          place(l.row, l.widget, l.x + dx, l.y, l.size, l.size);
        }
        return G_SOURCE_CONTINUE;
      }
      b.phase = Bench::Phase::Done;
      unmount();
      report();
      g_application_quit(g_application_get_default());
      return G_SOURCE_REMOVE;
    case Bench::Phase::Done:
      return G_SOURCE_REMOVE;
  }
  return G_SOURCE_REMOVE;
}

void on_map(GtkWidget *widget, gpointer) {
  GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
  g_signal_connect(clock, "before-paint", G_CALLBACK(on_before_paint), nullptr);
  // Connected after GTK's own handlers, so each marks the end of its phase.
  g_signal_connect(clock, "update", G_CALLBACK(on_update), nullptr);
  g_signal_connect(clock, "layout", G_CALLBACK(on_layout), nullptr);
  g_signal_connect(clock, "paint", G_CALLBACK(on_paint), nullptr);
  g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), nullptr);
}

void activate(GtkApplication *app, gpointer) {
  b.window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(b.window), "RN GTK4 widget benchmark");
  // Content size comes from the root frame (see hello_world.cc).
  gtk_window_set_resizable(GTK_WINDOW(b.window), FALSE);
  b.root = make_container();
  if (!opts.fixed) rn_widget_set_frame(b.root, 0, 0, kWidth, kHeight);
  else gtk_widget_set_size_request(b.root, kWidth, kHeight);
  gtk_window_set_child(GTK_WINDOW(b.window), b.root);
  g_signal_connect(b.root, "map", G_CALLBACK(on_map), nullptr);
  gtk_widget_add_tick_callback(b.root, on_tick, nullptr, nullptr);
  gtk_window_present(GTK_WINDOW(b.window));
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : nullptr;
    if (!strcmp(a, "--count") && v) opts.count = atoi(argv[++i]);
    else if (!strcmp(a, "--frames") && v) opts.frames = atoi(argv[++i]);
    else if (!strcmp(a, "--mode") && v) opts.fixed = !strcmp(argv[++i], "fixed");
    else if (!strcmp(a, "--update") && v) opts.update = atof(argv[++i]);
    else if (!strcmp(a, "--json") && v) opts.json = argv[++i];
    else if (!strcmp(a, "--screenshot") && v) opts.screenshot = argv[++i];
  }
  GtkApplication *app = gtk_application_new(
      "dev.curiosity26.RNGtk4.WidgetBenchmark", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
  int status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return status ? status : b.exit_code;
}
