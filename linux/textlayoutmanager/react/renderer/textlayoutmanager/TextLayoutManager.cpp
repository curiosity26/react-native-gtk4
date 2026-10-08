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

} // namespace facebook::react
