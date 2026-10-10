# @curiosity26/react-native-gtk4-gesture-handler

The Linux (GTK4) side of [`react-native-gesture-handler`](https://www.npmjs.com/package/react-native-gesture-handler) 3 for
[@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4).

```sh
npm install react-native-gesture-handler @curiosity26/react-native-gtk4-gesture-handler
npx react-native run-linux
```

- The `RNGestureHandlerModule` TurboModule and the `RNGestureHandlerDetector`
  component: the hook API (`useTapGesture`, `usePanGesture`... on a
  `GestureDetector`), the builder API (`Gesture.Tap()`...) and the handler
  components (`TapGestureHandler`...) all work, with the library's own
  JavaScript.
- The recognizers are a C++ port of the library's web implementation
  (itself a port of Android's): the same state machine, configs
  (`numberOfTaps`, `maxDelay`, `minDuration`, `activeOffsetX`,
  `failOffsetY`, `minVelocity`, `direction`, `hitSlop`,
  `shouldCancelWhenOutside`, `manualActivation`...) and orchestrator
  (`simultaneousWithExternalGesture`, `requireExternalGestureToFail`,
  `blocksExternalGesture`, and the composed gestures). They run on the GTK
  main thread on the host's pointer input (mouse and touchscreen), not on
  the JS thread.
- Gestures: Tap (and double tap), LongPress, Pan (and two-finger touchpad
  scrolling with `enableTrackpadTwoFingerGesture`), Fling, Hover, Manual
  (`GestureStateManager` from JS), Pinch and Rotation (two fingers on a
  touchscreen, or a touchpad pinch: GTK's touchpad gesture events, which
  give both at once), Native.
- Buttons: `RNGestureHandlerButton` with its native gesture, so
  `RectButton`, `BorderlessButton` and `Touchable` work: Touchable's own
  gesture (under its `handlerTag`, for relations) sends its press, long
  press and hover events, and the press and hover feedback
  (`activeOpacity`, `activeScale`, `activeUnderlayOpacity` with
  `underlayColor`, the hover ones) animates on GTK's frame clock without a
  React commit.
- The library's `ScrollView`, `FlatList`, `Switch`...: their native gesture
  activates when the view takes over (a ScrollView when it scrolls), and
  cancels the gestures it isn't simultaneous with.
- `ReanimatedSwipeable` and `ReanimatedDrawerLayout`, with
  react-native-reanimated and its Linux port.
- A gesture that activates takes the touches from React Native's
  responder (Pressable, ScrollView): their press is cancelled.
- Events: callbacks on the JS thread, or with react-native-reanimated
  (and its Linux port), worklets on the UI runtime, in the same event as
  the input; `GestureStateManager` works in worklets. As on iOS and
  Android, with Reanimated installed callbacks are worklets unless the
  gesture says `runOnJS: true`.

The Showcase's Gestures page has all of them; `rn-gtk-host --module
GalleryGestures --self-test` drives each with the mouse, touch points and a
touchpad pinch.

`run-linux` and `package-linux` autolink it. See
[docs/libraries.md](../../docs/libraries.md).
