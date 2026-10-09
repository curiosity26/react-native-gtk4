# @curiosity26/react-native-gtk4-async-storage

The Linux (GTK4) side of [`@react-native-async-storage/async-storage`](https://www.npmjs.com/package/@react-native-async-storage/async-storage) for
[@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4):
the `RNAsyncStorage` TurboModule, with each database a JSON file under `$XDG_DATA_HOME/<app id>/async-storage/`.

```sh
npm install @react-native-async-storage/async-storage @curiosity26/react-native-gtk4-async-storage
npx react-native run-linux
```

`run-linux` and `package-linux` autolink it. The library's own JavaScript
is unchanged. See [docs/libraries.md](../../docs/libraries.md).
