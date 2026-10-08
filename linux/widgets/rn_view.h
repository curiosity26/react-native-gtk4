// RNView: the GTK4 host widget that a React Native <View> will mount into.
//
// Children are positioned at absolute frames computed by Yoga (here: set by
// the caller). We never use GtkFixed or GtkLayoutManager: size_allocate reads
// each child's frame and allocates it with a translate transform, and
// snapshot draws RN styles (background, rounded border, clipping) with GSK.
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RN_TYPE_VIEW (rn_view_get_type())
G_DECLARE_FINAL_TYPE(RNView, rn_view, RN, VIEW, GtkWidget)

typedef struct {
  GdkRGBA background;    // transparent by default
  float border_radius;   // uniform radius in logical px
  float border_width;    // uniform width in logical px
  GdkRGBA border_color;
  gboolean clip_children;  // RN overflow: 'hidden'
} RNViewStyle;

GtkWidget *rn_view_new(void);

void rn_view_set_style(RNView *self, const RNViewStyle *style);

// Inserts child at index (RN mount instruction "Insert"); -1 appends.
void rn_view_insert_child(RNView *self, GtkWidget *child, int index);
void rn_view_remove_child(RNView *self, GtkWidget *child);

// Frame of any child mounted in an RNView, relative to its parent.
// Works for any GtkWidget (RNView, RNText, or a real GTK control).
void rn_widget_set_frame(GtkWidget *widget, float x, float y, float width,
                         float height);
graphene_rect_t rn_widget_get_frame(GtkWidget *widget);

G_END_DECLS
