// The GTK host's view traits (see HostPlatformViewProps.h): views that
// take keyboard focus, handle keys, have a tooltip or listen for the mouse
// entering and leaving stay real widgets, never flattened.
#pragma once

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ShadowNodeTraits.h>

namespace facebook::react::HostPlatformViewTraitsInitializer {

// A stacking context keeps the children inside the view's widget (Fabric
// hoists a plain view's children into its nearest stacking context), so
// hovering a child is hovering the view, and keys from a focused child
// pass through it.
inline bool formsStackingContext(const ViewProps &props)
{
  return props.focusable || props.autoFocus || !props.keyDownEvents.empty() || !props.keyUpEvents.empty() ||
      !props.tooltip.empty() || props.onMouseEnter || props.onMouseLeave;
}

inline bool formsView(const ViewProps &props)
{
  return formsStackingContext(props);
}

inline bool isKeyboardFocusable(const ViewProps &props)
{
  return props.focusable;
}

} // namespace facebook::react::HostPlatformViewTraitsInitializer
