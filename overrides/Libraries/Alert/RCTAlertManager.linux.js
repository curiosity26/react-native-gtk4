/**
 * Linux override of react-native/Libraries/Alert/RCTAlertManager.
 *
 * Alert.linux.js calls the host's AlertManager itself. This keeps iOS's
 * alertWithArgs (buttons as [{key: text}], cancelButtonKey and the other
 * keys) working for code that imports it, on the same dialog.
 */
import type {Args} from 'react-native-upstream/Libraries/Alert/NativeAlertManager';

import NativeAlertManager from 'react-native-upstream/Libraries/Alert/NativeAlertManager';

export function alertWithArgs(
  args: Args,
  callback: (id: number, value: string) => void,
) {
  if (NativeAlertManager == null) {
    return;
  }
  const keys = [];
  const buttons = (args.buttons ?? []).map(entry => {
    const key = Object.keys(entry)[0];
    keys.push(key);
    return {
      text: entry[key] ?? '',
      style:
        key === args.cancelButtonKey
          ? 'cancel'
          : key === args.destructiveButtonKey
            ? 'destructive'
            : 'default',
      isPreferred: key === args.preferredButtonKey,
    };
  });
  // $FlowFixMe[incompatible-type] Linux's AlertManager takes its own args.
  NativeAlertManager.alertWithArgs(
    {
      title: args.title ?? '',
      message: args.message ?? '',
      buttons,
      type: args.type ?? 'default',
      defaultValue: args.defaultValue ?? '',
      keyboardType: args.keyboardType ?? '',
      cancelable: false,
    },
    // $FlowFixMe[incompatible-type]
    (id: number, value: mixed) => {
      if (id >= 0 && callback) {
        // $FlowFixMe[incompatible-type]
        callback(keys.length > 0 ? Number(keys[id]) : id, value);
      }
    },
  );
}
