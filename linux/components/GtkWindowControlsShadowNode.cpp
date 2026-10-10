#include "GtkWindowControlsShadowNode.h"

#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/propsConversions.h>

#include <atomic>

namespace facebook::react {

extern const char GtkWindowControlsComponentName[] = "RNGtkWindowControls";

GtkWindowControlsProps::GtkWindowControlsProps(const PropsParserContext &context,
                                               const GtkWindowControlsProps &sourceProps,
                                               const RawProps &rawProps)
    : ViewProps(context, sourceProps, rawProps),
      side(convertRawProp(context, rawProps, "side", sourceProps.side, {"end"})) {}

namespace {
// Replaced at start-up by the real sizes (Adwaita: one 46 x 46 close
// button at the end).
std::atomic<float> g_startWidth{0}, g_startHeight{0};
std::atomic<float> g_endWidth{46}, g_endHeight{46};
}  // namespace

void GtkWindowControlsShadowNode::setNativeSize(bool start, Size size) {
  (start ? g_startWidth : g_endWidth) = size.width;
  (start ? g_startHeight : g_endHeight) = size.height;
}

Size GtkWindowControlsShadowNode::measureContent(
    const LayoutContext & /*layoutContext*/,
    const LayoutConstraints &layoutConstraints) const {
  bool start = getConcreteProps().side == "start";
  return layoutConstraints.clamp(Size{.width = start ? g_startWidth : g_endWidth,
                                      .height = start ? g_startHeight : g_endHeight});
}

}  // namespace facebook::react
