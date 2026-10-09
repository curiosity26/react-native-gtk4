/**
 * The host's window events ('rngtkWindowEvent': {id, type, width, height})
 * for Windows and useWindowDimensions, and the LinuxWindows module.
 */
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

export const NativeWindows = TurboModuleRegistry.get('LinuxWindows');

const handlers = new Set();
let subscription = null;

export function onWindowEvent(handler) {
  if (subscription == null) {
    subscription = DeviceEventEmitter.addListener('rngtkWindowEvent', event => {
      for (const h of [...handlers]) {
        h(event);
      }
    });
  }
  handlers.add(handler);
  return {remove: () => handlers.delete(handler)};
}

export function mainWindowId() {
  return NativeWindows?.getMainWindowId() ?? 1;
}

// {width, height, scale, fontScale} of window `id` (its surface's root
// tag), or null if it isn't one of the app's windows.
export function windowMetrics(id) {
  return NativeWindows?.getMetrics(id) ?? null;
}
