# Gesture handler, Reanimated and Screens on Linux

The three libraries most React Native apps add after the basics, and what
it would take to have them on GTK4. Checked against
react-native-gesture-handler 3.3.0, react-native-reanimated 4.7.1 (with
react-native-worklets 0.13.0) and react-native-screens 4.29.0, in October
2026. None of them has a Linux port yet: they're the roadmap's Phase 6.

## react-native-screens: use its web JS now; a native port is optional

**Today.** `isNativePlatformSupported` is iOS, Android and Windows only
(`src/core.ts`), so on Linux `screensEnabled()` is false and its `Screen`
and `ScreenContainer` render plain views. React Navigation's JS stack
(`@react-navigation/stack`), tabs and drawer work on top of that. What
doesn't work is `@react-navigation/native-stack`, which needs the native
`RNSScreenStack`.

**Cheap step.** Screens ships `.web.tsx` variants of every component
(`ScreenStack.web.tsx`, `Screen.web.tsx`, `SearchBar.web.tsx`, ...), built
on views and React Native's own animations. Like safe-area-context's
Windows variants today, the Metro config's `JS_FALLBACK_PLATFORMS` can give
Linux the web variants, and native-stack then works as it does on the web.
That's about a day, mostly testing native-stack's options against the web
behaviour.

**Native port.** `RNSScreenStack` and `RNSScreen` as a container component
(the host's new `insertChild`/`removeChild` hooks), with GTK's own
transitions (GtkStack's slide and crossfade, or libadwaita's
AdwNavigationView, which also gives desktop back gestures and keyboard
navigation). The header config could map to an AdwHeaderBar. This is worth
it for apps that want to feel native on GNOME, not for parity. Estimate:
2 to 3 weeks.

## react-native-gesture-handler: a port on GTK's gesture controllers

**Today.** It doesn't load. Its native side is the `RNGestureHandlerModule`
TurboModule, a root view component (`RNGestureHandlerRootView`), a button
component, and on each platform a gesture orchestrator fed by the
platform's raw touches (Android's MotionEvents, iOS's
UIGestureRecognizers). Without the module, `GestureHandlerRootView` and the
gesture components throw.

**What a port needs.**
- The module's methods (`src/specs/NativeRNGestureHandlerModule.ts`:
  `createGestureHandler`, `attachGestureHandler`,
  `setGestureHandlerConfig`, `updateGestureHandlerConfig`,
  `configureRelations`, `dropGestureHandler`, `flushOperations`,
  `installUIRuntimeBindings`) and its events (`onGestureHandlerEvent`,
  `onGestureHandlerStateChange`), dispatched to the attached view's event
  emitter.
- An orchestrator: handler states (undetermined, began, active, end,
  failed, cancelled), `simultaneousHandlers`, `waitFor`, `blocksHandlers`.
  Android's (`GestureHandlerOrchestrator.kt`) is the one to follow, and the
  web implementation (`src/web/`, TypeScript on pointer events) is a
  readable reference.
- Recognizers. GTK 4 has controllers for most of them: `GtkGestureClick`
  (tap and double tap), `GtkGestureLongPress`, `GtkGestureDrag` (pan),
  `GtkGestureSwipe` (fling), `GtkGestureZoom` (pinch), `GtkGestureRotate`
  (rotation), plus hover through `GtkEventControllerMotion`. Each controller
  attaches to the view's widget. Touchpad pinch and rotate come for free.
  Native, Manual and the button component map onto the host's existing
  pointer handling.
- Coexisting with the host's pointer handler, which feeds React Native's
  responder system (Pressable, ScrollView). Active GTK gestures need to
  cancel the JS responder, as the native platforms do.

An alternative is the web implementation's TypeScript, fed by the host's
pointer events. Less native code, but it gives up GTK's gesture recognition
(touchpad gestures, kinetic thresholds), and it runs on the JS thread,
which is what the library exists to avoid. **Recommendation:** the native
port. Estimate: 3 to 4 weeks for Tap, Pan, LongPress, Fling, Pinch,
Rotation and Native, and `GestureDetector` with Reanimated.

## react-native-reanimated (with react-native-worklets): the most work, but mostly shared C++

**Today.** It doesn't load. Reanimated 4 needs react-native-worklets:
another JS runtime (a Hermes runtime for worklets, on the UI thread) and
their shared values. Its JS checks for both native modules at import.

**Why it's feasible.** The engine is C++ shared by Android and iOS:
`Common/cpp/worklets` (worklet runtimes, the UI scheduler, shared items)
and `Common/cpp/reanimated` (animated props, layout animations, CSS
animations and transitions, Fabric integration). Each platform adds only a
thin layer: creating a Hermes runtime, a UI-thread scheduler, a frame
callback, and access to the Fabric UIManager. Our host is the same Fabric
C++ (ReactCxxPlatform), so the shared code should mostly compile as is.

**What a port needs.**
- Linux glue for worklets: a `WorkletsModule` that makes the UI runtime
  with the host's Hermes, a scheduler that runs on GTK's main thread
  (`g_main_context_invoke`), and the JS-thread invoker the host already
  gives TurboModules.
- Linux glue for Reanimated: frame callbacks from the window's
  `GdkFrameClock` (`gtk_widget_add_tick_callback`), and synchronous prop
  updates. Those need the host's UIManager or `UIManagerCommitHook`s, and
  a way to apply non-layout props (opacity, transform, colors) to widgets
  directly, without a commit. That is how Reanimated gets 60 fps on the
  other platforms.
- SDK access the host doesn't give libraries yet: the UIManager, the
  runtime executor, and a Hermes runtime factory. That's new API in
  `rngtk/Extensions.h` (a `Package` callback with a host context).
- The Babel plugin (`react-native-worklets/plugin`) already works with
  Metro on Linux.

**Recommendation:** port it after gesture-handler, since the two are
mostly used together. Start with worklets on the UI thread and
`useAnimatedStyle` with `withTiming`/`withSpring`, then layout animations
and CSS transitions. Estimate: 4 to 6 weeks, most of it the host API and
the direct-prop fast path. Until then, React Native's `Animated` with
`useNativeDriver: false` works (the Showcase's Animation page).

## Summary

| Library | Linux today | Next step | Estimate |
| --- | --- | --- | --- |
| react-native-screens | JS views; native-stack doesn't work | Web variants through Metro (native-stack as on the web) | 1 day; a native port (AdwNavigationView) 2 to 3 weeks |
| react-native-gesture-handler | Doesn't load | Native port on GTK's gesture controllers | 3 to 4 weeks |
| react-native-reanimated + worklets | Doesn't load | Linux glue for the shared C++, plus host API for UIManager access and direct props | 4 to 6 weeks |
