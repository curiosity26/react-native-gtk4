// RNTextInput: the host widget for React Native's <TextInput>.
//
// Single-line inputs edit in a GtkText, multiline ones in a GtkTextView
// (wrapping, scrolling vertically). Both bring GTK's input methods,
// clipboard, undo and keyboard handling. An RNView under the editor draws
// the input's own background and border; the editor sits in the content
// box (inside padding and border).
//
// Signals (main thread):
//   text-changed                the user changed the text (not set_text)
//   selection-changed
//   focus-changed (gboolean)
//   submit                      Enter in a single-line input, or in a
//                               multiline one with submit-on-enter set
//   key-pressed (gchararray)    RN key name: a character, "Enter",
//                               "Backspace", "Tab"... before the edit
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RN_TYPE_TEXT_INPUT (rn_text_input_get_type())
G_DECLARE_FINAL_TYPE(RNTextInput, rn_text_input, RN, TEXT_INPUT, GtkWidget)

GtkWidget *rn_text_input_new(gboolean multiline);
gboolean rn_text_input_is_multiline(RNTextInput *self);
GtkWidget *rn_text_input_get_background(RNTextInput *self);
// The widget that takes keyboard focus and text (GtkText or GtkTextView).
GtkWidget *rn_text_input_get_editor(RNTextInput *self);

// Padding + border around the editor.
void rn_text_input_set_insets(RNTextInput *self, float top, float right,
                              float bottom, float left);

// Returns a newly allocated string.
char *rn_text_input_get_text(RNTextInput *self);
// Sets the text without emitting text-changed. The cursor keeps its
// position (clamped), so a controlled value that rewrites the text (say,
// uppercasing it) doesn't move the caret.
void rn_text_input_set_text(RNTextInput *self, const char *text);
// In characters. start == end is a caret.
void rn_text_input_get_selection(RNTextInput *self, int *start, int *end);
void rn_text_input_set_selection(RNTextInput *self, int start, int end);

typedef struct {
  const char *font_family;   // NULL: the theme's
  double font_size;          // px; <= 0: the theme's
  int font_weight;           // 100..900; 0: normal
  gboolean italic;
  GdkRGBA color;
  GdkRGBA placeholder_color;
  GdkRGBA caret_color;       // alpha 0: same as the text
  GdkRGBA selection_color;   // alpha 0: the theme's
  float letter_spacing;      // px
  int text_align;            // 0 start, 1 center, 2 end
  gboolean caret_hidden;
} RNTextInputStyle;

void rn_text_input_set_style(RNTextInput *self, const RNTextInputStyle *style);
void rn_text_input_set_placeholder(RNTextInput *self, const char *text);
void rn_text_input_set_editable(RNTextInput *self, gboolean editable);
// In characters; 0: unlimited.
void rn_text_input_set_max_length(RNTextInput *self, int max_length);
void rn_text_input_set_secure(RNTextInput *self, gboolean secure);
void rn_text_input_set_input_purpose(RNTextInput *self, GtkInputPurpose purpose,
                                     GtkInputHints hints);
// Multiline: Enter submits instead of inserting a newline.
void rn_text_input_set_submit_on_enter(RNTextInput *self, gboolean submit);

void rn_text_input_focus(RNTextInput *self);
void rn_text_input_blur(RNTextInput *self);
gboolean rn_text_input_has_focus(RNTextInput *self);

G_END_DECLS
