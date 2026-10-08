#include "rn_scroll_view.h"

#include "rn_view.h"

#include <algorithm>
#include <cmath>

struct _RNScrollView {
  GtkWidget parent_instance;
  GtkWidget *background;  // RNView
  GtkWidget *scrolled;    // GtkScrolledWindow
  GtkWidget *content;     // RNView in a GtkViewport
  gboolean horizontal;
  gboolean show_vertical, show_horizontal;
  gboolean scroll_enabled;
  int programmatic;       // > 0 while we set the adjustments ourselves
  gboolean dragging, decelerating;
  guint idle_timeout;     // detects the end of momentum
  // scroll_to(animated)
  guint animation_tick;
  gint64 animation_start;
  double from_x, from_y, to_x, to_y;
};

G_DEFINE_FINAL_TYPE(RNScrollView, rn_scroll_view, GTK_TYPE_WIDGET)

enum {
  SIGNAL_OFFSET_CHANGED,
  SIGNAL_DRAG_BEGIN,
  SIGNAL_DRAG_END,
  SIGNAL_MOMENTUM_BEGIN,
  SIGNAL_MOMENTUM_END,
  N_SIGNALS,
};
static guint signals[N_SIGNALS];

constexpr gint64 kAnimationUs = 250000;
constexpr guint kMomentumIdleMs = 120;

static GtkAdjustment *hadj(RNScrollView *self) {
  return gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->scrolled));
}

static GtkAdjustment *vadj(RNScrollView *self) {
  return gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled));
}

static double clamp_value(GtkAdjustment *adj, double value) {
  double max = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
  return std::clamp(value, gtk_adjustment_get_lower(adj), std::max(0.0, max));
}

static void update_policies(RNScrollView *self) {
  // Like UIScrollView, whichever axis the content overflows scrolls (RN's
  // iOS ScrollView doesn't send `horizontal`; Yoga already sizes the
  // content to the viewport on the other axis). A hidden indicator still
  // scrolls (EXTERNAL).
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(self->scrolled),
      self->show_horizontal ? GTK_POLICY_AUTOMATIC : GTK_POLICY_EXTERNAL,
      self->show_vertical ? GTK_POLICY_AUTOMATIC : GTK_POLICY_EXTERNAL);
}

static bool can_scroll(GtkAdjustment *adj) {
  return gtk_adjustment_get_upper(adj) - gtk_adjustment_get_lower(adj) >
         gtk_adjustment_get_page_size(adj) + 0.5;
}

static gboolean momentum_idle(gpointer data) {
  auto *self = RN_SCROLL_VIEW(data);
  self->idle_timeout = 0;
  if (self->decelerating) {
    self->decelerating = FALSE;
    g_signal_emit(self, signals[SIGNAL_MOMENTUM_END], 0);
  }
  return G_SOURCE_REMOVE;
}

static void on_value_changed(GtkAdjustment *, gpointer data) {
  auto *self = RN_SCROLL_VIEW(data);
  gboolean user = self->programmatic == 0;
  if (self->decelerating) {
    if (self->idle_timeout) g_source_remove(self->idle_timeout);
    self->idle_timeout = g_timeout_add(kMomentumIdleMs, momentum_idle, self);
  }
  g_signal_emit(self, signals[SIGNAL_OFFSET_CHANGED], 0, user);
}

static gboolean on_scroll(GtkEventControllerScroll *controller, double dx,
                          double dy, gpointer data) {
  auto *self = RN_SCROLL_VIEW(data);
  // Swallow scrolling before the scrolled window sees it when disabled.
  if (!self->scroll_enabled) return TRUE;
  // A plain wheel over a sideways-only scroller scrolls it sideways.
  if (dx == 0 && dy != 0 && !can_scroll(vadj(self)) && can_scroll(hadj(self))) {
    if (gtk_event_controller_scroll_get_unit(controller) == GDK_SCROLL_UNIT_SURFACE) {
      GtkAdjustment *h = hadj(self);
      gtk_adjustment_set_value(h, clamp_value(h, gtk_adjustment_get_value(h) + dy));
    } else {
      rn_scroll_view_scroll_by_wheel(self, 0, dy);
    }
    return TRUE;
  }
  return FALSE;
}

static void on_scroll_begin(GtkEventControllerScroll *, gpointer data) {
  auto *self = RN_SCROLL_VIEW(data);
  if (!self->scroll_enabled) return;
  if (self->decelerating) momentum_idle(self);
  self->dragging = TRUE;
  g_signal_emit(self, signals[SIGNAL_DRAG_BEGIN], 0);
}

static void on_scroll_end(GtkEventControllerScroll *, gpointer data) {
  auto *self = RN_SCROLL_VIEW(data);
  if (!self->dragging) return;
  self->dragging = FALSE;
  g_signal_emit(self, signals[SIGNAL_DRAG_END], 0);
}

static void on_decelerate(GtkEventControllerScroll *, double, double,
                          gpointer data) {
  auto *self = RN_SCROLL_VIEW(data);
  if (!self->scroll_enabled) return;
  self->decelerating = TRUE;
  g_signal_emit(self, signals[SIGNAL_MOMENTUM_BEGIN], 0);
  if (self->idle_timeout) g_source_remove(self->idle_timeout);
  self->idle_timeout = g_timeout_add(kMomentumIdleMs, momentum_idle, self);
}

static void set_values(RNScrollView *self, double x, double y) {
  self->programmatic++;
  gtk_adjustment_set_value(hadj(self), clamp_value(hadj(self), x));
  gtk_adjustment_set_value(vadj(self), clamp_value(vadj(self), y));
  self->programmatic--;
}

static void stop_animation(RNScrollView *self) {
  if (self->animation_tick) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(self), self->animation_tick);
    self->animation_tick = 0;
  }
}

static gboolean animation_tick(GtkWidget *widget, GdkFrameClock *clock,
                               gpointer) {
  auto *self = RN_SCROLL_VIEW(widget);
  double t = double(gdk_frame_clock_get_frame_time(clock) -
                    self->animation_start) / kAnimationUs;
  t = std::clamp(t, 0.0, 1.0);
  double e = 1 - std::pow(1 - t, 3);  // ease-out cubic
  set_values(self, self->from_x + (self->to_x - self->from_x) * e,
             self->from_y + (self->to_y - self->from_y) * e);
  if (t >= 1) {
    self->animation_tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

static void rn_scroll_view_measure(GtkWidget *widget, GtkOrientation o,
                                   int, int *minimum, int *natural,
                                   int *minimum_baseline,
                                   int *natural_baseline) {
  // Yoga owns the frame, as for RNView.
  graphene_rect_t f = rn_widget_get_frame(widget);
  *minimum = 0;
  *natural = int(o == GTK_ORIENTATION_HORIZONTAL ? f.size.width : f.size.height);
  *minimum_baseline = *natural_baseline = -1;
}

static void rn_scroll_view_size_allocate(GtkWidget *widget, int width,
                                         int height, int) {
  auto *self = RN_SCROLL_VIEW(widget);
  gtk_widget_allocate(self->background, width, height, -1, nullptr);
  gtk_widget_allocate(self->scrolled, width, height, -1, nullptr);
}

static void rn_scroll_view_dispose(GObject *object) {
  auto *self = RN_SCROLL_VIEW(object);
  stop_animation(self);
  if (self->idle_timeout) {
    g_source_remove(self->idle_timeout);
    self->idle_timeout = 0;
  }
  if (self->scrolled) {
    g_signal_handlers_disconnect_by_data(hadj(self), self);
    g_signal_handlers_disconnect_by_data(vadj(self), self);
  }
  g_clear_pointer(&self->background, gtk_widget_unparent);
  g_clear_pointer(&self->scrolled, gtk_widget_unparent);
  G_OBJECT_CLASS(rn_scroll_view_parent_class)->dispose(object);
}

static void rn_scroll_view_class_init(RNScrollViewClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = rn_scroll_view_dispose;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = rn_scroll_view_measure;
  widget_class->size_allocate = rn_scroll_view_size_allocate;
  gtk_widget_class_set_css_name(widget_class, "rn-scroll-view");
  signals[SIGNAL_OFFSET_CHANGED] = g_signal_new(
      "offset-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
      nullptr, nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  const char *simple[] = {"drag-begin", "drag-end", "momentum-begin",
                          "momentum-end"};
  for (int i = 0; i < 4; i++) {
    signals[SIGNAL_DRAG_BEGIN + i] =
        g_signal_new(simple[i], G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                     nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  }
}

static void rn_scroll_view_init(RNScrollView *self) {
  self->show_vertical = self->show_horizontal = TRUE;
  self->scroll_enabled = TRUE;

  self->background = rn_view_new();
  gtk_widget_set_parent(self->background, GTK_WIDGET(self));

  self->scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(self->scrolled),
                                            TRUE);
  GtkWidget *viewport = gtk_viewport_new(nullptr, nullptr);
  gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(viewport), FALSE);
  // Size the content from its natural size (its frame); RNView's minimum
  // is 0.
  gtk_scrollable_set_hscroll_policy(GTK_SCROLLABLE(viewport), GTK_SCROLL_NATURAL);
  gtk_scrollable_set_vscroll_policy(GTK_SCROLLABLE(viewport), GTK_SCROLL_NATURAL);
  self->content = rn_view_new();
  gtk_viewport_set_child(GTK_VIEWPORT(viewport), self->content);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled), viewport);
  gtk_widget_set_parent(self->scrolled, GTK_WIDGET(self));
  update_policies(self);

  g_signal_connect(hadj(self), "value-changed", G_CALLBACK(on_value_changed),
                   self);
  g_signal_connect(vadj(self), "value-changed", G_CALLBACK(on_value_changed),
                   self);

  // Watches scrolling ahead of the scrolled window: drag and momentum
  // phases for RN's events, and scrollEnabled.
  GtkEventController *scroll = gtk_event_controller_scroll_new(
      GtkEventControllerScrollFlags(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES |
                                    GTK_EVENT_CONTROLLER_SCROLL_KINETIC));
  gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
  g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), self);
  g_signal_connect(scroll, "scroll-begin", G_CALLBACK(on_scroll_begin), self);
  g_signal_connect(scroll, "scroll-end", G_CALLBACK(on_scroll_end), self);
  g_signal_connect(scroll, "decelerate", G_CALLBACK(on_decelerate), self);
  gtk_widget_add_controller(GTK_WIDGET(self), scroll);
}

GtkWidget *rn_scroll_view_new(void) {
  return GTK_WIDGET(g_object_new(RN_TYPE_SCROLL_VIEW, nullptr));
}

GtkWidget *rn_scroll_view_get_content(RNScrollView *self) {
  return self->content;
}

GtkWidget *rn_scroll_view_get_background(RNScrollView *self) {
  return self->background;
}

void rn_scroll_view_set_content_size(RNScrollView *self, float width,
                                     float height) {
  graphene_rect_t f = rn_widget_get_frame(self->content);
  if (f.size.width == width && f.size.height == height) return;
  rn_widget_set_frame(self->content, 0, 0, width, height);
  // The viewport sizes the content from its natural size.
  gtk_widget_queue_resize(self->content);
}

void rn_scroll_view_set_horizontal(RNScrollView *self, gboolean horizontal) {
  if (self->horizontal == horizontal) return;
  self->horizontal = horizontal;
  update_policies(self);
}

void rn_scroll_view_set_indicators(RNScrollView *self, gboolean vertical,
                                   gboolean horizontal) {
  if (self->show_vertical == vertical && self->show_horizontal == horizontal) {
    return;
  }
  self->show_vertical = vertical;
  self->show_horizontal = horizontal;
  update_policies(self);
}

void rn_scroll_view_set_scroll_enabled(RNScrollView *self, gboolean enabled) {
  self->scroll_enabled = enabled;
  gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(self->scrolled),
                                            enabled);
}

void rn_scroll_view_get_offset(RNScrollView *self, double *x, double *y) {
  *x = gtk_adjustment_get_value(hadj(self));
  *y = gtk_adjustment_get_value(vadj(self));
}

void rn_scroll_view_get_viewport_size(RNScrollView *self, double *width,
                                      double *height) {
  *width = gtk_widget_get_width(GTK_WIDGET(self));
  *height = gtk_widget_get_height(GTK_WIDGET(self));
}

void rn_scroll_view_scroll_to(RNScrollView *self, double x, double y,
                              gboolean animated) {
  stop_animation(self);
  if (!animated || !gtk_widget_get_mapped(GTK_WIDGET(self))) {
    set_values(self, x, y);
    return;
  }
  rn_scroll_view_get_offset(self, &self->from_x, &self->from_y);
  self->to_x = clamp_value(hadj(self), x);
  self->to_y = clamp_value(vadj(self), y);
  GdkFrameClock *clock = gtk_widget_get_frame_clock(GTK_WIDGET(self));
  self->animation_start = clock ? gdk_frame_clock_get_frame_time(clock)
                                : g_get_monotonic_time();
  self->animation_tick =
      gtk_widget_add_tick_callback(GTK_WIDGET(self), animation_tick, nullptr,
                                   nullptr);
}

void rn_scroll_view_scroll_by_wheel(RNScrollView *self, double dx, double dy) {
  if (!self->scroll_enabled) return;
  stop_animation(self);
  GtkAdjustment *h = hadj(self), *v = vadj(self);
  // GTK's step for discrete wheel scrolling.
  // A vertical wheel scrolls a sideways-only scroll view sideways.
  if (dx == 0 && !can_scroll(v) && can_scroll(h)) std::swap(dx, dy);
  double hstep = std::pow(gtk_adjustment_get_page_size(h), 2.0 / 3.0);
  double vstep = std::pow(gtk_adjustment_get_page_size(v), 2.0 / 3.0);
  gtk_adjustment_set_value(h, clamp_value(h, gtk_adjustment_get_value(h) + dx * hstep));
  gtk_adjustment_set_value(v, clamp_value(v, gtk_adjustment_get_value(v) + dy * vstep));
}
