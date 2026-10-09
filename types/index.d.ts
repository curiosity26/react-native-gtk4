/**
 * Types for React Native on Linux.
 *
 * React Native declares PlatformOSType and its Platform types as type
 * aliases, which TypeScript cannot extend from outside, so `Platform.OS`
 * stays typed without 'linux' (react-native-windows and react-native-macos
 * are listed upstream). Until that changes, narrow by hand:
 *
 *   import {Platform} from 'react-native';
 *   import type {LinuxPlatform} from '@curiosity26/react-native-gtk4';
 *
 *   if ((Platform.OS as string) === 'linux') {
 *     const {windowSystem} = (Platform as unknown as LinuxPlatform).constants;
 *   }
 */

/** Platform.constants on Linux (overrides/Libraries/Utilities/NativePlatformConstantsLinux.js). */
export type PlatformConstantsLinux = {
  isTesting: boolean;
  isDisableAnimations?: boolean | undefined;
  reactNativeVersion: {
    major: number;
    minor: number;
    patch: number;
    prerelease: string | null | undefined;
  };
  /** VERSION_ID from /etc/os-release, or the kernel release if there is none. */
  Version: string;
  /** PRETTY_NAME from /etc/os-release. */
  Release: string;
  /** ID from /etc/os-release, e.g. "ubuntu", "fedora", "linuxmint". */
  osId: string;
  /** VERSION_ID from /etc/os-release, or '' if there is none. */
  osVersion: string;
  /** uname -r */
  kernelRelease: string;
  /** The GTK version the host runs with, e.g. "4.14.5". */
  gtkVersion: string;
  windowSystem: 'wayland' | 'x11' | 'unknown';
  /** XDG_CURRENT_DESKTOP, e.g. "ubuntu:GNOME" or "X-Cinnamon"; '' if unset. */
  desktop: string;
};

export type LinuxPlatform = {
  OS: 'linux';
  readonly Version: string;
  readonly constants: PlatformConstantsLinux;
  readonly isTV: false;
  readonly isVision: false;
  readonly isTesting: boolean;
  readonly isDisableAnimations: boolean;
  select<T>(spec: {linux?: T; native?: T; default?: T; [os: string]: T | undefined}): T;
};

/**
 * An entry of keyDownEvents / keyUpEvents: a key the view handles itself,
 * so GTK doesn't act on it (Tab won't move focus, a TextInput won't type
 * it). Matched by `key` (react-native-macos) or `code`
 * (react-native-windows); a modifier left out matches either way.
 */
export type HandledKeyEvent = {
  key?: string;
  code?: string;
  altKey?: boolean;
  ctrlKey?: boolean;
  metaKey?: boolean;
  shiftKey?: boolean;
};

/** The View props the Linux host adds (docs/components.md). */
export type ViewPropsLinux = {
  keyDownEvents?: ReadonlyArray<HandledKeyEvent>;
  keyUpEvents?: ReadonlyArray<HandledKeyEvent>;
  /** Draw the keyboard focus ring (default true), as react-native-macos. */
  enableFocusRing?: boolean;
  /** Take keyboard focus once mounted. */
  autoFocus?: boolean;
  /** A GTK tooltip. */
  tooltip?: string;
  onMouseEnter?: (event: {nativeEvent: {clientX: number; clientY: number; offsetX: number; offsetY: number}}) => void;
  onMouseLeave?: (event: {nativeEvent: {clientX: number; clientY: number; offsetX: number; offsetY: number}}) => void;
  /** A middle (button 1) or right (button 2) click. */
  onAuxClick?: (event: {nativeEvent: {button: number}}) => void;
};
