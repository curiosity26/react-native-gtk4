/**
 * The PlatformConstants native module as the GTK host implements it
 * (linux/src/PlatformConstantsModule.cc).
 *
 * @flow strict
 */

import type {TurboModule} from 'react-native/Libraries/TurboModule/RCTExport';

import * as TurboModuleRegistry from 'react-native/Libraries/TurboModule/TurboModuleRegistry';

export type PlatformConstantsLinux = {
  isTesting: boolean,
  isDisableAnimations?: boolean,
  reactNativeVersion: {
    major: number,
    minor: number,
    patch: number,
    prerelease: ?string,
  },
  // VERSION_ID from /etc/os-release ("24.04", "41", "22.3"); the kernel
  // release on distros without one (rolling releases).
  Version: string,
  // PRETTY_NAME from /etc/os-release ("Ubuntu 24.04.3 LTS").
  Release: string,
  // ID from /etc/os-release ("ubuntu", "fedora", "linuxmint").
  osId: string,
  // VERSION_ID from /etc/os-release, or '' when there is none.
  osVersion: string,
  // uname -r
  kernelRelease: string,
  // The GTK the host is running with, e.g. "4.14.5".
  gtkVersion: string,
  windowSystem: 'wayland' | 'x11' | 'unknown',
  // XDG_CURRENT_DESKTOP, e.g. "ubuntu:GNOME" or "X-Cinnamon"; '' if unset.
  desktop: string,
};

export interface Spec extends TurboModule {
  +getConstants: () => PlatformConstantsLinux;
}

export default (TurboModuleRegistry.getEnforcing<Spec>(
  'PlatformConstants',
): Spec);
