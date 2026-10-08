// Turns a React Native AttributedString into a PangoLayout.
//
// The same function builds the layout Yoga measures (TextLayoutManager) and
// the layout RNText draws, so measured and drawn text agree.
#pragma once

#include <pango/pango.h>
#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace rngtk {

// The font family RN text uses when a fragment names none.
inline constexpr const char *kDefaultFontFamily = "Sans";

// Registers the Pango context to measure with on the GTK main thread. Pass
// one made by gtk_widget_create_pango_context() so measuring uses the same
// font options (hinting, antialiasing) as drawing.
void set_main_thread_pango_context(PangoContext *context);

// A Pango context safe to use on the calling thread: the registered one on
// the main thread, otherwise a per-thread context (Pango font maps are not
// thread-safe).
PangoContext *pango_context_for_current_thread();

// Builds a layout for the string. max_width < 0 means unconstrained.
// Returns a new reference.
PangoLayout *create_pango_layout(
    PangoContext *context,
    const facebook::react::AttributedString &string,
    const facebook::react::ParagraphAttributes &paragraph,
    float max_width);

// Logical size of the layout in px, rounded up.
void pango_layout_size_px(PangoLayout *layout, float *width, float *height);

}  // namespace rngtk
