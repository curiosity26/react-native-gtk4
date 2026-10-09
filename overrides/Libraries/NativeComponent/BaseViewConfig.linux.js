/**
 * Linux override of react-native/Libraries/NativeComponent/BaseViewConfig.
 *
 * Follows Android: the GTK host runs React Native's shared C++ view props,
 * which accept Android's view config (event names, accessibility props).
 * Android's already has focusable and the focus, blur, keyDown and keyUp
 * events; this adds the host's keyboard props, spelled as in
 * react-native-windows and react-native-macos.
 *
 * @flow strict-local
 * @format
 */

import type {PartialViewConfigWithoutName} from 'react-native-upstream/Libraries/NativeComponent/PlatformBaseViewConfig';

import AndroidConfig from 'react-native-upstream/Libraries/NativeComponent/BaseViewConfig.android';

const PlatformBaseViewConfigLinux: PartialViewConfigWithoutName = {
  ...AndroidConfig,
  validAttributes: {
    ...AndroidConfig.validAttributes,
    // Keys the view handles itself: [{key, code, altKey, ctrlKey,
    // metaKey, shiftKey}] (GTK then doesn't act on them).
    keyDownEvents: true,
    keyUpEvents: true,
    // react-native-macos: draw the keyboard focus ring (default true).
    enableFocusRing: true,
    // Take keyboard focus once mounted.
    autoFocus: true,
  },
};

export default PlatformBaseViewConfigLinux;
