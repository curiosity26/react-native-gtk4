/**
 * Title bars of the app's own, for windows without the desktop's
 * (titleBar: 'hidden' in Windows.open, or AppOptions::titleBar for the main
 * window):
 *
 *   import {TitleBar} from '@curiosity26/react-native-gtk4';
 *
 *   <TitleBar>
 *     <Text style={styles.title}>Notes</Text>
 *     <Pressable onPress={search}>...</Pressable>
 *   </TitleBar>
 *
 * Dragging it moves the window; double-, middle- and right-clicks do what
 * the desktop's settings say (maximize, the window menu). Presses on
 * Pressables, Touchables, Buttons and TextInputs in it stay theirs. The
 * window's buttons (<WindowControls>) sit at each end, as the desktop's
 * button layout puts them (Cinnamon's on the right, macOS-style layouts
 * on the left).
 *
 * Any View can drag the window with `windowDragRegion`; <WindowControls
 * side="start" | "end" /> places the buttons yourself.
 */
import * as React from 'react';
import {useEffect, useState} from 'react';
import {DeviceEventEmitter, PlatformColor, StyleSheet, View} from 'react-native';

import WindowControlsNativeComponent from './WindowControlsNativeComponent';

// Bumped when the desktop's button layout or theme changes the buttons'
// size: each WindowControls mounts again and is measured anew.
let generation = 0;
const listeners = new Set();
let subscribed = false;

function useControlsGeneration(): number {
  const [value, setValue] = useState(generation);
  useEffect(() => {
    if (!subscribed) {
      subscribed = true;
      DeviceEventEmitter.addListener('rngtkWindowControlsChanged', () => {
        generation++;
        for (const listener of [...listeners]) {
          listener(generation);
        }
      });
    }
    listeners.add(setValue);
    return () => {
      listeners.delete(setValue);
    };
  }, []);
  return value;
}

/** The window's minimize, maximize and close buttons for one end. */
export function WindowControls({
  side = 'end',
  ...props
}: {
  side?: 'start' | 'end',
  ...
}): React.Node {
  const key = useControlsGeneration();
  return <WindowControlsNativeComponent key={key} side={side} {...props} />;
}

export default function TitleBar({
  children,
  style,
  showWindowControls = true,
  ...props
}: {
  children?: React.Node,
  style?: any,
  showWindowControls?: boolean,
  ...
}): React.Node {
  return (
    <View
      // $FlowFixMe[prop-missing] Linux: drags the window.
      windowDragRegion={true}
      style={[styles.bar, style]}
      {...props}>
      {showWindowControls ? <WindowControls side="start" /> : null}
      <View style={styles.content}>{children}</View>
      {showWindowControls ? <WindowControls side="end" /> : null}
    </View>
  );
}

const styles = StyleSheet.create({
  // A GTK header bar's look.
  bar: {
    flexDirection: 'row',
    alignItems: 'center',
    minHeight: 46,
    paddingHorizontal: 6,
    gap: 6,
    backgroundColor: PlatformColor('headerbar_bg_color'),
    borderBottomWidth: 1,
    borderBottomColor: PlatformColor('headerbar_shade_color'),
  },
  content: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
  },
});
