// The GTK host's view traits (see HostPlatformViewProps.h): views that
// take keyboard focus or handle keys stay real widgets, never flattened.
#pragma once

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ShadowNodeTraits.h>

namespace facebook::react::HostPlatformViewTraitsInitializer {

inline bool formsStackingContext(const ViewProps & /*props*/)
{
  return false;
}

inline bool formsView(const ViewProps &props)
{
  return props.focusable || props.autoFocus || !props.keyDownEvents.empty() || !props.keyUpEvents.empty();
}

inline bool isKeyboardFocusable(const ViewProps &props)
{
  return props.focusable;
}

} // namespace facebook::react::HostPlatformViewTraitsInitializer
