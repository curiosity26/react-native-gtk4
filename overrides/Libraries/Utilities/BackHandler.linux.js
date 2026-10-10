/**
 * Linux override of react-native/Libraries/Utilities/BackHandler.
 *
 * Like react-native-windows: the host sends hardwareBackPress when the
 * user goes back (Alt+Left, the Back key, the mouse's back button), and
 * listeners run newest first until one returns true (React Navigation pops
 * its stack). Unhandled, nothing happens: a desktop app doesn't quit on
 * Alt+Left. exitApp() quits the app.
 *
 * React Native 0.83 calls the listeners without an argument (the
 * HardwareBackPressEvent they get from 0.85 on doesn't exist yet).
 */
import NativeDeviceEventManager from 'react-native-upstream/Libraries/NativeModules/specs/NativeDeviceEventManager';
import RCTDeviceEventEmitter from 'react-native-upstream/Libraries/EventEmitter/RCTDeviceEventEmitter';

const subscriptions = [];

RCTDeviceEventEmitter.addListener('hardwareBackPress', () => {
  for (let i = subscriptions.length - 1; i >= 0; i--) {
    if (subscriptions[i]?.()) return;
  }
});

const BackHandler = {
  exitApp() {
    NativeDeviceEventManager?.invokeDefaultBackPressHandler();
  },

  addEventListener(eventName, handler) {
    if (!subscriptions.includes(handler)) subscriptions.push(handler);
    return {
      remove() {
        const index = subscriptions.indexOf(handler);
        if (index !== -1) subscriptions.splice(index, 1);
      },
    };
  },
};

export default BackHandler;
