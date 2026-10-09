/**
 * Desktop notifications (GNotification): GNOME's notification banners and
 * list, or the desktop's notification server.
 *
 *   import {Notifications} from '@curiosity26/react-native-gtk4';
 *
 *   const id = Notifications.show({
 *     title: 'Download finished',
 *     body: 'notes.pdf (2 MB)',
 *     icon: 'folder-download-symbolic',   // an icon name, or a file path / URI
 *     priority: 'normal',                 // 'low' | 'normal' | 'high' | 'urgent'
 *     buttons: [{id: 'open', title: 'Open'}, {id: 'show', title: 'Show in Files'}],
 *     onPress: action => (action === 'open' ? openFile() : showFolder()),
 *   });
 *   Notifications.close(id);
 *
 * Clicking the notification (action 'default') or a button raises the
 * app's window, then runs onPress and the 'press' listeners with
 * {id, action}. Showing again with the same id replaces it. There is no
 * system tray (stock GNOME has none).
 */
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

const NativeNotifications = TurboModuleRegistry.get('LinuxNotifications');

const listeners = new Set();
const onPress = new Map(); // id -> callback
let subscription = null;

function subscribe() {
  if (subscription != null) {
    return;
  }
  subscription = DeviceEventEmitter.addListener('rngtkNotification', event => {
    onPress.get(event.id)?.(event.action, event);
    for (const listener of [...listeners]) {
      listener(event);
    }
  });
}

const Notifications = {
  /** Shows (or replaces, same id) a notification; returns its id. */
  show(options) {
    if (NativeNotifications == null) {
      return null;
    }
    subscribe();
    const {onPress: callback, ...rest} = options ?? {};
    const id = NativeNotifications.show({
      ...rest,
      id: rest.id != null ? String(rest.id) : undefined,
      buttons: (rest.buttons ?? []).map((b, i) => ({id: String(b.id ?? i), title: String(b.title ?? '')})),
    });
    if (callback != null) {
      onPress.set(id, callback);
    } else {
      onPress.delete(id);
    }
    return id;
  },
  /** Withdraws it from the desktop's list. */
  close(id) {
    onPress.delete(String(id));
    NativeNotifications?.close(String(id));
  },
  /** 'press': a click on a notification ({id, action: 'default'}) or one of its buttons ({id, action: buttonId}). */
  addListener(type, listener) {
    if (type !== 'press') {
      throw new Error(`Notifications: unknown event '${type}'`);
    }
    subscribe();
    listeners.add(listener);
    return {remove: () => listeners.delete(listener)};
  },
  /** Desktops don't ask: always 'granted'. */
  requestPermission() {
    return Promise.resolve('granted');
  },
};

export default Notifications;
