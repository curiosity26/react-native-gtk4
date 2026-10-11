/**
 * Linux override of react-native/Libraries/Utilities/BackHandler.
 *
 * Like react-native-windows: the host sends hardwareBackPress when the
 * user goes back (Alt+Left, the Back key, the mouse's back button), and
 * listeners run newest first until one returns true (React Navigation pops
 * its stack). Unhandled, nothing happens: a desktop app doesn't quit on
 * Alt+Left. exitApp() quits the app.
 */
import NativeDeviceEventManager from 'react-native-upstream/Libraries/NativeModules/specs/NativeDeviceEventManager';
import RCTDeviceEventEmitter from 'react-native-upstream/Libraries/EventEmitter/RCTDeviceEventEmitter';
import {setEventInitTimeStamp} from 'react-native-upstream/src/private/webapis/dom/events/internals/EventInternals';
import {HardwareBackPressEvent} from 'react-native-upstream/Libraries/Utilities/HardwareBackPressEvent';

const subscriptions = [];

RCTDeviceEventEmitter.addListener('hardwareBackPress', nativeEvent => {
  const options = {};
  if (nativeEvent?.timeStamp != null) {
    setEventInitTimeStamp(options, nativeEvent.timeStamp);
  }
  const event = new HardwareBackPressEvent(options);
  for (let i = subscriptions.length - 1; i >= 0; i--) {
    if (subscriptions[i]?.(event)) return;
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
