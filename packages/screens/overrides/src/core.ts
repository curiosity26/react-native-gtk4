'use client';

// react-native-screens' src/core.ts with Linux among the native platforms:
// the Metro config uses this file instead of the library's when
// @curiosity26/react-native-gtk4-screens is installed.
import { Platform } from 'react-native';

export const isNativePlatformSupported =
  Platform.OS === 'ios' ||
  Platform.OS === 'android' ||
  Platform.OS === 'windows' ||
  Platform.OS === 'linux';

let ENABLE_SCREENS = isNativePlatformSupported;

export function enableScreens(shouldEnableScreens = true) {
  ENABLE_SCREENS = shouldEnableScreens;
}

let ENABLE_FREEZE = false;

export function enableFreeze(shouldEnableReactFreeze = true) {
  if (!isNativePlatformSupported) {
    return;
  }

  ENABLE_FREEZE = shouldEnableReactFreeze;
}

export function screensEnabled() {
  return ENABLE_SCREENS;
}

export function freezeEnabled() {
  return ENABLE_FREEZE;
}
