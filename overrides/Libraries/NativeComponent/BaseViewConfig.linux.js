/**
 * Linux override of react-native/Libraries/NativeComponent/BaseViewConfig.
 *
 * Follows Android: the GTK host runs React Native's shared C++ view props,
 * which accept Android's view config (event names, accessibility props).
 */
export {default} from 'react-native/Libraries/NativeComponent/BaseViewConfig.android';
