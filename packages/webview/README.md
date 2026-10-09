# @curiosity26/react-native-gtk4-webview

The Linux (GTK4) side of [`react-native-webview`](https://www.npmjs.com/package/react-native-webview)
for [@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4):
`RNCWebView` as a [WebKitGTK 6.0](https://webkitgtk.org/) web view. With this
package installed, the Metro config gives react-native-webview its iOS JS
on Linux, which drives the same native component.

```sh
sudo apt install libwebkitgtk-6.0-dev     # Fedora: sudo dnf install webkitgtk6.0-devel
npm install react-native-webview @curiosity26/react-native-gtk4-webview
npx react-native run-linux
```

Works:
- `source` as `{uri, headers}` or `{html, baseUrl}`.
- `injectedJavaScript` and `injectedJavaScriptBeforeContentLoaded`, run on
  every page load.
- `onMessage` with `window.ReactNativeWebView.postMessage`, and
  `postMessage`/`injectJavaScript` from React Native.
- `onLoadStart`, `onLoad`, `onLoadEnd`, `onLoadProgress`, `onError`,
  `onHttpError` and `onNavigationStateChange`.
- `onShouldStartLoadWithRequest`. The navigation waits for JS's answer, as
  on iOS.
- `onOpenWindow`, `userAgent`, `applicationNameForUserAgent`,
  `javaScriptEnabled`, `incognito`, `webviewDebuggingEnabled` (WebKit's
  inspector), `mediaPlaybackRequiresUserAction`.
- `goBack`, `goForward`, `reload`, `stopLoading`, `requestFocus`,
  `clearCache`.

Not yet: POST sources (`method`/`body`: WebKitGTK loads requests as GET),
`clearHistory`, file downloads, and the iOS-only scrolling and
content-inset props. See [docs/libraries.md](../../docs/libraries.md).
