#include "HostPlatformViewProps.h"

#include <react/renderer/core/propsConversions.h>

#include <unordered_map>

namespace facebook::react {

void fromRawValue(const PropsParserContext & /*context*/, const RawValue &value, HandledKeyEvent &result) {
  result = {};
  if (value.hasType<std::string>()) {
    // A bare key name.
    result.key = (std::string)value;
    return;
  }
  if (!value.hasType<std::unordered_map<std::string, RawValue>>()) {
    return;
  }
  auto map = (std::unordered_map<std::string, RawValue>)value;
  auto text = [&](const char *name, std::string &out) {
    auto it = map.find(name);
    if (it != map.end() && it->second.hasType<std::string>()) {
      out = (std::string)it->second;
    }
  };
  auto flag = [&](const char *name, std::optional<bool> &out) {
    auto it = map.find(name);
    if (it != map.end() && it->second.hasType<bool>()) {
      out = (bool)it->second;
    }
  };
  text("key", result.key);
  text("code", result.code);
  flag("altKey", result.altKey);
  flag("ctrlKey", result.ctrlKey);
  flag("metaKey", result.metaKey);
  flag("shiftKey", result.shiftKey);
}

HostPlatformViewProps::HostPlatformViewProps(
    const PropsParserContext &context,
    const HostPlatformViewProps &sourceProps,
    const RawProps &rawProps,
    const std::function<bool(const std::string &)> &filterObjectKeys)
    : BaseViewProps(context, sourceProps, rawProps, filterObjectKeys),
      focusable(convertRawProp(context, rawProps, "focusable", sourceProps.focusable, false)),
      enableFocusRing(convertRawProp(
          context,
          rawProps,
          "enableFocusRing",
          sourceProps.enableFocusRing,
          true)),
      autoFocus(convertRawProp(context, rawProps, "autoFocus", sourceProps.autoFocus, false)),
      keyDownEvents(convertRawProp(
          context,
          rawProps,
          "keyDownEvents",
          sourceProps.keyDownEvents,
          {})),
      keyUpEvents(convertRawProp(
          context,
          rawProps,
          "keyUpEvents",
          sourceProps.keyUpEvents,
          {})),
      tooltip(convertRawProp(context, rawProps, "tooltip", sourceProps.tooltip, {})),
      onMouseEnter(convertRawProp(
          context,
          rawProps,
          "onMouseEnter",
          sourceProps.onMouseEnter,
          false)),
      onMouseLeave(convertRawProp(
          context,
          rawProps,
          "onMouseLeave",
          sourceProps.onMouseLeave,
          false)),
      onAuxClick(convertRawProp(context, rawProps, "onAuxClick", sourceProps.onAuxClick, false)),
      onAuxClickCapture(convertRawProp(
          context,
          rawProps,
          "onAuxClickCapture",
          sourceProps.onAuxClickCapture,
          false)) {}

void HostPlatformViewProps::setProp(
    const PropsParserContext &context,
    RawPropsPropNameHash hash,
    const char *propName,
    const RawValue &value) {
  // As every Props::setProp must: the base first, unconditionally.
  BaseViewProps::setProp(context, hash, propName, value);
  static auto defaults = HostPlatformViewProps{};
  switch (hash) {
    RAW_SET_PROP_SWITCH_CASE_BASIC(focusable);
    RAW_SET_PROP_SWITCH_CASE_BASIC(enableFocusRing);
    RAW_SET_PROP_SWITCH_CASE_BASIC(autoFocus);
    RAW_SET_PROP_SWITCH_CASE_BASIC(keyDownEvents);
    RAW_SET_PROP_SWITCH_CASE_BASIC(keyUpEvents);
    RAW_SET_PROP_SWITCH_CASE_BASIC(tooltip);
    RAW_SET_PROP_SWITCH_CASE_BASIC(onMouseEnter);
    RAW_SET_PROP_SWITCH_CASE_BASIC(onMouseLeave);
    RAW_SET_PROP_SWITCH_CASE_BASIC(onAuxClick);
    RAW_SET_PROP_SWITCH_CASE_BASIC(onAuxClickCapture);
  }
}

} // namespace facebook::react
