/**
 * Linux override of react-native/Libraries/Utilities/Platform.
 *
 * Platform.OS is 'linux'. Platform.select() picks `linux`, then `native`,
 * then `default`. Constants come from the GTK host's PlatformConstants
 * module (see NativePlatformConstantsLinux.js).
 *
 * @flow strict
 */

import type {PlatformConstantsLinux} from './NativePlatformConstantsLinux';

import NativePlatformConstantsLinux from './NativePlatformConstantsLinux';

const Platform = {
  __constants: (null: ?PlatformConstantsLinux),
  OS: 'linux',
  // $FlowFixMe[unsafe-getters-setters]
  get Version(): string {
    // $FlowFixMe[object-this-reference]
    return this.constants.Version;
  },
  // $FlowFixMe[unsafe-getters-setters]
  get constants(): PlatformConstantsLinux {
    // $FlowFixMe[object-this-reference]
    if (this.__constants == null) {
      // $FlowFixMe[object-this-reference]
      this.__constants = NativePlatformConstantsLinux.getConstants();
    }
    // $FlowFixMe[object-this-reference]
    return this.__constants;
  },
  // $FlowFixMe[unsafe-getters-setters]
  get isTesting(): boolean {
    if (__DEV__) {
      // $FlowFixMe[object-this-reference]
      return this.constants.isTesting;
    }
    return false;
  },
  // $FlowFixMe[unsafe-getters-setters]
  get isDisableAnimations(): boolean {
    // $FlowFixMe[object-this-reference]
    return this.constants.isDisableAnimations ?? this.isTesting;
  },
  // $FlowFixMe[unsafe-getters-setters]
  get isTV(): boolean {
    return false;
  },
  // $FlowFixMe[unsafe-getters-setters]
  get isVision(): boolean {
    return false;
  },
  select: <T>(spec: {+linux?: T, +native?: T, +default?: T, ...}): T =>
    'linux' in spec
      ? // $FlowFixMe[incompatible-type]
        spec.linux
      : 'native' in spec
        ? // $FlowFixMe[incompatible-type]
          spec.native
        : // $FlowFixMe[incompatible-type]
          spec.default,
};

export default Platform;
