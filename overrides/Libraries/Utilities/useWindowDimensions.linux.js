/**
 * Linux override of react-native/Libraries/Utilities/useWindowDimensions
 * (RN 0.87.1).
 *
 * An app can have several windows (Windows from @curiosity26/react-native-
 * gtk4), each a surface of its own: the hook reports the window the
 * calling component is in (its root tag), and follows that window's size.
 * In the main window (and anything not in a window of the app's) it is
 * React Native's: Dimensions.get('window').
 *
 * @flow strict-local
 * @format
 */

import {
  type DisplayMetrics,
  type DisplayMetricsAndroid,
} from 'react-native-upstream/Libraries/Utilities/NativeDeviceInfo';

import Dimensions from 'react-native-upstream/Libraries/Utilities/Dimensions';
import {RootTagContext} from 'react-native-upstream/Libraries/ReactNative/RootTag';
import {useContext, useEffect, useState} from 'react';

import {
  mainWindowId,
  onWindowEvent,
  windowMetrics,
} from '../../../js/windowEvents';

type Metrics = DisplayMetrics | DisplayMetricsAndroid;

function same(a: Metrics, b: Metrics): boolean {
  return (
    a.width === b.width &&
    a.height === b.height &&
    a.scale === b.scale &&
    a.fontScale === b.fontScale
  );
}

export default function useWindowDimensions(): Metrics {
  const rootTag = useContext(RootTagContext);
  // $FlowFixMe[incompatible-type] a root tag is a number
  const ownWindow = rootTag !== mainWindowId() ? windowMetrics(rootTag) : null;
  const read = (): Metrics =>
    // $FlowFixMe[incompatible-type]
    (ownWindow != null ? windowMetrics(rootTag) : null) ??
    Dimensions.get('window');
  const [dimensions, setDimensions] = useState<Metrics>(read);
  useEffect(() => {
    const update = () => {
      const next = read();
      if (!same(dimensions, next)) {
        setDimensions(next);
      }
    };
    // The main window's changes (and font scale, for every window).
    const subscriptions = [Dimensions.addEventListener('change', update)];
    if (ownWindow != null) {
      subscriptions.push(
        onWindowEvent(event => {
          if (event.id === rootTag && event.type === 'resize') {
            update();
          }
        }),
      );
    }
    // A change between `read` in render and the subscriptions.
    update();
    return () => subscriptions.forEach(s => s.remove());
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [dimensions, rootTag]);
  return dimensions;
}
