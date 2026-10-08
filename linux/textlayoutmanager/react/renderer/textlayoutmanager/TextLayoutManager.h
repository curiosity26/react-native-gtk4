// Same interface as React Native's platform/cxx TextLayoutManager; the
// implementation (TextLayoutManager.cpp) measures with Pango.
// Derived from React Native (MIT, Copyright (c) Meta Platforms, Inc.).
#pragma once

#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/textlayoutmanager/TextLayoutContext.h>
#include <react/renderer/textlayoutmanager/TextMeasureCache.h>
#include <react/utils/ContextContainer.h>
#include <memory>

namespace facebook::react {

class TextLayoutManager;

/*
 * Cross platform facade for text measurement (e.g. Android-specific
 * TextLayoutManager)
 */
class TextLayoutManager {
 public:
  explicit TextLayoutManager(const std::shared_ptr<const ContextContainer> &contextContainer);
  virtual ~TextLayoutManager() = default;

  /*
   * Not copyable.
   */
  TextLayoutManager(const TextLayoutManager &) = delete;
  TextLayoutManager &operator=(const TextLayoutManager &) = delete;

  /*
   * Not movable.
   */
  TextLayoutManager(TextLayoutManager &&) = delete;
  TextLayoutManager &operator=(TextLayoutManager &&) = delete;

  /*
   * Measures `attributedString` using native text rendering infrastructure.
   */
  virtual TextMeasurement measure(
      const AttributedStringBox &attributedStringBox,
      const ParagraphAttributes &paragraphAttributes,
      const TextLayoutContext &layoutContext,
      const LayoutConstraints &layoutConstraints) const;

  /*
   * Measures each line of the text laid out in `size` (TextInput's
   * baseline).
   */
  LinesMeasurements measureLines(
      const AttributedStringBox &attributedStringBox,
      const ParagraphAttributes &paragraphAttributes,
      const Size &size) const;

 protected:
  std::shared_ptr<const ContextContainer> contextContainer_;
  TextMeasureCache textMeasureCache_;
};

} // namespace facebook::react
