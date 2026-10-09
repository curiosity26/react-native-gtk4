#include "GtkSwitchShadowNode.h"

#include <react/renderer/core/LayoutConstraints.h>

#include <atomic>

namespace facebook::react {

extern const char GtkSwitchComponentName[] = "Switch";

namespace {
// GtkSwitch in Adwaita is 48x26; replaced at start-up by the real size.
std::atomic<float> g_width{48};
std::atomic<float> g_height{26};
}  // namespace

void GtkSwitchShadowNode::setNativeSize(Size size) {
  g_width = size.width;
  g_height = size.height;
}

Size GtkSwitchShadowNode::measureContent(
    const LayoutContext & /*layoutContext*/,
    const LayoutConstraints &layoutConstraints) const {
  return layoutConstraints.clamp(Size{.width = g_width, .height = g_height});
}

}  // namespace facebook::react
