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

// Measures text the way Yoga's measure function will: max_width < 0 means
// unconstrained. Safe to call from any thread.
graphene_size_t rn_text_measure(const char *text, const char *family,
                                double size_px, int weight, double max_width);

G_END_DECLS
