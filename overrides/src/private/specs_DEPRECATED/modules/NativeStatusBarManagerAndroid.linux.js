/**
 * Linux override of
 * react-native/src/private/specs_DEPRECATED/modules/NativeStatusBarManagerAndroid.
 *
 * A desktop window has no status bar: StatusBar imports both platforms'
 * specs, whose upstream versions require a StatusBarManager native module.
 * Here every call does nothing and the bar's height is 0.
 */
const constants = {HEIGHT: 0, DEFAULT_BACKGROUND_COLOR: 0};

const NativeStatusBarManager = {
  getConstants() {
    return constants;
  },
  getHeight(callback) {
    callback({height: 0});
  },
  addListener(eventType) {},
  removeListeners(count) {},
  setColor(color, animated) {},
  setTranslucent(translucent) {},
  setStyle(statusBarStyle, animated) {},
  setHidden(hidden, withAnimation) {},
  setNetworkActivityIndicatorVisible(visible) {},
};

export default NativeStatusBarManager;
