/**
 * Linux override of react-native/Libraries/Share/Share (not split by
 * platform: it rejects every platform but iOS and Android).
 *
 * Linux desktops have no share sheet, so this is a stub: Share.share()
 * checks its arguments as upstream and resolves as if the user dismissed
 * the dialog (`dismissedAction`), so apps that offer sharing keep working.
 *
 * @flow strict-local
 * @format
 */

import type {ColorValue} from 'react-native-upstream/Libraries/StyleSheet/StyleSheet';

import invariant from 'invariant';

export type ShareContent =
  | {title?: string, url: string, message?: string}
  | {title?: string, url?: string, message: string};
export type ShareOptions = {
  dialogTitle?: string,
  excludedActivityTypes?: Array<string>,
  tintColor?: ColorValue,
  subject?: string,
  anchor?: number,
};
export type ShareAction = {
  action: 'sharedAction' | 'dismissedAction',
  activityType?: string | null,
};

class Share {
  static share(
    content: ShareContent,
    options?: ShareOptions = {},
  ): Promise<{action: string, activityType: ?string}> {
    invariant(
      typeof content === 'object' && content !== null,
      'Content to share must be a valid object',
    );
    invariant(
      typeof content.url === 'string' || typeof content.message === 'string',
      'At least one of URL or message is required',
    );
    invariant(
      typeof options === 'object' && options !== null,
      'Options must be a valid object',
    );
    return Promise.resolve({action: Share.dismissedAction, activityType: null});
  }

  static sharedAction: 'sharedAction' = 'sharedAction';
  static dismissedAction: 'dismissedAction' = 'dismissedAction';
}

export default Share;
