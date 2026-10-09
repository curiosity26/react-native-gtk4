#include "HostPlatformParagraphProps.h"

#include <react/renderer/core/graphicsConversions.h>
#include <react/renderer/core/propsConversions.h>

namespace facebook::react {

HostPlatformParagraphProps::HostPlatformParagraphProps(
    const PropsParserContext &context,
    const HostPlatformParagraphProps &sourceProps,
    const RawProps &rawProps)
    : BaseParagraphProps(context, sourceProps, rawProps),
      selectionColor(convertRawProp(
          context,
          rawProps,
          "selectionColor",
          sourceProps.selectionColor,
          {})) {}

void HostPlatformParagraphProps::setProp(
    const PropsParserContext &context,
    RawPropsPropNameHash hash,
    const char *propName,
    const RawValue &value) {
  // As every Props::setProp must: the base first, unconditionally.
  BaseParagraphProps::setProp(context, hash, propName, value);
  static auto defaults = HostPlatformParagraphProps{};
  switch (hash) {
    RAW_SET_PROP_SWITCH_CASE_BASIC(selectionColor);
  }
}

} // namespace facebook::react
