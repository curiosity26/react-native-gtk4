// The GTK host's view props (linux/CMakeLists.txt puts this directory ahead
// of ReactCommon's platform/cxx, whose HostPlatformViewProps is
// BaseViewProps alone): keyboard focus and keys, spelled as in
// react-native-windows and react-native-macos.
#pragma once

#include <react/renderer/components/view/BaseViewProps.h>
#include <react/renderer/core/PropsParserContext.h>

#include <optional>
#include <string>
#include <vector>

namespace facebook::react {

// An entry of keyDownEvents / keyUpEvents: a key the view handles itself,
// so GTK's own handling (Tab moving focus, Space activating) doesn't run.
// Matched by W3C `key` (react-native-macos) or `code`
// (react-native-windows); a modifier given must match, one left out
// matches either way.
struct HandledKeyEvent {
  std::string key;
  std::string code;
  std::optional<bool> altKey, ctrlKey, metaKey, shiftKey;
  bool operator==(const HandledKeyEvent &) const = default;
};

void fromRawValue(const PropsParserContext &context, const RawValue &value, HandledKeyEvent &result);

class HostPlatformViewProps : public BaseViewProps {
 public:
  HostPlatformViewProps() = default;
  HostPlatformViewProps(
      const PropsParserContext &context,
      const HostPlatformViewProps &sourceProps,
      const RawProps &rawProps,
      const std::function<bool(const std::string &)> &filterObjectKeys = nullptr);

  void setProp(
      const PropsParserContext &context,
      RawPropsPropNameHash hash,
      const char *propName,
      const RawValue &value);

  // Tab reaches it, and a click focuses it (View's `focusable` and
  // `tabIndex`; Pressable and the Touchables set it).
  bool focusable{false};
  // react-native-macos: draw the focus ring when focused by keyboard.
  bool enableFocusRing{true};
  // Takes keyboard focus once mounted.
  bool autoFocus{false};
  std::vector<HandledKeyEvent> keyDownEvents{};
  std::vector<HandledKeyEvent> keyUpEvents{};
};

} // namespace facebook::react
