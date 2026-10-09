#include "PangoText.h"

#include <pango/pangocairo.h>
#include <react/renderer/graphics/Color.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <mutex>
#include <thread>

using namespace facebook::react;

namespace rngtk {

namespace {

PangoContext *g_main_context = nullptr;
std::atomic<std::thread::id> g_main_thread;
// The main context's font options and resolution, for the other threads'
// contexts (the JS thread measures text): measuring and drawing must agree.
std::mutex g_options_mutex;
cairo_font_options_t *g_font_options = nullptr;
double g_resolution = -1;

struct ThreadPango {
  PangoFontMap *font_map = pango_cairo_font_map_new();
  PangoContext *context = pango_font_map_create_context(font_map);
  ThreadPango() {
    std::lock_guard<std::mutex> lock(g_options_mutex);
    if (g_font_options) pango_cairo_context_set_font_options(context, g_font_options);
    if (g_resolution > 0) pango_cairo_context_set_resolution(context, g_resolution);
  }
  ~ThreadPango() {
    g_object_unref(context);
    g_object_unref(font_map);
  }
};

void insert(PangoAttrList *list, PangoAttribute *attr, guint start,
            guint end) {
  attr->start_index = start;
  attr->end_index = end;
  pango_attr_list_insert(list, attr);
}

guint16 channel16(float c) {
  return static_cast<guint16>(std::lround(std::clamp(c, 0.0f, 1.0f) * 65535));
}

void insert_color(PangoAttrList *list, const SharedColor &color, bool fg,
                  guint start, guint end) {
  if (!color) return;
  ColorComponents c = colorComponentsFromColor(color);
  guint16 r = channel16(c.red), g = channel16(c.green), b = channel16(c.blue);
  insert(list,
         fg ? pango_attr_foreground_new(r, g, b)
            : pango_attr_background_new(r, g, b),
         start, end);
  guint16 a = channel16(c.alpha);
  insert(list,
         fg ? pango_attr_foreground_alpha_new(a)
            : pango_attr_background_alpha_new(a),
         start, end);
}

PangoStyle to_pango(FontStyle style) {
  switch (style) {
    case FontStyle::Italic:
      return PANGO_STYLE_ITALIC;
    case FontStyle::Oblique:
      return PANGO_STYLE_OBLIQUE;
    default:
      return PANGO_STYLE_NORMAL;
  }
}

}  // namespace

float effective_font_size(const TextAttributes &a) {
  float size = std::isnan(a.fontSize) ? 14.0f : a.fontSize;
  float multiplier = std::isnan(a.fontSizeMultiplier) ? 1.0f : a.fontSizeMultiplier;
  if (a.allowFontScaling.has_value() && !*a.allowFontScaling) multiplier = 1;
  if (!std::isnan(a.maxFontSizeMultiplier) && a.maxFontSizeMultiplier >= 1) {
    multiplier = std::min(multiplier, float(a.maxFontSizeMultiplier));
  }
  return size * multiplier;
}

namespace {

void apply_fragment(PangoAttrList *list, const TextAttributes &a, guint start,
                    guint end) {
  PangoFontDescription *desc = pango_font_description_new();
  pango_font_description_set_family(
      desc, a.fontFamily.empty() ? kDefaultFontFamily : a.fontFamily.c_str());
  pango_font_description_set_absolute_size(desc,
                                           effective_font_size(a) * PANGO_SCALE);
  if (a.fontWeight) {
    pango_font_description_set_weight(
        desc, static_cast<PangoWeight>(static_cast<int>(*a.fontWeight)));
  }
  if (a.fontStyle) pango_font_description_set_style(desc, to_pango(*a.fontStyle));
  insert(list, pango_attr_font_desc_new(desc), start, end);
  pango_font_description_free(desc);

  insert_color(list, a.foregroundColor, true, start, end);
  if (a.backgroundColor && alphaFromColor(a.backgroundColor) > 0) {
    insert_color(list, a.backgroundColor, false, start, end);
  }
  if (!std::isnan(a.letterSpacing)) {
    insert(list,
           pango_attr_letter_spacing_new(
               static_cast<int>(a.letterSpacing * PANGO_SCALE)),
           start, end);
  }
  if (!std::isnan(a.lineHeight) && a.lineHeight > 0) {
    insert(list,
           pango_attr_line_height_new_absolute(
               static_cast<int>(a.lineHeight * PANGO_SCALE)),
           start, end);
  }
  if (a.textDecorationLineType) {
    auto t = *a.textDecorationLineType;
    // Pango has no dotted or dashed underline; those draw solid.
    PangoUnderline underline = PANGO_UNDERLINE_SINGLE;
    if (a.textDecorationStyle == TextDecorationStyle::Double) {
      underline = PANGO_UNDERLINE_DOUBLE;
    } else if (a.textDecorationStyle == TextDecorationStyle::Wavy) {
      underline = PANGO_UNDERLINE_ERROR;
    }
    std::optional<ColorComponents> color;
    if (a.textDecorationColor) {
      color = colorComponentsFromColor(a.textDecorationColor);
    }
    if (t == TextDecorationLineType::Underline ||
        t == TextDecorationLineType::UnderlineStrikethrough) {
      insert(list, pango_attr_underline_new(underline), start, end);
      if (color) {
        insert(list,
               pango_attr_underline_color_new(channel16(color->red),
                                              channel16(color->green),
                                              channel16(color->blue)),
               start, end);
      }
    }
    if (t == TextDecorationLineType::Strikethrough ||
        t == TextDecorationLineType::UnderlineStrikethrough) {
      insert(list, pango_attr_strikethrough_new(TRUE), start, end);
      if (color) {
        insert(list,
               pango_attr_strikethrough_color_new(channel16(color->red),
                                                  channel16(color->green),
                                                  channel16(color->blue)),
               start, end);
      }
    }
  }
  if (a.textTransform) {
    PangoTextTransform t = PANGO_TEXT_TRANSFORM_NONE;
    switch (*a.textTransform) {
      case TextTransform::Uppercase:
        t = PANGO_TEXT_TRANSFORM_UPPERCASE;
        break;
      case TextTransform::Lowercase:
        t = PANGO_TEXT_TRANSFORM_LOWERCASE;
        break;
      case TextTransform::Capitalize:
        t = PANGO_TEXT_TRANSFORM_CAPITALIZE;
        break;
      default:
        break;
    }
    insert(list, pango_attr_text_transform_new(t), start, end);
  }
}

void apply_alignment(PangoLayout *layout, std::optional<TextAlignment> align) {
  PangoAlignment a = PANGO_ALIGN_LEFT;
  bool justify = false;
  if (align) {
    switch (*align) {
      case TextAlignment::Center:
        a = PANGO_ALIGN_CENTER;
        break;
      case TextAlignment::Right:
      case TextAlignment::End:
        a = PANGO_ALIGN_RIGHT;
        break;
      case TextAlignment::Justified:
        justify = true;
        break;
      default:
        break;
    }
  }
  pango_layout_set_alignment(layout, a);
  pango_layout_set_justify(layout, justify);
}

PangoEllipsizeMode to_pango(EllipsizeMode mode) {
  switch (mode) {
    case EllipsizeMode::Head:
      return PANGO_ELLIPSIZE_START;
    case EllipsizeMode::Middle:
      return PANGO_ELLIPSIZE_MIDDLE;
    case EllipsizeMode::Clip:
      return PANGO_ELLIPSIZE_NONE;
    default:
      return PANGO_ELLIPSIZE_END;
  }
}

// numberOfLines with ellipsizeMode 'clip': Pango only limits lines when it
// ellipsizes, so the layout keeps every line and its measured height stops
// after the last allowed one; RNText clips the rest.
const char *kClipLinesKey = "rngtk-clip-lines";

}  // namespace

void set_main_thread_pango_context(PangoContext *context) {
  g_set_object(&g_main_context, context);
  g_main_thread = std::this_thread::get_id();
  std::lock_guard<std::mutex> lock(g_options_mutex);
  if (g_font_options) cairo_font_options_destroy(g_font_options);
  const cairo_font_options_t *options =
      pango_cairo_context_get_font_options(context);
  g_font_options = options ? cairo_font_options_copy(options) : nullptr;
  g_resolution = pango_cairo_context_get_resolution(context);
}

PangoContext *pango_context_for_current_thread() {
  if (g_main_context && std::this_thread::get_id() == g_main_thread) {
    return g_main_context;
  }
  thread_local ThreadPango pango;
  return pango.context;
}

PangoLayout *create_pango_layout(PangoContext *context,
                                 const AttributedString &string,
                                 const ParagraphAttributes &paragraph,
                                 float max_width) {
  PangoLayout *layout = pango_layout_new(context);
  PangoAttrList *attrs = pango_attr_list_new();
  std::string text;
  std::optional<TextAlignment> alignment;

  for (const auto &fragment : string.getFragments()) {
    auto start = static_cast<guint>(text.size());
    text += fragment.string;
    auto end = static_cast<guint>(text.size());
    if (fragment.isAttachment()) {
      // Inline views are not supported yet; take up no space.
      PangoRectangle empty{0, 0, 0, 0};
      insert(attrs, pango_attr_shape_new(&empty, &empty), start, end);
      continue;
    }
    apply_fragment(attrs, fragment.textAttributes, start, end);
    if (!alignment) alignment = fragment.textAttributes.alignment;
  }

  pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
  pango_layout_set_attributes(layout, attrs);
  pango_attr_list_unref(attrs);

  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  apply_alignment(layout, alignment);
  if (paragraph.maximumNumberOfLines > 0) {
    if (paragraph.ellipsizeMode == EllipsizeMode::Clip) {
      g_object_set_data(G_OBJECT(layout), kClipLinesKey,
                        GINT_TO_POINTER(paragraph.maximumNumberOfLines));
    } else {
      pango_layout_set_height(layout, -paragraph.maximumNumberOfLines);
      pango_layout_set_ellipsize(layout, to_pango(paragraph.ellipsizeMode));
    }
  }
  pango_layout_set_width(
      layout, max_width < 0 || std::isinf(max_width)
                  ? -1
                  : static_cast<int>(std::ceil(max_width * PANGO_SCALE)));
  return layout;
}

void pango_layout_size_px(PangoLayout *layout, float *width, float *height) {
  PangoRectangle logical;
  pango_layout_get_extents(layout, nullptr, &logical);
  *width = std::ceil(static_cast<float>(logical.width) / PANGO_SCALE);
  *height = std::ceil(static_cast<float>(logical.height) / PANGO_SCALE);
  int clip_lines =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(layout), kClipLinesKey));
  if (clip_lines > 0 && pango_layout_get_line_count(layout) > clip_lines) {
    PangoLayoutIter *iter = pango_layout_get_iter(layout);
    for (int i = 1; i < clip_lines; i++) pango_layout_iter_next_line(iter);
    PangoRectangle line;
    pango_layout_iter_get_line_extents(iter, nullptr, &line);
    pango_layout_iter_free(iter);
    *height = std::ceil(static_cast<float>(line.y + line.height) / PANGO_SCALE);
  }
}

}  // namespace rngtk
