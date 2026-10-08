#include "rn_view.h"

#include "rn_text.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Gradient {
  RNGradient spec;
  std::vector<RNColorStop> stops;
};

// The rarely used styles live outside the instance struct, so plain views
// (the benchmark's 10k) stay small.
struct Extras {
  std::vector<RNBoxShadow> shadows;
  std::vector<RNFilter> filters;
  std::vector<Gradient> gradients;
  GdkTexture *image = nullptr;
  float image_scale = 1;
  RNImageFit image_fit = RN_IMAGE_COVER;
  bool image_tinted = false;
  GdkRGBA image_tint{};
  float image_blur = 0;
  ~Extras() { g_clear_object(&image); }
};

}  // namespace

struct _RNView {
  GtkWidget parent_instance;
  RNViewStyle style;
  Extras *extras;  // nullptr until a shadow, filter or gradient is set
};

G_DEFINE_FINAL_TYPE(RNView, rn_view, GTK_TYPE_WIDGET)

static GQuark frame_quark() {
  static GQuark q = g_quark_from_static_string("rn-frame");
  return q;
}

static GQuark transform_quark() {
  static GQuark q = g_quark_from_static_string("rn-transform");
  return q;
}

static GQuark pointer_events_quark() {
  static GQuark q = g_quark_from_static_string("rn-pointer-events");
  return q;
}

void rn_widget_set_frame(GtkWidget *widget, float x, float y, float width,
                         float height) {
  auto *frame = static_cast<graphene_rect_t *>(
      g_object_get_qdata(G_OBJECT(widget), frame_quark()));
  if (!frame) {
    frame = g_new0(graphene_rect_t, 1);
    g_object_set_qdata_full(G_OBJECT(widget), frame_quark(), frame, g_free);
  }
  if (frame->origin.x == x && frame->origin.y == y &&
      frame->size.width == width && frame->size.height == height) {
    return;
  }
  graphene_rect_init(frame, x, y, width, height);
  // Only the parent's allocation changes; the child is re-allocated from it.
  if (GtkWidget *parent = gtk_widget_get_parent(widget)) {
    gtk_widget_queue_allocate(parent);
  } else {
    gtk_widget_queue_resize(widget);
  }
}

graphene_rect_t rn_widget_get_frame(GtkWidget *widget) {
  auto *frame = static_cast<graphene_rect_t *>(
      g_object_get_qdata(G_OBJECT(widget), frame_quark()));
  return frame ? *frame : GRAPHENE_RECT_INIT(0, 0, 0, 0);
}

static const graphene_matrix_t *widget_transform(GtkWidget *widget) {
  return static_cast<const graphene_matrix_t *>(
      g_object_get_qdata(G_OBJECT(widget), transform_quark()));
}

void rn_widget_set_transform(GtkWidget *widget,
                             const graphene_matrix_t *matrix) {
  const graphene_matrix_t *old = widget_transform(widget);
  bool identity = !matrix || graphene_matrix_is_identity(matrix);
  if (identity && !old) return;
  if (!identity && old && graphene_matrix_equal_fast(old, matrix)) return;
  if (identity) {
    g_object_set_qdata(G_OBJECT(widget), transform_quark(), nullptr);
  } else {
    auto *copy = g_new(graphene_matrix_t, 1);
    *copy = *matrix;
    g_object_set_qdata_full(G_OBJECT(widget), transform_quark(), copy, g_free);
  }
  if (GtkWidget *parent = gtk_widget_get_parent(widget)) {
    gtk_widget_queue_allocate(parent);
  }
}

void rn_widget_set_pointer_events(GtkWidget *widget, RNPointerEvents mode) {
  g_object_set_qdata(G_OBJECT(widget), pointer_events_quark(),
                     GINT_TO_POINTER(mode));
  // GTK's own picking (cursors) skips the subtree too.
  gtk_widget_set_can_target(widget, mode != RN_POINTER_EVENTS_NONE);
}

RNPointerEvents rn_widget_get_pointer_events(GtkWidget *widget) {
  return static_cast<RNPointerEvents>(GPOINTER_TO_INT(
      g_object_get_qdata(G_OBJECT(widget), pointer_events_quark())));
}

namespace {

GskRoundedRect outline_of(const RNViewStyle &s, float w, float h) {
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);
  GskRoundedRect r;
  gsk_rounded_rect_init(&r, &bounds, &s.radii[0], &s.radii[1], &s.radii[2],
                        &s.radii[3]);
  // Scales radii that don't fit, as CSS does.
  gsk_rounded_rect_normalize(&r);
  return r;
}

bool has_radius(const RNViewStyle &s) {
  for (const auto &r : s.radii) {
    if (r.width > 0 && r.height > 0) return true;
  }
  return false;
}

// The view's transform turns its back to the viewer (rotateY(180deg)...).
bool backface_visible(GtkWidget *widget) {
  const graphene_matrix_t *m = widget_transform(widget);
  return !m || graphene_matrix_get_value(m, 2, 2) >= 0;
}

void push_clip(GtkSnapshot *snapshot, const GskRoundedRect &r, bool rounded) {
  if (rounded) {
    gtk_snapshot_push_rounded_clip(snapshot, &r);
  } else {
    gtk_snapshot_push_clip(snapshot, &r.bounds);
  }
}

// A dashed or dotted line along the rounded rect `r`, centred on it.
void stroke_line(GtkSnapshot *snapshot, const GskRoundedRect &r, float width,
                 const GdkRGBA &color, RNLineStyle style) {
  GskPathBuilder *builder = gsk_path_builder_new();
  gsk_path_builder_add_rounded_rect(builder, &r);
  GskPath *path = gsk_path_builder_free_to_path(builder);
  GskStroke *stroke = gsk_stroke_new(width);
  if (style == RN_LINE_DOTTED) {
    const float dash[2] = {0, 2 * width};
    gsk_stroke_set_dash(stroke, dash, 2);
    gsk_stroke_set_line_cap(stroke, GSK_LINE_CAP_ROUND);
  } else if (style == RN_LINE_DASHED) {
    const float dash[2] = {3 * width, 3 * width};
    gsk_stroke_set_dash(stroke, dash, 2);
  }
  gtk_snapshot_append_stroke(snapshot, path, stroke, &color);
  gsk_stroke_free(stroke);
  gsk_path_unref(path);
}

void append_border(GtkSnapshot *snapshot, const RNViewStyle &s,
                   const GskRoundedRect &outline) {
  bool any = false;
  for (int i = 0; i < 4; i++) {
    any |= s.border_widths[i] > 0 && s.border_colors[i].alpha > 0;
  }
  if (!any) return;
  if (s.border_style == RN_LINE_SOLID) {
    gtk_snapshot_append_border(snapshot, &outline, s.border_widths,
                               s.border_colors);
    return;
  }
  // Dashed and dotted borders: one stroke with the widest side's width and
  // the top color (per-side styles are rare and not drawn yet).
  float w = *std::max_element(s.border_widths, s.border_widths + 4);
  GskRoundedRect line = outline;
  gsk_rounded_rect_shrink(&line, w / 2, w / 2, w / 2, w / 2);
  stroke_line(snapshot, line, w, s.border_colors[0], s.border_style);
}

void append_outline(GtkSnapshot *snapshot, const RNViewStyle &s,
                    const GskRoundedRect &outline) {
  float w = s.outline_width;
  if (w <= 0 || s.outline_color.alpha <= 0) return;
  GskRoundedRect outer = outline;
  float grow = s.outline_offset + w;
  gsk_rounded_rect_shrink(&outer, -grow, -grow, -grow, -grow);
  if (s.outline_style == RN_LINE_SOLID) {
    const float widths[4] = {w, w, w, w};
    const GdkRGBA colors[4] = {s.outline_color, s.outline_color,
                               s.outline_color, s.outline_color};
    gtk_snapshot_append_border(snapshot, &outer, widths, colors);
  } else {
    GskRoundedRect line = outer;
    gsk_rounded_rect_shrink(&line, w / 2, w / 2, w / 2, w / 2);
    stroke_line(snapshot, line, w, s.outline_color, s.outline_style);
  }
}

// CSS gradient stop positions resolved to 0..1 along a line of `length` px.
std::vector<GskColorStop> resolve_stops(const std::vector<RNColorStop> &stops,
                                        float length) {
  size_t n = stops.size();
  std::vector<float> pos(n, NAN);
  for (size_t i = 0; i < n; i++) {
    if (stops[i].unit == RN_STOP_PERCENT) pos[i] = stops[i].position / 100;
    if (stops[i].unit == RN_STOP_POINTS && length > 0) {
      pos[i] = stops[i].position / length;
    }
  }
  if (n > 0 && std::isnan(pos[0])) pos[0] = 0;
  if (n > 1 && std::isnan(pos[n - 1])) pos[n - 1] = 1;
  // A stop before an earlier one moves up to it.
  float last = 0;
  for (auto &p : pos) {
    if (!std::isnan(p)) last = p = std::max(p, last);
  }
  // Runs of unpositioned stops spread evenly between their neighbours.
  for (size_t i = 1; i < n; i++) {
    if (!std::isnan(pos[i])) continue;
    size_t j = i;
    while (std::isnan(pos[j])) j++;
    for (size_t k = i; k < j; k++) {
      pos[k] = pos[i - 1] + (pos[j] - pos[i - 1]) * (k - i + 1) / (j - i + 1);
    }
  }
  std::vector<GskColorStop> out(n);
  for (size_t i = 0; i < n; i++) {
    out[i].offset = std::clamp(pos[i], 0.0f, 1.0f);
    out[i].color = stops[i].color;
  }
  return out;
}

void append_gradient(GtkSnapshot *snapshot, const Gradient &g, float w,
                     float h) {
  if (g.stops.size() < 2) return;
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);
  if (!g.spec.radial) {
    // CSS: the gradient line runs through the centre at `angle`, long
    // enough that the corners get the end colors.
    float a = g.spec.angle * float(M_PI) / 180;
    float dx = std::sin(a), dy = -std::cos(a);
    float len = std::abs(w * dx) + std::abs(h * dy);
    graphene_point_t start = GRAPHENE_POINT_INIT(w / 2 - dx * len / 2,
                                                 h / 2 - dy * len / 2);
    graphene_point_t end = GRAPHENE_POINT_INIT(w / 2 + dx * len / 2,
                                               h / 2 + dy * len / 2);
    auto stops = resolve_stops(g.stops, len);
    gtk_snapshot_append_linear_gradient(snapshot, &bounds, &start, &end,
                                        stops.data(), stops.size());
    return;
  }
  float cx = g.spec.center_x * w, cy = g.spec.center_y * h;
  float fx = std::max(cx, w - cx), fy = std::max(cy, h - cy);
  float rx, ry;
  if (g.spec.circle) {
    rx = ry = std::hypot(fx, fy);  // farthest-corner circle
  } else {
    rx = fx * float(M_SQRT2);       // farthest-corner ellipse
    ry = fy * float(M_SQRT2);
  }
  if (rx <= 0 || ry <= 0) return;
  graphene_point_t center = GRAPHENE_POINT_INIT(cx, cy);
  auto stops = resolve_stops(g.stops, rx);
  gtk_snapshot_append_radial_gradient(snapshot, &bounds, &center, rx, ry, 0, 1,
                                      stops.data(), stops.size());
}

// CSS Filter Effects color matrices, written with output rows; GSK
// multiplies row vectors, so they go in transposed.
void push_color_matrix(GtkSnapshot *snapshot, const float rows[4][4],
                       const float offset[4]) {
  float m[16];
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) m[i * 4 + j] = rows[j][i];
  }
  graphene_matrix_t matrix;
  graphene_matrix_init_from_float(&matrix, m);
  graphene_vec4_t off;
  graphene_vec4_init(&off, offset[0], offset[1], offset[2], offset[3]);
  gtk_snapshot_push_color_matrix(snapshot, &matrix, &off);
}

void push_filter(GtkSnapshot *snapshot, const RNFilter &f) {
  float a = f.amount;
  float z[4] = {0, 0, 0, 0};
  switch (f.type) {
    case RN_FILTER_BLUR:
      // GSK's radius is twice the standard deviation.
      gtk_snapshot_push_blur(snapshot, 2 * a);
      return;
    case RN_FILTER_DROP_SHADOW: {
      GskShadow shadow{f.shadow.color, f.shadow.offset_x, f.shadow.offset_y,
                       2 * f.shadow.blur};
      gtk_snapshot_push_shadow(snapshot, &shadow, 1);
      return;
    }
    case RN_FILTER_BRIGHTNESS: {
      const float m[4][4] = {{a, 0, 0, 0}, {0, a, 0, 0}, {0, 0, a, 0},
                             {0, 0, 0, 1}};
      push_color_matrix(snapshot, m, z);
      return;
    }
    case RN_FILTER_CONTRAST: {
      const float m[4][4] = {{a, 0, 0, 0}, {0, a, 0, 0}, {0, 0, a, 0},
                             {0, 0, 0, 1}};
      float o = 0.5f - 0.5f * a;
      const float off[4] = {o, o, o, 0};
      push_color_matrix(snapshot, m, off);
      return;
    }
    case RN_FILTER_INVERT: {
      float d = 1 - 2 * a;
      const float m[4][4] = {{d, 0, 0, 0}, {0, d, 0, 0}, {0, 0, d, 0},
                             {0, 0, 0, 1}};
      const float off[4] = {a, a, a, 0};
      push_color_matrix(snapshot, m, off);
      return;
    }
    case RN_FILTER_OPACITY: {
      const float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0},
                             {0, 0, 0, a}};
      push_color_matrix(snapshot, m, z);
      return;
    }
    case RN_FILTER_GRAYSCALE: {
      float g = 1 - std::clamp(a, 0.0f, 1.0f);
      const float m[4][4] = {
          {0.2126f + 0.7874f * g, 0.7152f - 0.7152f * g, 0.0722f - 0.0722f * g, 0},
          {0.2126f - 0.2126f * g, 0.7152f + 0.2848f * g, 0.0722f - 0.0722f * g, 0},
          {0.2126f - 0.2126f * g, 0.7152f - 0.7152f * g, 0.0722f + 0.9278f * g, 0},
          {0, 0, 0, 1}};
      push_color_matrix(snapshot, m, z);
      return;
    }
    case RN_FILTER_SEPIA: {
      float s = 1 - std::clamp(a, 0.0f, 1.0f);
      const float m[4][4] = {
          {0.393f + 0.607f * s, 0.769f - 0.769f * s, 0.189f - 0.189f * s, 0},
          {0.349f - 0.349f * s, 0.686f + 0.314f * s, 0.168f - 0.168f * s, 0},
          {0.272f - 0.272f * s, 0.534f - 0.534f * s, 0.131f + 0.869f * s, 0},
          {0, 0, 0, 1}};
      push_color_matrix(snapshot, m, z);
      return;
    }
    case RN_FILTER_SATURATE: {
      const float m[4][4] = {
          {0.213f + 0.787f * a, 0.715f - 0.715f * a, 0.072f - 0.072f * a, 0},
          {0.213f - 0.213f * a, 0.715f + 0.285f * a, 0.072f - 0.072f * a, 0},
          {0.213f - 0.213f * a, 0.715f - 0.715f * a, 0.072f + 0.928f * a, 0},
          {0, 0, 0, 1}};
      push_color_matrix(snapshot, m, z);
      return;
    }
    case RN_FILTER_HUE_ROTATE: {
      float r = a * float(M_PI) / 180, c = std::cos(r), s = std::sin(r);
      const float m[4][4] = {
          {0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f,
           0.072f - c * 0.072f + s * 0.928f, 0},
          {0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f,
           0.072f - c * 0.072f - s * 0.283f, 0},
          {0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f,
           0.072f + c * 0.928f + s * 0.072f, 0},
          {0, 0, 0, 1}};
      push_color_matrix(snapshot, m, z);
      return;
    }
  }
}

// RN's resizeMode, in the padding box `box`.
void append_image(GtkSnapshot *snapshot, const Extras &x,
                  const graphene_rect_t &box) {
  float iw = gdk_texture_get_width(x.image) / x.image_scale;
  float ih = gdk_texture_get_height(x.image) / x.image_scale;
  if (iw <= 0 || ih <= 0 || box.size.width <= 0 || box.size.height <= 0) return;
  float bw = box.size.width, bh = box.size.height;
  graphene_rect_t dest = box;
  auto centred = [&](float w, float h) {
    return GRAPHENE_RECT_INIT(box.origin.x + (bw - w) / 2,
                              box.origin.y + (bh - h) / 2, w, h);
  };
  bool repeat = false;
  switch (x.image_fit) {
    case RN_IMAGE_STRETCH:
      break;
    case RN_IMAGE_CONTAIN: {
      float s = std::min(bw / iw, bh / ih);
      dest = centred(iw * s, ih * s);
      break;
    }
    case RN_IMAGE_COVER: {
      float s = std::max(bw / iw, bh / ih);
      dest = centred(iw * s, ih * s);
      break;
    }
    case RN_IMAGE_CENTER: {
      // Natural size, scaled down (never up) to fit.
      float s = std::min(1.0f, std::min(bw / iw, bh / ih));
      dest = centred(iw * s, ih * s);
      break;
    }
    case RN_IMAGE_REPEAT:
      repeat = true;
      dest = GRAPHENE_RECT_INIT(box.origin.x, box.origin.y, iw, ih);
      break;
    case RN_IMAGE_NONE:
      dest = GRAPHENE_RECT_INIT(box.origin.x, box.origin.y, iw, ih);
      break;
  }
  if (x.image_blur > 0) gtk_snapshot_push_blur(snapshot, x.image_blur);
  if (x.image_tinted) {
    // Every pixel takes the tint's color, keeping its own alpha.
    const float m[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, x.image_tint.alpha};
    graphene_matrix_t matrix;
    graphene_matrix_init_from_float(&matrix, m);
    graphene_vec4_t off;
    graphene_vec4_init(&off, x.image_tint.red, x.image_tint.green,
                       x.image_tint.blue, 0);
    gtk_snapshot_push_color_matrix(snapshot, &matrix, &off);
  }
  if (repeat) gtk_snapshot_push_repeat(snapshot, &box, &dest);
  gtk_snapshot_append_scaled_texture(snapshot, x.image,
                                     GSK_SCALING_FILTER_LINEAR, &dest);
  if (repeat) gtk_snapshot_pop(snapshot);
  if (x.image_tinted) gtk_snapshot_pop(snapshot);
  if (x.image_blur > 0) gtk_snapshot_pop(snapshot);
}

}  // namespace

static void rn_view_measure(GtkWidget *widget, GtkOrientation orientation,
                            int /*for_size*/, int *minimum, int *natural,
                            int *minimum_baseline, int *natural_baseline) {
  // Yoga owns layout. GTK only needs a natural size for the root view; the
  // minimum stays 0 so a parent may allocate any frame without warnings.
  graphene_rect_t frame = rn_widget_get_frame(widget);
  *minimum = 0;
  *natural = static_cast<int>(orientation == GTK_ORIENTATION_HORIZONTAL
                                  ? frame.size.width
                                  : frame.size.height);
  *minimum_baseline = *natural_baseline = -1;
}

static void rn_view_size_allocate(GtkWidget *widget, int /*width*/,
                                  int /*height*/, int /*baseline*/) {
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child)) {
    // Popovers parented here (a text "Copy" menu) place themselves.
    if (GTK_IS_POPOVER(child)) {
      gtk_popover_present(GTK_POPOVER(child));
      continue;
    }
    graphene_rect_t f = rn_widget_get_frame(child);
    graphene_point_t origin = GRAPHENE_POINT_INIT(f.origin.x, f.origin.y);
    GskTransform *t = gsk_transform_translate(nullptr, &origin);
    if (const graphene_matrix_t *m = widget_transform(child)) {
      t = gsk_transform_matrix(t, m);
    }
    gtk_widget_allocate(child, static_cast<int>(f.size.width),
                        static_cast<int>(f.size.height), -1, t);
  }
}

static void rn_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RNView *self = RN_VIEW(widget);
  const RNViewStyle &s = self->style;
  if (s.backface_hidden && !backface_visible(widget)) return;
  float w = gtk_widget_get_width(widget);
  float h = gtk_widget_get_height(widget);
  GskRoundedRect outline = outline_of(s, w, h);
  bool rounded = has_radius(s);
  Extras *x = self->extras;

  // CSS applies filters left to right: the first one is innermost.
  size_t filters = x ? x->filters.size() : 0;
  for (size_t i = filters; i-- > 0;) push_filter(snapshot, x->filters[i]);

  if (x) {
    for (const auto &sh : x->shadows) {
      if (!sh.inset && sh.color.alpha > 0) {
        gtk_snapshot_append_outset_shadow(snapshot, &outline, &sh.color,
                                          sh.offset_x, sh.offset_y, sh.spread,
                                          sh.blur);
      }
    }
  }

  bool gradients = x && !x->gradients.empty();
  if (s.background.alpha > 0 || gradients) {
    push_clip(snapshot, outline, rounded);
    if (s.background.alpha > 0) {
      gtk_snapshot_append_color(snapshot, &s.background, &outline.bounds);
    }
    if (gradients) {
      // The first gradient in the list paints on top, like CSS layers.
      for (size_t i = x->gradients.size(); i-- > 0;) {
        append_gradient(snapshot, x->gradients[i], w, h);
      }
    }
    gtk_snapshot_pop(snapshot);
  }

  if (x && x->image) {
    // Inside the border, clipped to the rounded padding box.
    GskRoundedRect padding = outline;
    gsk_rounded_rect_shrink(&padding, s.border_widths[0], s.border_widths[1],
                            s.border_widths[2], s.border_widths[3]);
    push_clip(snapshot, padding, rounded);
    append_image(snapshot, *x, padding.bounds);
    gtk_snapshot_pop(snapshot);
  }

  if (x) {
    GskRoundedRect padding = outline;
    gsk_rounded_rect_shrink(&padding, s.border_widths[0], s.border_widths[1],
                            s.border_widths[2], s.border_widths[3]);
    for (const auto &sh : x->shadows) {
      if (sh.inset && sh.color.alpha > 0) {
        gtk_snapshot_append_inset_shadow(snapshot, &padding, &sh.color,
                                         sh.offset_x, sh.offset_y, sh.spread,
                                         sh.blur);
      }
    }
  }

  if (s.clip_children) push_clip(snapshot, outline, rounded);
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child)) {
    if (!GTK_IS_NATIVE(child)) gtk_widget_snapshot_child(widget, child, snapshot);
  }
  if (s.clip_children) gtk_snapshot_pop(snapshot);

  // RN draws the border above the children's content.
  append_border(snapshot, s, outline);
  append_outline(snapshot, s, outline);

  for (size_t i = 0; i < filters; i++) gtk_snapshot_pop(snapshot);
}

static void rn_view_dispose(GObject *object) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(GTK_WIDGET(object)))) {
    gtk_widget_unparent(child);
  }
  G_OBJECT_CLASS(rn_view_parent_class)->dispose(object);
}

static void rn_view_finalize(GObject *object) {
  delete RN_VIEW(object)->extras;
  G_OBJECT_CLASS(rn_view_parent_class)->finalize(object);
}

static void rn_view_class_init(RNViewClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = rn_view_dispose;
  G_OBJECT_CLASS(klass)->finalize = rn_view_finalize;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = rn_view_measure;
  widget_class->size_allocate = rn_view_size_allocate;
  widget_class->snapshot = rn_view_snapshot;
  gtk_widget_class_set_css_name(widget_class, "rn-view");
}

static void rn_view_init(RNView *self) {
  self->style = RNViewStyle{};
  self->extras = nullptr;
}

GtkWidget *rn_view_new(void) {
  return GTK_WIDGET(g_object_new(RN_TYPE_VIEW, nullptr));
}

void rn_view_set_style(RNView *self, const RNViewStyle *style) {
  self->style = *style;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static Extras &extras(RNView *self) {
  if (!self->extras) self->extras = new Extras();
  return *self->extras;
}

void rn_view_set_box_shadows(RNView *self, const RNBoxShadow *shadows,
                             guint n) {
  if (!n && !self->extras) return;
  extras(self).shadows.assign(shadows, shadows + n);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_filters(RNView *self, const RNFilter *filters, guint n) {
  if (!n && !self->extras) return;
  extras(self).filters.assign(filters, filters + n);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_background_gradients(RNView *self, const RNGradient *gradients,
                                      guint n) {
  if (!n && !self->extras) return;
  auto &list = extras(self).gradients;
  list.clear();
  for (guint i = 0; i < n; i++) {
    Gradient g{gradients[i], {}};
    g.stops.assign(gradients[i].stops, gradients[i].stops + gradients[i].n_stops);
    g.spec.stops = nullptr;
    list.push_back(std::move(g));
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_image(RNView *self, GdkTexture *texture, float scale,
                       RNImageFit fit, const GdkRGBA *tint, float blur_radius) {
  if (!texture && !self->extras) return;
  Extras &x = extras(self);
  g_set_object(&x.image, texture);
  x.image_scale = scale > 0 ? scale : 1;
  x.image_fit = fit;
  x.image_tinted = tint && tint->alpha > 0;
  if (x.image_tinted) x.image_tint = *tint;
  x.image_blur = blur_radius;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

GdkTexture *rn_view_get_image(RNView *self) {
  return self->extras ? self->extras->image : nullptr;
}

void rn_view_insert_child(RNView *self, GtkWidget *child, int index) {
  GtkWidget *sibling = nullptr;
  if (index >= 0) {
    sibling = gtk_widget_get_first_child(GTK_WIDGET(self));
    for (int i = 0; sibling && i < index; i++) {
      sibling = gtk_widget_get_next_sibling(sibling);
    }
  }
  gtk_widget_insert_before(child, GTK_WIDGET(self), sibling);
}

void rn_view_remove_child(RNView *self, GtkWidget *child) {
  g_return_if_fail(gtk_widget_get_parent(child) == GTK_WIDGET(self));
  gtk_widget_unparent(child);
}

// ---------------------------------------------------------------------------
// Hit-testing

static bool contains(GtkWidget *widget, graphene_point_t p) {
  float w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
  if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return false;
  if (RN_IS_VIEW(widget)) {
    const RNViewStyle &s = RN_VIEW(widget)->style;
    if (has_radius(s)) {
      GskRoundedRect r = outline_of(s, w, h);
      return gsk_rounded_rect_contains_point(&r, &p);
    }
  }
  return true;
}

// Widgets mounted for React views (they carry a tag; RNView/RNText also
// count, for the spike). GTK widgets in between or inside (a scroll
// view's scrolled window, a text input's GtkText) are passed through but
// never hit themselves.
static bool is_react_widget(GtkWidget *widget) {
  static GQuark tag = g_quark_from_static_string("rn-tag");
  return RN_IS_VIEW(widget) || RN_IS_TEXT(widget) ||
         g_object_get_qdata(G_OBJECT(widget), tag) != nullptr;
}

static GtkWidget *pick(GtkWidget *widget, graphene_point_t p,
                       graphene_point_t *local) {
  if (!gtk_widget_get_visible(widget)) return nullptr;
  RNPointerEvents mode = rn_widget_get_pointer_events(widget);
  if (mode == RN_POINTER_EVENTS_NONE) return nullptr;
  if (RN_IS_VIEW(widget) && RN_VIEW(widget)->style.backface_hidden &&
      !backface_visible(widget)) {
    return nullptr;
  }
  bool react = is_react_widget(widget);
  bool inside = contains(widget, p);
  // Scrollers clip their content.
  // Scroll views (and every other mounted non-RNView control) clip.
  bool clips = (RN_IS_VIEW(widget) && RN_VIEW(widget)->style.clip_children) ||
               GTK_IS_VIEWPORT(widget) || (react && !RN_IS_VIEW(widget) &&
                                           !RN_IS_TEXT(widget));
  if (clips && !inside) return nullptr;
  if (mode != RN_POINTER_EVENTS_BOX_ONLY) {
    // Topmost first: later siblings paint above earlier ones.
    for (GtkWidget *child = gtk_widget_get_last_child(widget); child;
         child = gtk_widget_get_prev_sibling(child)) {
      if (GTK_IS_NATIVE(child) || GTK_IS_SCROLLBAR(child)) continue;
      graphene_point_t cp;
      if (!gtk_widget_compute_point(widget, child, &p, &cp)) continue;
      if (GtkWidget *hit = pick(child, cp, local)) return hit;
    }
  }
  if (react && mode != RN_POINTER_EVENTS_BOX_NONE && inside) {
    *local = p;
    return widget;
  }
  return nullptr;
}

GtkWidget *rn_widget_pick(GtkWidget *root, double x, double y,
                          double *local_x, double *local_y) {
  graphene_point_t local{};
  GtkWidget *hit = pick(root, GRAPHENE_POINT_INIT(float(x), float(y)), &local);
  if (hit) {
    *local_x = local.x;
    *local_y = local.y;
  }
  return hit;
}
