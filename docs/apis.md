# APIs: Appearance, PlatformColor

React Native's JS APIs as the GTK host implements them. Components are in
[components.md](components.md).

## Appearance and dark mode

`Appearance` and `useColorScheme()` work as on iOS and Android, backed by
the host's `Appearance` TurboModule (`linux/src/Appearance.cc`).

| API | Linux |
| --- | --- |
| `Appearance.getColorScheme()`, `useColorScheme()` | `'light'` or `'dark'`: the app's override if set, otherwise the desktop's style |
| `Appearance.setColorScheme('light' \| 'dark')` | overrides the desktop for this app, GTK's own widgets included |
| `Appearance.setColorScheme('unspecified')` (or `'auto'`) | follows the desktop again |
| `Appearance.addChangeListener` | fires for desktop changes and overrides alike (the `appearanceChanged` event) |

**The desktop's style** comes from the XDG Settings portal: the
`org.freedesktop.appearance` `color-scheme` key (GNOME, Cinnamon and KDE
serve it), followed live through the portal's `SettingChanged` signal.
"No preference" counts as dark when the GTK theme is a dark one (a name
with `-dark`, as Ubuntu's dark style picks `Yaru-dark`). Without a portal
the host reads GtkSettings: `gtk-application-prefer-dark-theme` or a dark
theme name.

**GTK's own widgets** (TextInput's caret and menus, Switch, the spinner,
scrollbars, the dev menu) follow the app's scheme: the host sets
`gtk-application-prefer-dark-theme` on the app's GtkSettings (GTK 4.14
doesn't read the portal's color-scheme itself). When the desktop's theme
is a dark theme and the app is light, the app uses the theme's light
variant (`Yaru-dark` → `Yaru`).

`Text` and `TextInput` keep React Native's default black text in both
schemes, as on iOS: give them a color, such as
`PlatformColor('window_fg_color')`, to follow dark mode.

## PlatformColor

`PlatformColor(name, ...fallbacks)` names libadwaita's colors. They
resolve from the host's own copy of Adwaita's light and dark palettes
(`linux/graphics/rngtk/PlatformColors.h`; no libadwaita needed) and
**follow the scheme**: when it changes, the mounted views repaint with the
other palette, without a re-render or commit (like iOS's dynamic colors).
The first name the palette knows wins; if none is known, black.

```js
const styles = StyleSheet.create({
  page: {backgroundColor: PlatformColor('window_bg_color')},
  card: {backgroundColor: PlatformColor('card_bg_color')},
  text: {color: PlatformColor('window_fg_color')},
  link: {color: PlatformColor('accent_color')},
});
```

| Names | |
| --- | --- |
| `window_bg_color`, `window_fg_color`, `view_bg_color`, `view_fg_color` | the window and content areas |
| `accent_bg_color`, `accent_fg_color`, `accent_color` | the accent (background, text on it, standalone text) |
| `destructive_*`, `success_*`, `warning_*`, `error_*` (`_bg_color`, `_fg_color`, `_color`) | status colors |
| `headerbar_bg_color`, `headerbar_fg_color`, `headerbar_border_color`, `headerbar_backdrop_color`, `headerbar_shade_color`, `headerbar_darker_shade_color` | header bars |
| `sidebar_bg_color`, `sidebar_fg_color`, `sidebar_backdrop_color`, `sidebar_shade_color` | sidebars |
| `card_bg_color`, `card_fg_color`, `card_shade_color` | cards (translucent in dark) |
| `dialog_bg_color`, `dialog_fg_color`, `popover_bg_color`, `popover_fg_color`, `popover_shade_color`, `thumbnail_bg_color`, `thumbnail_fg_color` | dialogs, popovers |
| `shade_color`, `scrollbar_outline_color` | shading |
| `borders`, `unfocused_borders`, `insensitive_fg_color`, `insensitive_bg_color`, `link_color` | GTK's own stylesheet |
| `theme_bg_color`, `theme_fg_color`, `theme_base_color`, `theme_text_color`, `theme_selected_bg_color`, `theme_selected_fg_color`, `theme_unfocused_bg_color`, `theme_unfocused_fg_color` | GTK 3's names |

GTK CSS's `@window_bg_color` and libadwaita 1.6's `--window-bg-color`
spellings work too. The accent colors follow the portal's `accent-color`
when the desktop sets one (GNOME 47 and later): `accent_bg_color` is that
color and `accent_color` is derived from it the way libadwaita 1.6 does
(its Oklab lightness capped for light and raised for dark backgrounds).
Without one they are Adwaita's blue.

### How it works

`PlatformColor()` reaches the C++ color parser as Android-style
`resource_paths`. The host replaces ReactCommon's cxx-platform
`PlatformColorParser.h` (which made every PlatformColor transparent) and
`HostPlatformColor.h` with its own (`linux/graphics/`, first on the
include path). The parser stores a reserved color value that names a
palette entry; the color accessors resolve it against the current scheme
wherever a color is read: view styles, text attributes, TextInput, Switch,
ActivityIndicator and Image tints. On a change the host re-applies the
mounted views' props (`GtkMountingManager::refreshColors`).

## Testing

`rn-gtk-host --module GalleryAppearance --self-test` switches the scheme
with `setColorScheme`, and as if the desktop changed, and checks
`useColorScheme`, `getColorScheme`, the swatches' pixels (both palettes,
fallbacks, unknown names, the alternative spellings), the dark Button, the
text color and GTK's dark variant. Self-tests otherwise treat the desktop
as light. Add `--system-appearance` to test the real thing: it flips
GNOME's `org.gnome.desktop.interface color-scheme` (what Settings >
Appearance writes), waits for the app to follow through the portal, and
restores the setting. Both pass on GNOME Wayland and X11.

The Showcase's Appearance page (`npm run dev:showcase`) has a
System / Light / Dark switch, a button that flips GNOME's dark style (a
harness-only `ShowcaseDesktop` module in `rn-gtk-host`), the named colors
and GTK widgets; the whole Showcase follows the scheme.
