# @curiosity26/react-native-gtk4-reanimated

The Linux (GTK4) side of [`react-native-reanimated`](https://www.npmjs.com/package/react-native-reanimated) 4.7 for
[@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4):
the library's own C++ engine, built from its package in your app's
`node_modules`, and its `ReanimatedModule`, on
[react-native-worklets](../worklets)' UI runtime.

```sh
npm install react-native-reanimated react-native-worklets \
  @curiosity26/react-native-gtk4-reanimated @curiosity26/react-native-gtk4-worklets
```

With `react-native-worklets/plugin` in `babel.config.js` (see the worklets
port).

- Shared values and animated styles run on the UI runtime, on GTK's main
  thread: `withTiming`, `withSpring`, `withDecay`, `withRepeat`,
  `withSequence`, `withDelay`, frame by frame on the main window's
  `GdkFrameClock`. Each frame's props are committed to Fabric's shadow
  tree from the main thread, which mounts them there at once: animations
  keep running while the JS thread is busy, and React commits keep the
  animated values (the library's commit hook).
- Every Fabric event reaches the engine before JS, for
  `useAnimatedScrollHandler` and `useEvent`. `measure`, `scrollTo`,
  `useFrameCallback`, `useAnimatedReaction` and `useDerivedValue` work as
  on the other platforms.
- react-native-gesture-handler (its Linux port) drives animations from
  worklet gesture callbacks, and `GestureStateManager` in worklets changes
  gesture states. With Reanimated installed, gesture callbacks that set
  React state need `runOnJS: true`, as on iOS and Android.
- Layout animations: entering and exiting animations (the presets,
  keyframes, custom ones) and layout transitions, with exiting views kept
  mounted until they finish. As on the other platforms, a view with an
  entering animation must not set `nativeID` (Reanimated finds the
  animation by its own); put an ID on a child.
- CSS animations (`animationName` with keyframes, durations, delays,
  iteration counts, directions, fill modes, timing functions) and CSS
  transitions (`transitionProperty`, ...), with their `onCSSAnimation*`
  and `onCSSTransition*` callbacks, on the engine's own C++ loop.
- Reduced motion follows GNOME's "Reduce animation" setting
  (`ReduceMotion.System`).
- Not on Linux: sensors (`useAnimatedSensor` gets no data), keyboard
  events, `:hover`/`:active` pseudo selectors in CSS animations, shared
  element transitions.

The library's C++ chooses between Android's and Apple's way in places
(synchronous prop updates, the mounted props layout animations read); Linux
takes Apple's: the build rewrites `__APPLE__` in a copy of the sources. Its
codegen headers (`rnreanimated`) are generated at configure time with the
app's React Native. See [docs/libraries.md](../../docs/libraries.md).
