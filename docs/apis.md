# APIs

React Native's JS APIs as the GTK host implements them: Appearance,
PlatformColor, AccessibilityInfo, Linking, AppState, Clipboard,
PixelRatio, I18nManager, Share, Vibration, Alert. Then the Linux APIs
React Native has none for, which `@curiosity26/react-native-gtk4` exports:
[Dialogs](#dialogs), [MenuBar](#menubar) (and `ContextMenu`, in
[components.md](components.md#context-menus)).

Components are in [components.md](components.md).

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

## AccessibilityInfo

React Native's AccessibilityInfo calls iOS's `AccessibilityManager` module
on every platform but Android; the host provides one in that shape
(`linux/src/AccessibilityInfo.cc`).

| API | Linux |
| --- | --- |
| `isScreenReaderEnabled()`, `screenReaderChanged` | the AT-SPI bus's `org.a11y.Status` `ScreenReaderEnabled`, which GNOME sets while Orca runs; followed live |
| `isReduceMotionEnabled()`, `reduceMotionChanged` | GTK's `gtk-enable-animations` off (GNOME's Reduce Animation) |
| `announceForAccessibility(text)` | `gtk_accessible_announce`, medium priority |
| `announceForAccessibilityWithOptions(text, {priority})` | `'high'` / `'low'` set the priority; `queue` has no GTK equivalent |
| `setAccessibilityFocus(reactTag)` | the view takes keyboard focus, which is what Orca follows (GTK has no separate screen reader focus); a view that isn't focusable is for that once |
| `sendAccessibilityEvent(ref, 'focus')` | Not yet: ReactCxxPlatform drops Fabric's accessibility events; use `setAccessibilityFocus` |
| `isBoldTextEnabled`, `isGrayscaleEnabled`, `isInvertColorsEnabled`, `isReduceTransparencyEnabled`, `isDarkerSystemColorsEnabled` | `false` |

`GalleryAccessibility --self-test` checks them (the screen reader as if one
started, reduce motion by flipping GTK's setting, announcements and focus);
add `--system-accessibility` to flip the AT-SPI bus's `ScreenReaderEnabled`
for real (Orca isn't started) and restore it.

## Linking

The host's `LinkingManager` module (iOS's shape; `linux/src/PlatformModules.cc`).

| API | Linux |
| --- | --- |
| `openURL(url)` | the desktop's default handler for the scheme (GIO, with GDK's launch context, so the handler gets focus on Wayland); rejects when there is none. In a Flatpak or Snap, GtkUriLauncher and the OpenURI portal |
| `canOpenURL(url)` | whether the desktop has a default handler for the scheme (`file:` URLs: whether the file exists) |
| `getInitialURL()` | the URL the app was started with: `myapp 'myapp://open?id=7'`, or a `.desktop` file's `Exec=myapp %u` (with `MimeType=x-scheme-handler/myapp;`), exactly as given |
| `addEventListener('url')` | URLs passed to the app while it runs: `rngtk::runApp` apps are unique GApplications, so a second launch with a URL (or a click on a link the app handles) hands it to the running one |
| `openSettings()` | rejects: Linux apps have no settings page |
| `sendIntent` | Android only |

## AppState

| State | When |
| --- | --- |
| `active` | the window is the active window |
| `inactive` | another window is active |
| `background` | the window is minimized (X11) or suspended (Wayland compositors that report it, when it isn't visible); Wayland can't tell minimized otherwise |

`change` events follow, and Android's `focus` / `blur` events fire when
the window gains or loses activation. `memoryWarning` never fires.

## Clipboard

`Clipboard.getString()` / `setString()` (React Native's deprecated core
module): GDK's clipboard. `getString()` resolves `''` when the clipboard
holds no text.

## PixelRatio and font scale

`PixelRatio.get()` is the monitor's scale. `PixelRatio.getFontScale()` and
`Dimensions`' `fontScale` follow GNOME's text scaling (Settings >
Accessibility > Large Text, `text-scaling-factor`), which reaches GTK as
`gtk-xft-dpi`: when it changes, `Dimensions` emits `change` and text lays
out again. Text and TextInput scale their font by it unless
`allowFontScaling={false}`, capped by `maxFontSizeMultiplier`, as on
Android.

## I18nManager

`isRTL` comes from the locale (GTK's default direction, from its
translations), unless `allowRTL(false)` or `forceRTL(true)`. Those and
`swapLeftAndRightInRTL` are saved in
`$XDG_CONFIG_HOME/react-native-gtk4/<appId>/i18n.ini` and apply at the
next reload or start, as on iOS. Right to left, Yoga lays out mirrored
(rows run right to left) and GTK's own widgets switch direction.
`localeIdentifier` is the first of GLib's language names (`en_US`).

## Share and Vibration

Linux desktops have neither. `Share.share()` (an override of
`Libraries/Share/Share`, which rejects other platforms) checks its
arguments and resolves `{action: 'dismissedAction'}`. `Vibration` accepts
every call and does nothing.

## Alert

`Alert.alert` and `Alert.prompt` show a GTK message dialog (the one
GtkAlertDialog shows), modal over the app's active window: the main
window, a Modal's, or another app window. `Alert.linux.js` replaces
React Native's Alert and takes iOS's and Android's options together.

| | Status | Notes |
| --- | --- | --- |
| `title`, `message` | Supported | the heading and the text under it; a message alone is the heading |
| `buttons` | Supported | any number, in order, with the `cancel` one first (GNOME puts it on the left). `destructive` is red; `isPreferred`, or else the last plain button, is the default: suggested-action, and Enter presses it. None given: one OK. Buttons without `text` say GTK's own (translated) OK or Cancel |
| Escape, the close button | Supported | press the `cancel` button. With none, `options.cancelable` lets them dismiss the alert, calling `options.onDismiss` (Android's meaning); otherwise the alert stays open |
| `Alert.prompt` types | Supported | `plain-text`, `secure-text` (a password field with a peek icon), `login-password` (onPress gets `{login, password}`), `default`; `defaultValue`; `keyboardType` sets the field's input purpose (email, number, phone, URL) |
| `userInterfaceStyle` | Not applicable | alerts follow the app's style (Appearance) |

Screen readers get an alert dialog (`GTK_ACCESSIBLE_ROLE_ALERT_DIALOG`)
named by the title, with the message as its description, which Orca reads
when it opens. `RCTAlertManager.alertWithArgs` (iOS's shape) shows the same
dialog, for code that calls it directly.

## Dialogs

File dialogs, from `@curiosity26/react-native-gtk4`, on GtkFileDialog: the
desktop's file chooser through its portal when there is one (GNOME's, and
always under Flatpak), GTK's own otherwise. They are modal over the app's
active window.

```js
import {Dialogs} from '@curiosity26/react-native-gtk4';

const [path] = await Dialogs.openFile({
  title: 'Open an image',
  filters: [
    {name: 'Images', mimeTypes: ['image/*']},
    {name: 'Text', extensions: ['txt', 'md']},
  ],
});
const paths = await Dialogs.openFile({multiple: true, defaultPath: '/home/me/Documents'});
const target = await Dialogs.saveFile({defaultName: 'notes.txt', buttonLabel: 'Export'});
const [folder] = await Dialogs.openFolder();
```

| Method | Resolves to | Options |
| --- | --- | --- |
| `openFile(options)` | the paths picked, `[]` if cancelled | `multiple` |
| `saveFile(options)` | the path, `null` if cancelled | `defaultName`; GTK asks before replacing a file |
| `openFolder(options)` | the folders picked, `[]` if cancelled | `multiple` |

Every method takes `title`, `buttonLabel` (the accept button), `defaultPath`
(a folder to start in, or a file to select or to save as) and `filters`
(`{name, extensions, mimeTypes, patterns}`; the first is selected). Paths
are local paths; a file without one (a remote location) comes back as its
URI. Other failures reject.

## MenuBar

The app's menu bar, from `@curiosity26/react-native-gtk4`: menus of items
(the same items as [context menus](components.md#context-menus)) that
GtkApplicationWindow shows under the title bar. Item shortcuts are the
app's accelerators (`gtk_application_set_accels_for_action`): they work
while the menus are closed, in every app window.

```js
import {MenuBar} from '@curiosity26/react-native-gtk4';

useEffect(() => {
  MenuBar.setMenu([
    {title: 'File', items: [
      {title: 'New', shortcut: 'Ctrl+N', onSelect: newDocument},
      {title: 'Open…', shortcut: 'Ctrl+O', onSelect: open},
      {type: 'separator'},
      {title: 'Quit', shortcut: 'Ctrl+Q', onSelect: quit},
    ]},
    {title: 'View', items: [
      {title: 'Sidebar', checked: sidebar, shortcut: 'F9', onSelect: toggleSidebar},
    ]},
  ]);
}, [sidebar]);
```

`setMenu` replaces the whole bar (call it again when a checkbox, radio item
or `disabled` changes); `MenuBar.clear()` removes it. A reload clears the
bar the old JS set. `Menu.setMenuBar` / `Menu.clearMenuBar` are the same.
Desktops that show app menus themselves (`gtk-shell-shows-menubar`) take it
from GTK. `GalleryMenus` checks the bar's menus, that the accelerators are
registered, that Ctrl+N reaches New through the window's shortcuts with
the menus closed, and a checkbox's state.

## Testing

The `GalleryPlatform` page (`--module GalleryPlatform --self-test --url
rngtk-test:initial`, and again with `--rtl`) checks the initial URL, `url`
events, `canOpenURL` and `openURL` (against a handler for `rngtk-test:`
installed in the test's own XDG directories), AppState's changes and
focus/blur events, the clipboard, Vibration, Share, the font scale (by
changing `gtk-xft-dpi`; Text grows, `allowFontScaling={false}` doesn't)
and RTL layout, on Wayland and X11. GNOME's real text scaling and URLs
handed to a running app were checked by hand on both.

`--module GalleryDialogs --self-test` opens each kind of alert and answers
it (buttons, Escape, Enter, typing), checks the dialog (modal over the app,
an alert dialog, button order and styles) and what JS got; then the file
dialogs, picking files and folders in a folder it fills, and cancelling.
It answers GTK's own chooser (it sets `GDK_DEBUG=no-portals`): the portal's
runs in another process. The portal's chooser was checked by hand
(`GDK_DEBUG=portals` with `--step-delay`).

### Appearance and PlatformColor

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
