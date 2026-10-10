// Window styles (Windows.open's titleBar and transparent) and <TitleBar>
// from @curiosity26/react-native-gtk4. The main window runs with no title
// bar of the desktop's (rn-gtk-host gives GalleryTitleBar titleBar
// 'hidden') and draws its own.
// rn-gtk-host --module GalleryTitleBar --self-test drags and clicks the
// title bars and checks a transparent window's pixels.
import React, {useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';
import {TitleBar, Windows, useWindow} from '@curiosity26/react-native-gtk4';

function Action({id, label, onPress, style}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={[styles.action, style]}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export function GalleryTitleBarChild({kind}) {
  const window = useWindow();
  if (kind === 'clear') {
    // Frameless and transparent: only the panel shows, and drags the window.
    return (
      <View style={styles.clearRoot}>
        <View nativeID="clear-panel" windowDragRegion style={styles.panel}>
          <Text style={styles.panelText}>A transparent window</Text>
          <Action id="clear-close" label="Close" onPress={() => window.close()} />
        </View>
      </View>
    );
  }
  return (
    <View style={styles.root}>
      <TitleBar nativeID="child-titlebar">
        <Text style={styles.title}>A title bar of its own</Text>
      </TitleBar>
      <View style={styles.body}>
        <Text style={styles.status}>Drag the bar, double-click it, or right-click it.</Text>
      </View>
    </View>
  );
}

export default function GalleryTitleBar() {
  const [presses, setPresses] = useState(0);
  return (
    <View style={styles.root}>
      <TitleBar nativeID="titlebar">
        <Text nativeID="title" style={styles.title}>
          Title bar gallery
        </Text>
        <View style={styles.spacer} />
        <Action id="tb-button" label="Search" onPress={() => setPresses(n => n + 1)} />
      </TitleBar>
      <View style={styles.body}>
        <Text nativeID="status" style={styles.status}>
          presses {presses}
        </Text>
        <View style={styles.row}>
          <Action
            id="open-hidden"
            label="A window with its own title bar"
            onPress={() =>
              Windows.open({
                component: 'GalleryTitleBarChild',
                initialProps: {kind: 'hidden'},
                title: 'Own title bar',
                titleBar: 'hidden',
                width: 480,
                height: 320,
              })
            }
          />
          <Action
            id="open-clear"
            label="A transparent, frameless window"
            onPress={() =>
              Windows.open({
                component: 'GalleryTitleBarChild',
                initialProps: {kind: 'clear'},
                title: 'Transparent',
                titleBar: 'none',
                transparent: true,
                width: 360,
                height: 240,
              })
            }
          />
        </View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#FFFFFF'},
  title: {fontSize: 15, fontWeight: 'bold', color: '#1C1C1E', paddingHorizontal: 6},
  spacer: {flex: 1},
  body: {flex: 1, padding: 16, gap: 10},
  status: {fontSize: 14, color: '#1C1C1E'},
  row: {flexDirection: 'row', gap: 8, flexWrap: 'wrap'},
  action: {paddingVertical: 6, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  actionText: {color: '#FFFFFF', fontSize: 14},
  clearRoot: {flex: 1, padding: 40},
  panel: {
    flex: 1,
    borderRadius: 24,
    backgroundColor: 'rgba(53,132,228,0.92)',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 12,
  },
  panelText: {color: '#FFFFFF', fontSize: 16, fontWeight: 'bold'},
});
