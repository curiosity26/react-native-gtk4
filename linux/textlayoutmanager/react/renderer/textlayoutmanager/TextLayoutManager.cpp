// Pango-backed TextLayoutManager for the GTK platform.
#include "TextLayoutManager.h"

#include "PangoText.h"

namespace facebook::react {

TextLayoutManager::TextLayoutManager(
    const std::shared_ptr<const ContextContainer>& contextContainer)
    : contextContainer_(contextContainer),
      textMeasureCache_(kSimpleThreadSafeCacheSizeCap) {}

TextMeasurement TextLayoutManager::measure(
    const AttributedStringBox& attributedStringBox,
    const ParagraphAttributes& paragraphAttributes,
    const TextLayoutContext& layoutContext,
    const LayoutConstraints& layoutConstraints) const {
  const auto& attributedString = attributedStringBox.getValue();

  auto measureText = [&]() {
    PangoLayout* layout = rngtk::create_pango_layout(
        rngtk::pango_context_for_current_thread(),
        attributedString,
        paragraphAttributes,
        layoutConstraints.maximumSize.width);
    float width = 0, height = 0;
    rngtk::pango_layout_size_px(layout, &width, &height);
    g_object_unref(layout);

    TextMeasurement::Attachments attachments;
    for (const auto& fragment : attributedString.getFragments()) {
      if (fragment.isAttachment()) {
        attachments.push_back(
            TextMeasurement::Attachment{
                .frame = {.origin = {.x = 0, .y = 0},
                          .size = {.width = 0, .height = 0}},
                .isClipped = true});
      }
    }
    return TextMeasurement{
        .size = {.width = width, .height = height},
        .attachments = attachments};
  };

  auto measurement = textMeasureCache_.get(
      {.attributedString = attributedString,
       .paragraphAttributes = paragraphAttributes,
       .layoutConstraints = layoutConstraints,
       .pointScaleFactor = layoutContext.pointScaleFactor},
      std::move(measureText));
  measurement.size = layoutConstraints.clamp(measurement.size);
  return measurement;
}

LinesMeasurements TextLayoutManager::measureLines(
    const AttributedStringBox& attributedStringBox,
    const ParagraphAttributes& paragraphAttributes,
    const Size& size) const {
  PangoContext* context = rngtk::pango_context_for_current_thread();
  PangoLayout* layout = rngtk::create_pango_layout(
      context, attributedStringBox.getValue(), paragraphAttributes, size.width);
  const char* text = pango_layout_get_text(layout);

  // Pango has no cap/x-height metrics; typical Latin proportions of the
  // ascender stand in (RN uses them for text effects, not layout).
  const float capRatio = 0.7f, xRatio = 0.5f;

  LinesMeasurements lines;
  PangoLayoutIter* iter = pango_layout_get_iter(layout);
  do {
    PangoLayoutLine* line = pango_layout_iter_get_line_readonly(iter);
    PangoRectangle logical;
    pango_layout_iter_get_line_extents(iter, nullptr, &logical);
    float baseline = float(pango_layout_iter_get_baseline(iter)) / PANGO_SCALE;
    float y = float(logical.y) / PANGO_SCALE;
    float height = float(logical.height) / PANGO_SCALE;
    float ascender = baseline - y;
    float descender = height - ascender;
    std::string lineText(text + line->start_index, size_t(line->length));
    lines.emplace_back(
        lineText,
        Rect{
            .origin = {.x = float(logical.x) / PANGO_SCALE, .y = y},
            .size = {.width = float(logical.width) / PANGO_SCALE,
                     .height = height}},
        descender,
        ascender * capRatio,
        ascender,
        ascender * xRatio);
  } while (pango_layout_iter_next_line(iter));
  pango_layout_iter_free(iter);
  g_object_unref(layout);
  return lines;
}

} // namespace facebook::react
