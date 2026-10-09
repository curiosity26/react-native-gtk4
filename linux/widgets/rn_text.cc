#include "rn_text.h"

#include "rn_css.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cstring>

struct _RNText {
  GtkWidget parent_instance;
  PangoLayout *layout;
  GdkRGBA color;
  float insets[4];  // padding + border: top, right, bottom, left
  int sel_anchor, sel_cursor;  // byte indices; equal: no selection
  gboolean has_sel_color;
  GdkRGBA sel_color;
};

G_DEFINE_FINAL_TYPE(RNText, rn_text, GTK_TYPE_WIDGET)

static PangoFontDescription *make_font(const char *family, double size_px,
                                       int weight) {
  PangoFontDescription *desc = pango_font_description_new();
  pango_font_description_set_family(desc, family ? family : "Sans");
  pango_font_description_set_absolute_size(desc, size_px * PANGO_SCALE);
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  return desc;
}

static void rn_text_measure_vfunc(GtkWidget *widget, GtkOrientation orientation,
                                  int for_size, int *minimum, int *natural,
                                  int *minimum_baseline,
                                  int *natural_baseline) {
  RNText *self = RN_TEXT(widget);
  const float *in = self->insets;
  int width = -1;
  if (orientation == GTK_ORIENTATION_VERTICAL && for_size >= 0) {
    width = std::max(0, int(for_size - in[1] - in[3])) * PANGO_SCALE;
  }
  pango_layout_set_width(self->layout, width);
  int w, h;
  pango_layout_get_pixel_size(self->layout, &w, &h);
  w += int(in[1] + in[3]);
  h += int(in[0] + in[2]);
  // Minimum 0: Yoga decides the frame; text that does not fit is clipped.
  *minimum = 0;
  *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? w : h;
  *minimum_baseline = *natural_baseline = -1;
}

static void rn_text_size_allocate(GtkWidget *widget, int width, int, int) {
  RNText *self = RN_TEXT(widget);
  float content = std::max(0.0f, width - self->insets[1] - self->insets[3]);
  pango_layout_set_width(self->layout, int(content * PANGO_SCALE));
}

// GtkLabel's selection: the theme's selected background at 30% while the
// window is active, 50% gray in the backdrop.
static GdkRGBA selection_color(RNText *self) {
  if (self->has_sel_color) return self->sel_color;
  GtkWidget *widget = GTK_WIDGET(self);
  if (gtk_widget_get_state_flags(widget) & GTK_STATE_FLAG_BACKDROP) {
    return GdkRGBA{0.52f, 0.52f, 0.52f, 0.5f};
  }
  GdkRGBA accent = rn_theme_accent(widget);
  accent.alpha = 0.3f;
  return accent;
}

static void append_selection(RNText *self, GtkSnapshot *snapshot) {
  if (self->sel_anchor == self->sel_cursor) return;
  int range[2] = {std::min(self->sel_anchor, self->sel_cursor),
                  std::max(self->sel_anchor, self->sel_cursor)};
  cairo_region_t *region =
      gdk_pango_layout_get_clip_region(self->layout, 0, 0, range, 1);
  GdkRGBA color = selection_color(self);
  for (int i = 0, n = cairo_region_num_rectangles(region); i < n; i++) {
    cairo_rectangle_int_t r;
    cairo_region_get_rectangle(region, i, &r);
    graphene_rect_t rect = GRAPHENE_RECT_INIT(float(r.x), float(r.y),
                                              float(r.width), float(r.height));
    gtk_snapshot_append_color(snapshot, &color, &rect);
  }
  cairo_region_destroy(region);
}

static void rn_text_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RNText *self = RN_TEXT(widget);
  const float *in = self->insets;
  // Lines past numberOfLines (ellipsizeMode 'clip') fall outside the frame.
  float w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
  float cw = w - in[1] - in[3], ch = h - in[0] - in[2];
  int lw, lh;
  pango_layout_get_pixel_size(self->layout, &lw, &lh);
  bool clip = lh > ch + 1;
  if (clip) {
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(in[3], in[0], cw, ch);
    gtk_snapshot_push_clip(snapshot, &bounds);
  }
  bool offset = in[0] != 0 || in[3] != 0;
  if (offset) {
    gtk_snapshot_save(snapshot);
    graphene_point_t p = GRAPHENE_POINT_INIT(in[3], in[0]);
    gtk_snapshot_translate(snapshot, &p);
  }
  append_selection(self, snapshot);
  gtk_snapshot_append_layout(snapshot, self->layout, &self->color);
  if (offset) gtk_snapshot_restore(snapshot);
  if (clip) gtk_snapshot_pop(snapshot);
}

static void rn_text_dispose(GObject *object) {
  g_clear_object(&RN_TEXT(object)->layout);
  G_OBJECT_CLASS(rn_text_parent_class)->dispose(object);
}

static void rn_text_state_flags_changed(GtkWidget *widget,
                                        GtkStateFlags previous) {
  // The selection grays out in the backdrop.
  RNText *self = RN_TEXT(widget);
  GtkStateFlags now = gtk_widget_get_state_flags(widget);
  if (self->sel_anchor != self->sel_cursor &&
      ((now ^ previous) & GTK_STATE_FLAG_BACKDROP)) {
    gtk_widget_queue_draw(widget);
  }
  GTK_WIDGET_CLASS(rn_text_parent_class)->state_flags_changed(widget, previous);
}

static void rn_text_class_init(RNTextClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = rn_text_dispose;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = rn_text_measure_vfunc;
  widget_class->size_allocate = rn_text_size_allocate;
  widget_class->snapshot = rn_text_snapshot;
  widget_class->state_flags_changed = rn_text_state_flags_changed;
  gtk_widget_class_set_css_name(widget_class, "rn-text");
}

static void rn_text_init(RNText *self) {
  self->layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), nullptr);
  pango_layout_set_wrap(self->layout, PANGO_WRAP_WORD_CHAR);
  self->color = GdkRGBA{0, 0, 0, 1};
  rn_text_set_font(self, "Sans", 14, PANGO_WEIGHT_NORMAL);
}

GtkWidget *rn_text_new(const char *text) {
  auto *self = RN_TEXT(g_object_new(RN_TYPE_TEXT, nullptr));
  rn_text_set_text(self, text);
  return GTK_WIDGET(self);
}

void rn_text_set_text(RNText *self, const char *text) {
  pango_layout_set_text(self->layout, text ? text : "", -1);
  gtk_widget_queue_resize(GTK_WIDGET(self));
}

void rn_text_set_font(RNText *self, const char *family, double size_px,
                      int weight) {
  PangoFontDescription *desc = make_font(family, size_px, weight);
  pango_layout_set_font_description(self->layout, desc);
  pango_font_description_free(desc);
  gtk_widget_queue_resize(GTK_WIDGET(self));
}

void rn_text_set_layout(RNText *self, PangoLayout *layout) {
  // New text drops the selection; a restyle of the same text keeps it.
  if (self->sel_anchor != self->sel_cursor &&
      g_strcmp0(pango_layout_get_text(self->layout),
                pango_layout_get_text(layout)) != 0) {
    self->sel_anchor = self->sel_cursor = 0;
  }
  g_set_object(&self->layout, layout);
  gtk_widget_queue_resize(GTK_WIDGET(self));
}

PangoLayout *rn_text_get_layout(RNText *self) { return self->layout; }

void rn_text_set_insets(RNText *self, float top, float right, float bottom,
                        float left) {
  const float in[4] = {top, right, bottom, left};
  if (std::equal(in, in + 4, self->insets)) return;
  std::copy(in, in + 4, self->insets);
  gtk_widget_queue_resize(GTK_WIDGET(self));
}

void rn_text_get_insets(RNText *self, float insets[4]) {
  std::copy(self->insets, self->insets + 4, insets);
}

const char *rn_text_get_text(RNText *self) {
  return pango_layout_get_text(self->layout);
}

void rn_text_set_selection(RNText *self, int start, int end) {
  int length = int(strlen(pango_layout_get_text(self->layout)));
  start = std::clamp(start, 0, length);
  end = std::clamp(end, 0, length);
  if (start == end) start = end = 0;
  if (start == self->sel_anchor && end == self->sel_cursor) return;
  self->sel_anchor = start;
  self->sel_cursor = end;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

gboolean rn_text_get_selection(RNText *self, int *start, int *end) {
  *start = std::min(self->sel_anchor, self->sel_cursor);
  *end = std::max(self->sel_anchor, self->sel_cursor);
  return *start != *end;
}

char *rn_text_get_selected_text(RNText *self) {
  int start, end;
  if (!rn_text_get_selection(self, &start, &end)) return nullptr;
  return g_strndup(pango_layout_get_text(self->layout) + start, end - start);
}

void rn_text_set_selection_color(RNText *self, const GdkRGBA *color) {
  gboolean has = color != nullptr;
  if (has == self->has_sel_color &&
      (!has || gdk_rgba_equal(color, &self->sel_color))) {
    return;
  }
  self->has_sel_color = has;
  if (has) self->sel_color = *color;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

int rn_text_index_at(RNText *self, double x, double y, gboolean round) {
  int index = 0, trailing = 0;
  pango_layout_xy_to_index(self->layout,
                           int((x - self->insets[3]) * PANGO_SCALE),
                           int((y - self->insets[0]) * PANGO_SCALE), &index,
                           &trailing);
  const char *text = pango_layout_get_text(self->layout);
  const char *p = text + index;
  for (; round && trailing > 0 && *p; trailing--) p = g_utf8_next_char(p);
  return int(p - text);
}

// The word around character `c`, in characters; a character outside any
// word (space, punctuation) on its own, as GtkLabel's double-click does.
static void word_at(const PangoLogAttr *attrs, int n, long c, long *start,
                    long *end) {
  long s = c;
  while (s > 0 && !attrs[s].is_word_start && !attrs[s].is_word_end) s--;
  if (c < n - 1 && attrs[s].is_word_start) {
    long e = c + 1;
    while (e < n - 1 && !attrs[e].is_word_end) e++;
    *start = s;
    *end = e;
  } else {
    *start = c;
    *end = std::min<long>(c + 1, n - 1);
  }
}

void rn_text_extend_to_words(RNText *self, int *start, int *end) {
  const char *text = pango_layout_get_text(self->layout);
  int n = 0;
  const PangoLogAttr *attrs = pango_layout_get_log_attrs_readonly(self->layout, &n);
  if (n <= 1) return;
  long s = g_utf8_pointer_to_offset(text, text + *start);
  long e = g_utf8_pointer_to_offset(text, text + std::max(*end, *start));
  long ws, we, unused;
  word_at(attrs, n, std::min<long>(s, n - 2), &ws, &unused);
  word_at(attrs, n, std::clamp<long>(e - 1, std::min<long>(s, n - 2), n - 2),
          &unused, &we);
  *start = int(g_utf8_offset_to_pointer(text, ws) - text);
  *end = int(g_utf8_offset_to_pointer(text, std::max(we, ws)) - text);
}

void rn_text_extend_to_paragraph(RNText *self, int *start, int *end) {
  const char *text = pango_layout_get_text(self->layout);
  int length = int(strlen(text));
  int s = *start, e = std::max(*end, *start);
  while (s > 0 && text[s - 1] != '\n') s--;
  while (e < length && text[e] != '\n') e++;
  *start = s;
  *end = e;
}

void rn_text_set_color(RNText *self, const GdkRGBA *color) {
  self->color = *color;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

namespace {
// One Pango context per thread. The font options here must match what the
// main thread renders with, or measured and drawn sizes drift apart.
struct ThreadPango {
  PangoFontMap *font_map = pango_cairo_font_map_new();
  PangoContext *context = pango_font_map_create_context(font_map);
  ~ThreadPango() {
    g_object_unref(context);
    g_object_unref(font_map);
  }
};
}  // namespace

graphene_size_t rn_text_measure(const char *text, const char *family,
                                double size_px, int weight, double max_width) {
  thread_local ThreadPango pango;
  PangoLayout *layout = pango_layout_new(pango.context);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  PangoFontDescription *desc = make_font(family, size_px, weight);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_text(layout, text, -1);
  pango_layout_set_width(
      layout, max_width < 0 ? -1 : static_cast<int>(max_width * PANGO_SCALE));
  int w, h;
  pango_layout_get_pixel_size(layout, &w, &h);
  g_object_unref(layout);
  return GRAPHENE_SIZE_INIT(static_cast<float>(w), static_cast<float>(h));
}
