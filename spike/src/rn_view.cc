#include "rn_view.h"

struct _RNView {
  GtkWidget parent_instance;
  RNViewStyle style;
};

G_DEFINE_FINAL_TYPE(RNView, rn_view, GTK_TYPE_WIDGET)

static GQuark frame_quark() {
  static GQuark q = g_quark_from_static_string("rn-frame");
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
    graphene_rect_t f = rn_widget_get_frame(child);
    graphene_point_t origin = GRAPHENE_POINT_INIT(f.origin.x, f.origin.y);
    GskTransform *t = gsk_transform_translate(nullptr, &origin);
    gtk_widget_allocate(child, static_cast<int>(f.size.width),
                        static_cast<int>(f.size.height), -1, t);
  }
}

static void rn_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RNView *self = RN_VIEW(widget);
  const RNViewStyle &s = self->style;
  float w = gtk_widget_get_width(widget);
  float h = gtk_widget_get_height(widget);

  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);
  GskRoundedRect outline;
  gsk_rounded_rect_init_from_rect(&outline, &bounds, s.border_radius);

  if (s.background.alpha > 0) {
    if (s.border_radius > 0) {
      gtk_snapshot_push_rounded_clip(snapshot, &outline);
      gtk_snapshot_append_color(snapshot, &s.background, &outline.bounds);
      gtk_snapshot_pop(snapshot);
    } else {
      gtk_snapshot_append_color(snapshot, &s.background, &outline.bounds);
    }
  }

  bool clip = s.clip_children;
  if (clip) {
    if (s.border_radius > 0) {
      gtk_snapshot_push_rounded_clip(snapshot, &outline);
    } else {
      gtk_snapshot_push_clip(snapshot, &outline.bounds);
    }
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child)) {
    gtk_widget_snapshot_child(widget, child, snapshot);
  }
  if (clip) gtk_snapshot_pop(snapshot);

  // RN draws the border above the children's content.
  if (s.border_width > 0 && s.border_color.alpha > 0) {
    const float widths[4] = {s.border_width, s.border_width, s.border_width,
                             s.border_width};
    const GdkRGBA colors[4] = {s.border_color, s.border_color, s.border_color,
                               s.border_color};
    gtk_snapshot_append_border(snapshot, &outline, widths, colors);
  }
}

static void rn_view_dispose(GObject *object) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(GTK_WIDGET(object)))) {
    gtk_widget_unparent(child);
  }
  G_OBJECT_CLASS(rn_view_parent_class)->dispose(object);
}

static void rn_view_class_init(RNViewClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = rn_view_dispose;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = rn_view_measure;
  widget_class->size_allocate = rn_view_size_allocate;
  widget_class->snapshot = rn_view_snapshot;
  gtk_widget_class_set_css_name(widget_class, "rn-view");
}

static void rn_view_init(RNView *self) {
  self->style = RNViewStyle{};
}

GtkWidget *rn_view_new(void) {
  return GTK_WIDGET(g_object_new(RN_TYPE_VIEW, nullptr));
}

void rn_view_set_style(RNView *self, const RNViewStyle *style) {
  self->style = *style;
  gtk_widget_queue_draw(GTK_WIDGET(self));
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
