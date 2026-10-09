// PlatformColor on Linux: libadwaita's named colors, light and dark.
//
// PlatformColor('window_bg_color', ...) reaches the C++ color parser as
// Android-style resource_paths (overrides/.../PlatformColorValueTypes).
// The parser (PlatformColorParser.h next to this) stores the first name it
// knows as a *dynamic* color: a reserved Color value that names a palette
// entry. HostPlatformColor.h resolves it against the current scheme and
// accent whenever a color's components are read, so the same props draw
// light or dark; on a change the host re-applies the mounted views' props
// (GtkMountingManager::refreshColors) and nothing has to be re-committed.
//
// Header-only (inline state), since it is compiled into React Native's own
// targets through HostPlatformColor.h.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace rngtk::platform_colors {

// A dynamic color: alpha 1/255, red 'R', green 'N', blue the entry index.
constexpr int32_t kMarkerMask = int32_t(0xFFFFFF00u);
constexpr int32_t kMarker = int32_t(0x01524E00u);

enum class Kind : uint8_t { Fixed, AccentBg, Accent, AccentFg };

struct Entry {
  const char *name;
  uint32_t light, dark;  // 0xAARRGGBB
  Kind kind = Kind::Fixed;
};

// libadwaita 1.5's named colors (and GTK's legacy theme_* names), from
// its stylesheet; the accent ones follow the desktop's accent color when
// it has one.
inline constexpr Entry kEntries[] = {
    {"accent_bg_color", 0xFF3584E4, 0xFF3584E4, Kind::AccentBg},
    {"accent_fg_color", 0xFFFFFFFF, 0xFFFFFFFF, Kind::AccentFg},
    {"accent_color", 0xFF1C71D8, 0xFF78AEED, Kind::Accent},
    {"destructive_bg_color", 0xFFE01B24, 0xFFC01C28},
    {"destructive_fg_color", 0xFFFFFFFF, 0xFFFFFFFF},
    {"destructive_color", 0xFFC01C28, 0xFFFF7B63},
    {"success_bg_color", 0xFF2EC27E, 0xFF26A269},
    {"success_fg_color", 0xFFFFFFFF, 0xFFFFFFFF},
    {"success_color", 0xFF1B8553, 0xFF8FF0A4},
    {"warning_bg_color", 0xFFE5A50A, 0xFFCD9309},
    {"warning_fg_color", 0xCC000000, 0xCC000000},
    {"warning_color", 0xFF9C6E03, 0xFFF8E45C},
    {"error_bg_color", 0xFFE01B24, 0xFFC01C28},
    {"error_fg_color", 0xFFFFFFFF, 0xFFFFFFFF},
    {"error_color", 0xFFC01C28, 0xFFFF7B63},
    {"window_bg_color", 0xFFFAFAFA, 0xFF242424},
    {"window_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"view_bg_color", 0xFFFFFFFF, 0xFF1E1E1E},
    {"view_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"headerbar_bg_color", 0xFFEBEBEB, 0xFF303030},
    {"headerbar_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"headerbar_border_color", 0xCC000000, 0xFFFFFFFF},
    {"headerbar_backdrop_color", 0xFFFAFAFA, 0xFF242424},
    {"headerbar_shade_color", 0x1F000000, 0x5C000000},
    {"headerbar_darker_shade_color", 0x1F000000, 0xE6000000},
    {"sidebar_bg_color", 0xFFEBEBEB, 0xFF303030},
    {"sidebar_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"sidebar_backdrop_color", 0xFFF2F2F2, 0xFF2A2A2A},
    {"sidebar_shade_color", 0x12000000, 0x40000000},
    {"card_bg_color", 0xFFFFFFFF, 0x14FFFFFF},
    {"card_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"card_shade_color", 0x12000000, 0x5C000000},
    {"dialog_bg_color", 0xFFFAFAFA, 0xFF383838},
    {"dialog_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"popover_bg_color", 0xFFFFFFFF, 0xFF383838},
    {"popover_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"popover_shade_color", 0x12000000, 0x40000000},
    {"thumbnail_bg_color", 0xFFFFFFFF, 0xFF383838},
    {"thumbnail_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"shade_color", 0x12000000, 0x40000000},
    {"scrollbar_outline_color", 0xFFFFFFFF, 0x80000000},
    // GTK's own stylesheet (no libadwaita name).
    {"borders", 0xFFD5D0CC, 0xFF1B1B1B},
    {"unfocused_borders", 0xFFD5D0CC, 0xFF1B1B1B},
    {"insensitive_fg_color", 0xFF929595, 0xFF919190},
    {"insensitive_bg_color", 0xFFFAF9F8, 0xFF323232},
    {"theme_bg_color", 0xFFFAFAFA, 0xFF242424},
    {"theme_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"theme_base_color", 0xFFFFFFFF, 0xFF1E1E1E},
    {"theme_text_color", 0xCC000000, 0xFFFFFFFF},
    {"theme_selected_bg_color", 0xFF3584E4, 0xFF3584E4, Kind::AccentBg},
    {"theme_selected_fg_color", 0xFFFFFFFF, 0xFFFFFFFF, Kind::AccentFg},
    {"theme_unfocused_bg_color", 0xFFFAFAFA, 0xFF242424},
    {"theme_unfocused_fg_color", 0xCC000000, 0xFFFFFFFF},
    {"link_color", 0xFF1C71D8, 0xFF78AEED, Kind::Accent},
};
inline constexpr int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

// The current scheme and the desktop's accent (0: Adwaita's blue). Set on
// the GTK main thread; read wherever colors are (JS thread, main thread).
inline std::atomic<bool> dark{false};
inline std::atomic<uint32_t> accent{0};

// A name as PlatformColor gets it: "window_bg_color", also GTK CSS's
// "@window_bg_color" and libadwaita 1.6's "--window-bg-color".
inline int indexOf(std::string name) {
  if (name.rfind("--", 0) == 0) {
    name = name.substr(2);
  } else if (!name.empty() && name[0] == '@') {
    name = name.substr(1);
  }
  std::replace(name.begin(), name.end(), '-', '_');
  for (int i = 0; i < kEntryCount; i++) {
    if (name == kEntries[i].name) return i;
  }
  return -1;
}

inline bool isDynamic(int32_t color) {
  return (color & kMarkerMask) == kMarker && (color & 0xFF) < kEntryCount;
}

inline int32_t dynamicColor(int index) { return kMarker | index; }

namespace detail {

inline double toLinear(double c) {
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
inline double fromLinear(double c) {
  c = std::clamp(c, 0.0, 1.0);
  return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
}

// libadwaita 1.6's standalone accent: the accent's Oklab lightness capped
// at 0.5 on light backgrounds and raised to 0.85 on dark ones.
inline uint32_t standaloneAccent(uint32_t argb, bool isDark) {
  double r = toLinear(((argb >> 16) & 0xFF) / 255.0);
  double g = toLinear(((argb >> 8) & 0xFF) / 255.0);
  double b = toLinear((argb & 0xFF) / 255.0);
  double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
  double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
  double B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
  L = isDark ? std::max(L, 0.85) : std::min(L, 0.5);
  l = L + 0.3963377774 * A + 0.2158037573 * B;
  m = L - 0.1055613458 * A - 0.0638541728 * B;
  s = L - 0.0894841775 * A - 1.2914855480 * B;
  l = l * l * l;
  m = m * m * m;
  s = s * s * s;
  auto channel = [](double c) {
    return uint32_t(std::lround(fromLinear(c) * 255));
  };
  return 0xFF000000u |
         channel(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s) << 16 |
         channel(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s) << 8 |
         channel(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

}  // namespace detail

// The ARGB a dynamic color stands for now; other colors as they are.
inline int32_t resolve(int32_t color) {
  if (!isDynamic(color)) return color;
  const Entry &e = kEntries[color & 0xFF];
  bool isDark = dark.load(std::memory_order_relaxed);
  uint32_t custom = accent.load(std::memory_order_relaxed);
  if (custom) {
    if (e.kind == Kind::AccentBg) return int32_t(custom);
    if (e.kind == Kind::Accent) {
      return int32_t(detail::standaloneAccent(custom, isDark));
    }
  }
  return int32_t(isDark ? e.dark : e.light);
}

}  // namespace rngtk::platform_colors
