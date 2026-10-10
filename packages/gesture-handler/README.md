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
  (`GestureStateManager` from JS).
- A gesture that activates takes the touches from React Native's
  responder (Pressable, ScrollView): their press is cancelled.
- Events: callbacks on the JS thread. Reanimated worklets need
  react-native-reanimated on Linux (not yet).

`run-linux` and `package-linux` autolink it. See
[docs/libraries.md](../../docs/libraries.md).
