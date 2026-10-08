// The shadow node for <Switch> (Fabric name "Switch", the iOS component
// Switch.js renders off Android): codegen'd props and events, measured at
// GtkSwitch's natural size. React Native's own Switch shadow node is
// Apple-only.
#pragma once

#include <react/renderer/components/FBReactNativeSpec/EventEmitters.h>
#include <react/renderer/components/FBReactNativeSpec/Props.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

namespace facebook::react {

extern const char GtkSwitchComponentName[];

class GtkSwitchShadowNode final
    : public ConcreteViewShadowNode<GtkSwitchComponentName, SwitchProps,
                                    SwitchEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  // GTK's switch size for the current theme, measured on the main thread
  // (rngtk::measure_native_controls) and read here from any thread.
  static void setNativeSize(Size size);

  Size measureContent(const LayoutContext &layoutContext,
                      const LayoutConstraints &layoutConstraints) const override;
};

using GtkSwitchComponentDescriptor =
    ConcreteComponentDescriptor<GtkSwitchShadowNode>;

}  // namespace facebook::react
