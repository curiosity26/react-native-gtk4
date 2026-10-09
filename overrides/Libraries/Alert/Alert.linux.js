/**
 * Linux override of react-native/Libraries/Alert/Alert (RN 0.87.1).
 *
 * Alert.alert and Alert.prompt both show a GTK message dialog through the
 * host's AlertManager (linux/src/Dialogs.cc), modal over the app's active
 * window. It takes what iOS and Android take together:
 *
 * - buttons: any number, in order, with the `cancel` one first (GNOME puts
 *   it on the left); `destructive` draws red; `isPreferred` (or else the
 *   last plain button) is the default, which Enter presses.
 * - Escape or the window's close button press the cancel button. With none,
 *   `options.cancelable` lets them dismiss the alert, which calls
 *   `options.onDismiss` (Android's meaning); otherwise the alert stays.
 * - prompt types plain-text, secure-text and login-password, defaultValue
 *   and keyboardType, as on iOS: onPress gets the text (or {login,
 *   password}).
 *
 * Buttons without text say GTK's own "OK" or "Cancel", translated.
 */
import NativeAlertManager from 'react-native-upstream/Libraries/Alert/NativeAlertManager';

export type AlertType = 'default' | 'plain-text' | 'secure-text' | 'login-password';

export type AlertButtonStyle = 'default' | 'cancel' | 'destructive';

export type AlertButton = {
  text?: string,
  onPress?: ?((value?: string) => any) | ?Function,
  isPreferred?: boolean,
  style?: AlertButtonStyle,
  ...
};

export type AlertButtons = Array<AlertButton>;

export type AlertOptions = {
  cancelable?: ?boolean,
  userInterfaceStyle?: 'unspecified' | 'light' | 'dark',
  onDismiss?: ?() => void,
  ...
};

function show(
  title: ?string,
  message: ?string,
  buttons: AlertButtons,
  type: AlertType,
  defaultValue: ?string,
  keyboardType: ?string,
  options: ?AlertOptions,
): void {
  if (NativeAlertManager == null) {
    return;
  }
  // $FlowFixMe[incompatible-type] Linux's AlertManager takes its own args.
  NativeAlertManager.alertWithArgs(
    {
      title: title || '',
      message: message || '',
      buttons: buttons.map(b => ({
        text: b.text ?? '',
        style: b.style ?? 'default',
        isPreferred: b.isPreferred === true,
      })),
      type,
      defaultValue: defaultValue ?? '',
      keyboardType: keyboardType ?? '',
      cancelable: options?.cancelable === true,
    },
    // $FlowFixMe[incompatible-type]
    (id: number, value: mixed) => {
      if (id < 0) {
        options?.onDismiss?.();
        return;
      }
      const onPress = buttons[id]?.onPress;
      if (onPress) {
        type === 'default' ? onPress() : onPress(value);
      }
    },
  );
}

class Alert {
  static alert(
    title: ?string,
    message?: ?string,
    buttons?: AlertButtons,
    options?: AlertOptions,
  ): void {
    show(
      title,
      message,
      buttons && buttons.length > 0 ? buttons : [{text: ''}],
      'default',
      undefined,
      undefined,
      options,
    );
  }

  static prompt(
    title: ?string,
    message?: ?string,
    callbackOrButtons?: ?(((text: string) => void) | AlertButtons),
    type?: ?AlertType = 'plain-text',
    defaultValue?: string,
    keyboardType?: string,
    options?: AlertOptions,
  ): void {
    let buttons: AlertButtons;
    if (typeof callbackOrButtons === 'function') {
      buttons = [{text: '', style: 'cancel'}, {text: '', onPress: callbackOrButtons}];
    } else if (Array.isArray(callbackOrButtons) && callbackOrButtons.length > 0) {
      buttons = callbackOrButtons;
    } else {
      buttons = [{text: '', style: 'cancel'}, {text: ''}];
    }
    show(title, message, buttons, type || 'plain-text', defaultValue, keyboardType, options);
  }
}

export default Alert;
