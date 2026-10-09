// The "RNGtkExampleView" native component: a GtkCalendar.
// (src/ExampleViewNativeComponent.ts is its JS side.)
//
// A Fabric component needs its props, event emitter and shadow node in C++
// (what React Native's codegen writes for iOS and Android; written by hand
// here), and a GTK widget: rngtk::NativeComponent's create / update /
// command.
#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <rngtk/Extensions.h>

#include <string>

namespace example {

extern const char ExampleViewComponentName[];

class ExampleViewProps final : public facebook::react::ViewProps {
 public:
  ExampleViewProps() = default;
  ExampleViewProps(const facebook::react::PropsParserContext &context,
                   const ExampleViewProps &sourceProps,
                   const facebook::react::RawProps &rawProps);

  // "YYYY-MM-DD": the selected day.
  std::string date{};
  bool showWeekNumbers{false};
};

class ExampleViewEventEmitter : public facebook::react::ViewEventEmitter {
 public:
  using facebook::react::ViewEventEmitter::ViewEventEmitter;
  // onDateChange({date: 'YYYY-MM-DD'})
  void onDateChange(const std::string &date) const;
};

using ExampleViewShadowNode =
    facebook::react::ConcreteViewShadowNode<ExampleViewComponentName, ExampleViewProps,
                                            ExampleViewEventEmitter>;
using ExampleViewComponentDescriptor =
    facebook::react::ConcreteComponentDescriptor<ExampleViewShadowNode>;

// The component for rngtk::Package.
rngtk::NativeComponent exampleViewComponent();

}  // namespace example
