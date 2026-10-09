// The GTK host's paragraph props (linux/CMakeLists.txt puts this directory
// ahead of ReactCommon's platform/cxx, whose HostPlatformParagraphProps is
// BaseParagraphProps alone): adds Android's selectionColor, the selected
// text's highlight.
#pragma once

#include <react/renderer/components/text/BaseParagraphProps.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/graphics/Color.h>

#include <optional>

namespace facebook::react {

class HostPlatformParagraphProps : public BaseParagraphProps {
 public:
  HostPlatformParagraphProps() = default;
  HostPlatformParagraphProps(
      const PropsParserContext &context,
      const HostPlatformParagraphProps &sourceProps,
      const RawProps &rawProps);

  void setProp(
      const PropsParserContext &context,
      RawPropsPropNameHash hash,
      const char *propName,
      const RawValue &value);

  std::optional<SharedColor> selectionColor{};
};

} // namespace facebook::react
