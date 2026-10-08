// RNView: the GTK4 host widget that a React Native <View> mounts into.
//
// Children are positioned at absolute frames computed by Yoga (here: set by
// the caller). We never use GtkFixed or GtkLayoutManager: size_allocate reads
// each child's frame (and transform) and allocates it with a GskTransform,
// and snapshot draws RN styles with GSK: background, gradients, per-side
// borders with per-corner radii, box shadows, outline, filters, clipping.
//
// Plain GTK: no React Native types, so the Phase 0 spike builds it too.
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RN_TYPE_VIEW (rn_view_get_type())
G_DECLARE_FINAL_TYPE(RNView, rn_view, RN, VIEW, GtkWidget)

typedef enum {
  RN_LINE_SOLID,
  RN_LINE_DASHED,
  RN_LINE_DOTTED,
} RNLineStyle;

// Arrays follow GSK's order: corners top-left, top-right, bottom-right,
// bottom-left; sides top, right, bottom, left.
typedef struct {
  GdkRGBA background;            // transparent by default
  graphene_size_t radii[4];      // elliptical corner radii in logical px
  float border_widths[4];
  GdkRGBA border_colors[4];
  RNLineStyle border_style;
  float outline_width;
  float outline_offset;
  GdkRGBA outline_color;
  RNLineStyle outline_style;
  gboolean clip_children;        // RN overflow: 'hidden'
  gboolean backface_hidden;      // RN backfaceVisibility: 'hidden'
} RNViewStyle;

typedef struct {
  float offset_x, offset_y;
  float blur;    // CSS blur radius
  float spread;
  GdkRGBA color;
  gboolean inset;
} RNBoxShadow;

typedef enum {
  RN_FILTER_BLUR,         // amount: CSS blur radius in px
  RN_FILTER_BRIGHTNESS,   // amount: multiplier (1 = unchanged)
  RN_FILTER_CONTRAST,
  RN_FILTER_GRAYSCALE,    // amount: 0..1
  RN_FILTER_HUE_ROTATE,   // amount: degrees
  RN_FILTER_INVERT,
  RN_FILTER_OPACITY,
  RN_FILTER_SATURATE,
  RN_FILTER_SEPIA,
  RN_FILTER_DROP_SHADOW,  // shadow: offset, blur = standard deviation, color
} RNFilterType;

typedef struct {
  RNFilterType type;
  float amount;
  RNBoxShadow shadow;
} RNFilter;

typedef enum {
  RN_STOP_AUTO,     // spread evenly between neighbours, like CSS
  RN_STOP_PERCENT,
  RN_STOP_POINTS,
} RNStopUnit;

typedef struct {
  GdkRGBA color;
  float position;
  RNStopUnit unit;
} RNColorStop;

typedef struct {
  gboolean radial;
  // Linear: CSS angle in degrees (0 = to top, 90 = to right), already
  // resolved from keywords like "to bottom right".
  float angle;
  // Radial: an ellipse (or circle) reaching the farthest corner, at a
  // centre given as a fraction of the view's size.
  gboolean circle;
  float center_x, center_y;
  const RNColorStop *stops;
  guint n_stops;
} RNGradient;

// RN Image resizeMode.
typedef enum {
  RN_IMAGE_COVER,
  RN_IMAGE_CONTAIN,
  RN_IMAGE_STRETCH,
  RN_IMAGE_CENTER,
  RN_IMAGE_REPEAT,
  RN_IMAGE_NONE,  // natural size at the top-left
} RNImageFit;

typedef enum {
  RN_POINTER_EVENTS_AUTO,
  RN_POINTER_EVENTS_NONE,
  RN_POINTER_EVENTS_BOX_NONE,
  RN_POINTER_EVENTS_BOX_ONLY,
} RNPointerEvents;

GtkWidget *rn_view_new(void);

void rn_view_set_style(RNView *self, const RNViewStyle *style);
// Copies the arrays; n = 0 clears.
void rn_view_set_box_shadows(RNView *self, const RNBoxShadow *shadows, guint n);
void rn_view_set_filters(RNView *self, const RNFilter *filters, guint n);
void rn_view_set_background_gradients(RNView *self, const RNGradient *gradients,
                                      guint n);

// An image drawn inside the view's padding box (above the background,
// clipped to the rounded corners). `scale` is the image's pixels per point
// (2 for @2x assets). tint: NULL or a color all opaque pixels take. NULL
// texture clears.
void rn_view_set_image(RNView *self, GdkTexture *texture, float scale,
                       RNImageFit fit, const GdkRGBA *tint, float blur_radius);
GdkTexture *rn_view_get_image(RNView *self);

// Inserts child at index (RN mount instruction "Insert"); -1 appends.
void rn_view_insert_child(RNView *self, GtkWidget *child, int index);
void rn_view_remove_child(RNView *self, GtkWidget *child);

// Frame of any child mounted in an RNView, relative to its parent.
// Works for any GtkWidget (RNView, RNText, or a real GTK control).
void rn_widget_set_frame(GtkWidget *widget, float x, float y, float width,
                         float height);
graphene_rect_t rn_widget_get_frame(GtkWidget *widget);

// A transform in the widget's own coordinates (transform-origin already
// applied), composed after the frame's offset. NULL or identity clears.
void rn_widget_set_transform(GtkWidget *widget, const graphene_matrix_t *matrix);

void rn_widget_set_pointer_events(GtkWidget *widget, RNPointerEvents mode);
RNPointerEvents rn_widget_get_pointer_events(GtkWidget *widget);

// React Native hit-testing under `root`: the deepest widget at (x, y) in
// root coordinates, honouring transforms, overflow clipping (with rounded
// corners), pointerEvents and visibility. Stores the point in the hit
// widget's coordinates in local_x/local_y. NULL when nothing is hit.
GtkWidget *rn_widget_pick(GtkWidget *root, double x, double y,
                          double *local_x, double *local_y);

G_END_DECLS
