// Light and dark: what Appearance.getColorScheme() reports, what the
// PlatformColor palette resolves to, and which GTK theme variant the
// app's own widgets (TextInput, Switch, spinner, scrollbars, menus) use.
//
// The system's choice comes from the XDG Settings portal's
// org.freedesktop.appearance color-scheme (GNOME, Cinnamon, KDE serve it),
// followed through its SettingChanged signal, with its accent-color for
// the accent PlatformColors. Without a portal: GtkSettings'
// gtk-application-prefer-dark-theme, or a theme whose name ends in -dark.
// Appearance.setColorScheme('light' | 'dark') overrides it for the app.
//
// GTK 4.14 doesn't follow the portal's color-scheme itself, so the app's
// GtkSettings get gtk-application-prefer-dark-theme to match (and a
// "-dark" theme is swapped for its light variant while the app is light).
#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <string>

namespace rngtk {

class Appearance {
 public:
  enum class Scheme { Unspecified, Light, Dark };
  // Runs on the main thread after the effective scheme or accent changed.
  using OnChange = std::function<void()>;

  // followSystem false: the system is light (self-tests), with no portal.
  Appearance(GdkDisplay *display, bool followSystem, OnChange onChange);
  ~Appearance();
  Appearance(const Appearance &) = delete;
  Appearance &operator=(const Appearance &) = delete;

  // Any thread.
  bool isDark() const { return dark_; }
  static Scheme parseScheme(const std::string &name);

  // Main thread.
  void setOverride(Scheme scheme);
  Scheme override() const { return override_; }
  // What the system asks for, and where that came from: "portal",
  // "gtk-settings" or "none".
  bool systemIsDark() const { return systemDark_; }
  const char *systemSource() const { return source_; }
  // Tests: as if the system's color scheme changed.
  void setSystemDark(bool dark);

 private:
  static void onPortalSignal(GDBusConnection *, const char *sender,
                             const char *path, const char *iface,
                             const char *signal, GVariant *params,
                             gpointer self);
  static void onThemeName(GObject *, GParamSpec *, gpointer self);
  bool readPortal();
  void applyPortalValue(const char *key, GVariant *value);
  void updateSystem();
  void apply(bool notify);

  GtkSettings *settings_;
  OnChange onChange_;
  GDBusConnection *bus_{nullptr};
  guint portalSignal_{0};
  gulong themeHandler_{0};
  bool followSystem_;
  const char *source_ = "none";
  // The portal's color-scheme (-1: no portal) and the user's GTK setting.
  int portalScheme_{-1};
  bool userPreferDark_{false};
  bool systemDark_{false};
  Scheme override_{Scheme::Unspecified};
  // The theme the desktop set, and whether we replaced it with its light
  // variant.
  std::string systemTheme_;
  bool swappedTheme_{false};
  bool settingTheme_{false};
  bool applied_{false};
  std::atomic<bool> dark_{false};
  uint32_t accent_{0};
};

}  // namespace rngtk
