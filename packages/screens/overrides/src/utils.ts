import { BackHandler, Platform } from 'react-native';

// react-native-screens' src/utils.ts with Linux's search bar (a
// GtkSearchEntry in the header): the Metro config uses this file instead of
// the library's when @curiosity26/react-native-gtk4-screens is installed.
export const isSearchBarAvailableForCurrentPlatform = [
  'ios',
  'android',
  'linux',
].includes(Platform.OS);

export const isHeaderBarButtonsAvailableForCurrentPlatform =
  Platform.OS === 'ios';

export function executeNativeBackPress() {
  // This function invokes the native back press event
  BackHandler.exitApp();
  return true;
}

type OptionalBoolean = 'undefined' | 'false' | 'true';
export function parseBooleanToOptionalBooleanNativeProp(
  prop: boolean | undefined,
): OptionalBoolean {
  switch (prop) {
    case undefined:
      return 'undefined';
    case true:
      return 'true';
    case false:
      return 'false';
  }
}
