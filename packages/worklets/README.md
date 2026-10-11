# @curiosity26/react-native-gtk4-worklets

The Linux (GTK4) side of [`react-native-worklets`](https://www.npmjs.com/package/react-native-worklets) 0.13 for
[@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4):
the library's own shared C++, built from its package in your app's
`node_modules`, on GTK's threads.

```sh
npm install react-native-worklets @curiosity26/react-native-gtk4-worklets
```

```js
// babel.config.js
module.exports = {
  presets: ['module:@react-native/babel-preset'],
  plugins: ['react-native-worklets/plugin'],
};
```

- The UI runtime (a Hermes runtime of its own) runs on GTK's main thread:
  `scheduleOnUI`/`runOnUI`, `runOnUISync`, `scheduleOnRN`/`runOnJS`,
  serializables, shareables and synchronizables, `requestAnimationFrame`
  on the main window's `GdkFrameClock`, `console` (to stderr).
  `createWorkletRuntime` makes more runtimes on threads of their own.
- react-native-gesture-handler's bindings go on the UI runtime
  (`GestureStateManager` in worklets), and Reanimated builds on it.
- Bundle mode (the whole bundle on worklet runtimes) and networking on
  worklet runtimes aren't supported yet.

The library's C++ assumes Apple wherever it isn't Android in one place
(naming threads): `linux/compat/` gives it what it expects. See
[docs/libraries.md](../../docs/libraries.md).
