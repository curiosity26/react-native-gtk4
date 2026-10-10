// The shadow node for <WindowControls> (js/WindowControls.js, Fabric name
// "RNGtkWindowControls"): GtkWindowControls, the window's minimize,
// maximize and close buttons on one side of a title bar, as the desktop's
// gtk-decoration-layout setting places them. Measured at the buttons'
// natural size for the current theme and layout.
#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

#include <string>

namespace facebook::react {

extern const char GtkWindowControlsComponentName[];

class GtkWindowControlsProps final : public ViewProps {
 public:
  GtkWindowControlsProps() = default;
  GtkWindowControlsProps(const PropsParserContext &context,
                         const GtkWindowControlsProps &sourceProps, const RawProps &rawProps);

  // 'start' (the layout's left of the colon, on the left in left-to-right
  // locales) or 'end'.
  std::string side{"end"};
};

class GtkWindowControlsShadowNode final
    : public ConcreteViewShadowNode<GtkWindowControlsComponentName, GtkWindowControlsProps,
                                    ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  // Each side's size, measured on the main thread
  // (rngtk::measure_window_controls) and read here from any thread. A side
  // without buttons is 0 x 0.
  static void setNativeSize(bool start, Size size);

  Size measureContent(const LayoutContext &layoutContext,
                      const LayoutConstraints &layoutConstraints) const override;
};

using GtkWindowControlsComponentDescriptor =
    ConcreteComponentDescriptor<GtkWindowControlsShadowNode>;

}  // namespace facebook::react
