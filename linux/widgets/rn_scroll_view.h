// RNScrollView: the host widget for React Native's <ScrollView>.
//
// A GtkScrolledWindow (kinetic scrolling, overlay scrollbars, smooth
// touchpad deltas) around a GtkViewport whose child is an RNView "content"
// box sized to the scroll view's content. React children mount into the
// content box. A background RNView under the scroller draws the scroll
// view's own background and border.
//
// Signals (all on the main thread):
//   offset-changed (gboolean user)   the scroll offset moved; user is FALSE
//                                    for rn_scroll_view_scroll_to()
//   drag-begin / drag-end            a touchpad or touch drag started/ended
//   momentum-begin / momentum-end    kinetic deceleration started/stopped
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RN_TYPE_SCROLL_VIEW (rn_scroll_view_get_type())
G_DECLARE_FINAL_TYPE(RNScrollView, rn_scroll_view, RN, SCROLL_VIEW, GtkWidget)

GtkWidget *rn_scroll_view_new(void);

// Where React children go (an RNView).
GtkWidget *rn_scroll_view_get_content(RNScrollView *self);
// The RNView drawn under the scroller, for the scroll view's own style.
GtkWidget *rn_scroll_view_get_background(RNScrollView *self);

void rn_scroll_view_set_content_size(RNScrollView *self, float width,
                                     float height);
// RN's `horizontal`, where sent; the scrolling axis follows the content
// either way.
void rn_scroll_view_set_horizontal(RNScrollView *self, gboolean horizontal);
void rn_scroll_view_set_indicators(RNScrollView *self, gboolean vertical,
                                   gboolean horizontal);
// FALSE ignores wheel, touchpad and touch scrolling (scroll_to still works).
void rn_scroll_view_set_scroll_enabled(RNScrollView *self, gboolean enabled);

void rn_scroll_view_get_offset(RNScrollView *self, double *x, double *y);
void rn_scroll_view_get_viewport_size(RNScrollView *self, double *width,
                                      double *height);
// Clamped to the content; animated eases over ~250 ms.
void rn_scroll_view_scroll_to(RNScrollView *self, double x, double y,
                              gboolean animated);
// A user scroll by wheel units (one notch = 1), stepping like GTK's wheel
// handling. Used for synthesized input; real wheel events go to the
// GtkScrolledWindow directly.
void rn_scroll_view_scroll_by_wheel(RNScrollView *self, double dx, double dy);

G_END_DECLS
