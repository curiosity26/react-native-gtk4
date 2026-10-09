#include "Appearance.h"

#include <rngtk/PlatformColors.h>

#include <glog/logging.h>

#include <cctype>
#include <cmath>

namespace rngtk {

namespace {

constexpr const char *kPortalName = "org.freedesktop.portal.Desktop";
constexpr const char *kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char *kSettingsIface = "org.freedesktop.portal.Settings";
constexpr const char *kAppearanceNs = "org.freedesktop.appearance";

// Where a theme name marks its dark variant: "Adwaita:dark", "Yaru-dark",
// "Mint-Y-Dark-Aqua". npos if it doesn't.
size_t darkMarker(const std::string &theme, size_t *length) {
  std::string lower;
  for (char c : theme) lower += char(std::tolower(static_cast<unsigned char>(c)));
  for (const char *marker : {":dark", "-dark"}) {
    size_t at = lower.find(marker);
    while (at != std::string::npos) {
      size_t end = at + 5;
      if (end == lower.size() || lower[end] == '-') {
        *length = 5;
        return at;
      }
      at = lower.find(marker, at + 1);
    }
  }
  return std::string::npos;
}

bool isDarkTheme(const std::string &theme) {
  size_t length;
  return darkMarker(theme, &length) != std::string::npos;
}

std::string lightVariant(const std::string &theme) {
  size_t length;
  size_t at = darkMarker(theme, &length);
  if (at == std::string::npos) return theme;
  return theme.substr(0, at) + theme.substr(at + length);
}

std::string themeName(GtkSettings *settings) {
  gchar *name = nullptr;
  g_object_get(settings, "gtk-theme-name", &name, nullptr);
  std::string s = name ? name : "";
  g_free(name);
  return s;
}

// A portal value comes as a variant, inside another one from Read().
GVariant *unwrap(GVariant *value) {
  GVariant *v = g_variant_ref(value);
  while (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
    GVariant *inner = g_variant_get_variant(v);
    g_variant_unref(v);
    v = inner;
  }
  return v;
}

}  // namespace

Appearance::Scheme Appearance::parseScheme(const std::string &name) {
  if (name == "dark") return Scheme::Dark;
  if (name == "light") return Scheme::Light;
  return Scheme::Unspecified;  // 'unspecified', 'auto'
}

namespace {

// GTK 4.20 replaced gtk-application-prefer-dark-theme with
// gtk-interface-color-scheme (GtkInterfaceColorScheme: 1 default, 2 dark,
// 3 light) and warns about the old one; the host builds against 4.14 and
// runs on either.
bool hasColorScheme(GtkSettings *settings) {
  return g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
                                      "gtk-interface-color-scheme") != nullptr;
}

bool settingsPreferDark(GtkSettings *settings) {
  if (hasColorScheme(settings)) {
    int scheme = 0;
    g_object_get(settings, "gtk-interface-color-scheme", &scheme, nullptr);
    return scheme == 2;
  }
  gboolean preferDark = FALSE;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark,
               nullptr);
  return preferDark;
}

void setSettingsDark(GtkSettings *settings, bool dark) {
  if (hasColorScheme(settings)) {
    g_object_set(settings, "gtk-interface-color-scheme", dark ? 2 : 3,
                 nullptr);
  } else {
    g_object_set(settings, "gtk-application-prefer-dark-theme", dark,
                 nullptr);
  }
}

}  // namespace

Appearance::Appearance(GdkDisplay *display, bool followSystem,
                       OnChange onChange)
    : settings_(gtk_settings_get_for_display(display)),
      onChange_(std::move(onChange)),
      followSystem_(followSystem) {
  systemTheme_ = themeName(settings_);
  // The user's own setting (settings.ini, XSettings), before we set it.
  userPreferDark_ = settingsPreferDark(settings_);
  if (followSystem) {
    GError *error = nullptr;
    bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus_) {
      LOG(INFO) << "Appearance: no session bus: " << error->message;
      g_clear_error(&error);
    }
    if (bus_ && readPortal()) {
      source_ = "portal";
      portalSignal_ = g_dbus_connection_signal_subscribe(
          bus_, kPortalName, kSettingsIface, "SettingChanged", kPortalPath,
          kAppearanceNs, G_DBUS_SIGNAL_FLAGS_NONE, onPortalSignal, this,
          nullptr);
    } else {
      source_ = "gtk-settings";
    }
    updateSystem();
  }
  // The desktop changes the theme (Ubuntu's dark style picks Yaru-dark).
  themeHandler_ = g_signal_connect(settings_, "notify::gtk-theme-name",
                                   G_CALLBACK(onThemeName), this);
  apply(false);
}

Appearance::~Appearance() {
  if (portalSignal_) g_dbus_connection_signal_unsubscribe(bus_, portalSignal_);
  if (themeHandler_) g_signal_handler_disconnect(settings_, themeHandler_);
  g_clear_object(&bus_);
}

// The portal's color-scheme (0 no preference, 1 dark, 2 light) and
// accent-color ((ddd) sRGB, out of range when unset). ReadOne is version
// 2 of the interface; Read (version 1) wraps the value once more.
bool Appearance::readPortal() {
  bool found = false;
  for (const char *key : {"color-scheme", "accent-color"}) {
    GVariant *reply = nullptr;
    for (const char *method : {"ReadOne", "Read"}) {
      GError *error = nullptr;
      reply = g_dbus_connection_call_sync(
          bus_, kPortalName, kPortalPath, kSettingsIface, method,
          g_variant_new("(ss)", kAppearanceNs, key), G_VARIANT_TYPE("(v)"),
          G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, &error);
      if (reply) break;
      LOG(INFO) << "Appearance: portal " << method << "(" << key
                << "): " << error->message;
      bool unknownMethod =
          g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD);
      g_clear_error(&error);
      if (!unknownMethod) break;
    }
    if (!reply) continue;
    GVariant *value = nullptr;
    g_variant_get(reply, "(v)", &value);
    applyPortalValue(key, value);
    g_variant_unref(value);
    g_variant_unref(reply);
    if (std::string(key) == "color-scheme") found = true;
  }
  return found;
}

void Appearance::applyPortalValue(const char *key, GVariant *value) {
  GVariant *v = unwrap(value);
  std::string name = key;
  if (name == "color-scheme" && g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) {
    portalScheme_ = int(g_variant_get_uint32(v));
    updateSystem();
  } else if (name == "accent-color" &&
             g_variant_is_of_type(v, G_VARIANT_TYPE("(ddd)"))) {
    double r, g, b;
    g_variant_get(v, "(ddd)", &r, &g, &b);
    auto in = [](double c) { return c >= 0 && c <= 1; };
    accent_ = in(r) && in(g) && in(b)
                  ? 0xFF000000u | uint32_t(std::lround(r * 255)) << 16 |
                        uint32_t(std::lround(g * 255)) << 8 |
                        uint32_t(std::lround(b * 255))
                  : 0;
  }
  g_variant_unref(v);
}

void Appearance::updateSystem() {
  if (!followSystem_) return;
  if (portalScheme_ >= 0) {
    // No preference (0): a dark theme still makes the desktop dark.
    systemDark_ = portalScheme_ == 1 ||
                  (portalScheme_ == 0 && isDarkTheme(systemTheme_));
  } else {
    systemDark_ = userPreferDark_ || isDarkTheme(systemTheme_);
  }
}

void Appearance::onPortalSignal(GDBusConnection *, const char *, const char *,
                                const char *, const char *, GVariant *params,
                                gpointer data) {
  auto *self = static_cast<Appearance *>(data);
  const char *ns = nullptr, *key = nullptr;
  GVariant *value = nullptr;
  g_variant_get(params, "(&s&sv)", &ns, &key, &value);
  if (std::string(ns) == kAppearanceNs) {
    self->applyPortalValue(key, value);
    self->apply(true);
  }
  g_variant_unref(value);
}

void Appearance::onThemeName(GObject *, GParamSpec *, gpointer data) {
  auto *self = static_cast<Appearance *>(data);
  if (self->settingTheme_) return;  // our own swap
  self->systemTheme_ = themeName(self->settings_);
  self->swappedTheme_ = false;
  self->updateSystem();
  self->apply(true);
}

void Appearance::setOverride(Scheme scheme) {
  override_ = scheme;
  apply(true);
}

void Appearance::setSystemDark(bool dark) {
  systemDark_ = dark;
  apply(true);
}

void Appearance::apply(bool notify) {
  bool dark = override_ == Scheme::Unspecified ? systemDark_
                                               : override_ == Scheme::Dark;
  bool changed = !applied_ || dark != dark_ ||
                 accent_ != platform_colors::accent.load();
  applied_ = true;
  dark_ = dark;
  platform_colors::dark = dark;
  platform_colors::accent = accent_;

  // GTK's own widgets: the theme's dark variant, or its light one when
  // the desktop's theme is a dark theme and the app is light.
  setSettingsDark(settings_, dark);
  bool swap = !dark && isDarkTheme(systemTheme_);
  if (swap != swappedTheme_) {
    settingTheme_ = true;
    g_object_set(settings_, "gtk-theme-name",
                 (swap ? lightVariant(systemTheme_) : systemTheme_).c_str(),
                 nullptr);
    settingTheme_ = false;
    swappedTheme_ = swap;
  }
  if (changed && notify && onChange_) onChange_();
}

}  // namespace rngtk
