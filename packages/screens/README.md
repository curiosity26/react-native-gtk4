# @curiosity26/react-native-gtk4-screens

The Linux (GTK4) side of [`react-native-screens`](https://www.npmjs.com/package/react-native-screens) for
[@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4),
on [libadwaita](https://gnome.pages.gitlab.gnome.org/libadwaita/) 1.4 or later:
React Navigation's native-stack with GNOME's own navigation.

```sh
npm install react-native-screens @react-navigation/native @react-navigation/native-stack \
  @curiosity26/react-native-gtk4-screens
sudo apt install libadwaita-1-dev   # Fedora: libadwaita-devel
npx react-native run-linux
```

- `RNSScreenStack` is an `AdwNavigationView`: pushing and popping slide
  pages in and out, and the user goes back with the header's back button,
  a swipe (touchpad or touchscreen), Escape, Alt+Left or the mouse's back
  button. React Navigation's state follows (`onDismissed`).
- Each `RNSScreen` is a page with an `AdwHeaderBar` from the screen's
  header options: the title, its color and font, the background color,
  `headerTintColor`, `headerLeft`, `headerTitle` and `headerRight` (React
  views in the header bar), `headerShown: false`, `headerTransparent`,
  `headerBackVisible`, and `headerSearchBarOptions` (a `GtkSearchEntry`
  under the header). Large titles are ignored. Pages below the top one
  are unmapped: they stop drawing.
- `presentation: 'modal'` (and `fullScreenModal`, `containedModal`,
  `transparentModal`) slides up a layer of its own over the stack, with
  its own header; screens pushed after a modal go into its layer. Escape
  dismisses it. `formSheet` and `pageSheet` are a card over a dimmed
  backdrop, `sheetAllowedDetents` tall.
- `usePreventRemove` and `gestureEnabled: false` keep the user from going
  back natively; the back button asks React instead.
- The transition events (`transitionStart`, `transitionEnd`, focus and
  blur) and `useHeaderHeight()` work as on the other platforms.
- `RNSScreenContainer` (tabs and drawers with screens enabled) shows the
  active screens.

`run-linux` and `package-linux` autolink it, and the Metro config then
gives Linux the libraries' native JavaScript: react-native-screens with
Linux among its native platforms (this package's `overrides/`), and
native-stack's `NativeStackView.native`. Without this package, Linux gets
react-native-screens' and native-stack's web components instead (no
libadwaita needed). Native tabs (`createNativeBottomTabNavigator`) aren't
ported: React Navigation's JS bottom tabs work. libadwaita is linked into
the app only through this package: the rest of the host stays plain GTK,
and the desktop's GTK theme draws the header bars. See
[docs/libraries.md](../../docs/libraries.md).
