#include "rn_text.h"

#include <pango/pangocairo.h>

#include <algorithm>

struct _RNText {
  GtkWidget parent_instance;
  PangoLayout *layout;
  GdkRGBA color;
  float insets[4];  // padding + border: top, right, bottom, left
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
  gtk_snapshot_append_layout(snapshot, self->layout, &self->color);
  if (offset) gtk_snapshot_restore(snapshot);
  if (clip) gtk_snapshot_pop(snapshot);
}

static void rn_text_dispose(GObject *object) {
  g_clear_object(&RN_TEXT(object)->layout);
  G_OBJECT_CLASS(rn_text_parent_class)->dispose(object);
}

static void rn_text_class_init(RNTextClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = rn_text_dispose;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = rn_text_measure_vfunc;
  widget_class->size_allocate = rn_text_size_allocate;
  widget_class->snapshot = rn_text_snapshot;
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
