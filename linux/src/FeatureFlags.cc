#include "FeatureFlags.h"

#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>

#include <memory>

using namespace facebook::react;

namespace rngtk {
namespace {

// (Not ReactNativeFeatureFlagsDynamicProvider: its lookups insert into a
// shared folly::dynamic, which races once the JS and main threads both read
// flags.)
class HostFeatureFlags : public ReactNativeFeatureFlagsDefaults {
 public:
  bool enableBridgelessArchitecture() override { return true; }
  bool cxxNativeAnimatedEnabled() override { return true; }
  // Pressable's onHoverIn/onHoverOut from W3C pointerenter/pointerleave,
  // which GtkPointerHandler sends for the mouse.
  bool shouldPressibilityUseW3CPointerEventsForHover() override { return true; }
  // ref.focus() / ref.blur() on any view, not just TextInput (ViewCommands
  // focus and blur; GtkMountingManager::focusCommand).
  bool enableImperativeFocus() override { return true; }
};

}  // namespace

void setUpFeatureFlags() {
  ReactNativeFeatureFlags::override(std::make_unique<HostFeatureFlags>());
}

}  // namespace rngtk
