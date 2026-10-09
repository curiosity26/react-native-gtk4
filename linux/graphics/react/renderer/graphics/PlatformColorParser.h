/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// The GTK host's PlatformColorParser.h (linux/CMakeLists.txt puts this
// directory first; ReactCommon's platform/cxx one parses every
// PlatformColor to transparent). PlatformColor(...names) arrives as
// {resource_paths: names}, like Android's. The first name in the palette
// (rngtk/PlatformColors.h) becomes a dynamic color that follows light and
// dark; if none is known, black. Anything else is no color, as upstream.

#pragma once

#include <react/renderer/core/RawValue.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/fromRawValueShared.h>
#include <react/utils/ContextContainer.h>
#include <rngtk/PlatformColors.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace facebook::react {

inline SharedColor
parsePlatformColor(const ContextContainer & /*contextContainer*/, int32_t /*surfaceId*/, const RawValue &value)
{
  if (value.hasType<std::unordered_map<std::string, std::vector<std::string>>>()) {
    auto map = (std::unordered_map<std::string, std::vector<std::string>>)value;
    for (const auto &name : map["resource_paths"]) {
      int index = rngtk::platform_colors::indexOf(name);
      if (index >= 0) {
        return {rngtk::platform_colors::dynamicColor(index)};
      }
    }
    return {hostPlatformColorFromRGBA(0, 0, 0, 255)};
  }
  return {HostPlatformColor::UndefinedColor};
}

inline void
fromRawValue(const ContextContainer &contextContainer, int32_t surfaceId, const RawValue &value, SharedColor &result)
{
  fromRawValueShared(contextContainer, surfaceId, value, result, parsePlatformColor);
}

} // namespace facebook::react
