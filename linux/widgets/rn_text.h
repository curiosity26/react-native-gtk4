// RNText: paragraph host widget for React Native <Text>, drawn with Pango.
//
// Also exposes rn_text_measure(), the shape of the cxx TextLayoutManager
// measure call: it runs on any thread with a per-thread Pango font map,
// because Pango font maps are not thread-safe.
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RN_TYPE_TEXT (rn_text_get_type())
G_DECLARE_FINAL_TYPE(RNText, rn_text, RN, TEXT, GtkWidget)

GtkWidget *rn_text_new(const char *text);
void rn_text_set_text(RNText *self, const char *text);
void rn_text_set_font(RNText *self, const char *family, double size_px,
                      int weight);
void rn_text_set_color(RNText *self, const GdkRGBA *color);

// Replaces the widget's layout with a fully styled one (attributes carry the
// fonts and colors) and keeps a reference. Build it on
// gtk_widget_get_pango_context() so it follows the widget's font settings.
void rn_text_set_layout(RNText *self, PangoLayout *layout);

// The paragraph's text (owned by the widget).
const char *rn_text_get_text(RNText *self);
// The layout drawn (owned by the widget), for hit-testing text.
PangoLayout *rn_text_get_layout(RNText *self);

// Padding + border around the text (RN content insets): the layout is as
// wide as the frame minus these, and draws offset by top/left.
void rn_text_set_insets(RNText *self, float top, float right, float bottom,
                        float left);
void rn_text_get_insets(RNText *self, float insets[4]);

// Selection (selectable text), as byte indices into the text; start may
// be past end (a backwards drag). Setting an empty range clears it.
void rn_text_set_selection(RNText *self, int start, int end);
// FALSE if nothing is selected; start <= end.
gboolean rn_text_get_selection(RNText *self, int *start, int *end);
// The selected text (newly allocated), or NULL.
char *rn_text_get_selected_text(RNText *self);
// The highlight: `color` (selectionColor), or NULL for the theme's (its
// selected background at 30%, gray in an inactive window, like GtkLabel).
void rn_text_set_selection_color(RNText *self, const GdkRGBA *color);
// The byte index under (x, y) in widget coordinates, clamped to the text
// (for drags that leave the paragraph): the character's start, or with
// `round`, its end when the point is on its trailing half (a caret).
int rn_text_index_at(RNText *self, double x, double y, gboolean round);
// Grows [*start, *end) to whole words (double-click; *end == *start is
// the character at *start), or to the newline-delimited paragraph around
// them (triple-click).
void rn_text_extend_to_words(RNText *self, int *start, int *end);
void rn_text_extend_to_paragraph(RNText *self, int *start, int *end);

// Measures text the way Yoga's measure function will: max_width < 0 means
// unconstrained. Safe to call from any thread.
graphene_size_t rn_text_measure(const char *text, const char *family,
                                double size_px, int weight, double max_width);

G_END_DECLS
