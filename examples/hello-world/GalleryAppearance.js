// Appearance, useColorScheme and PlatformColor.
// rn-gtk-host --module GalleryAppearance --self-test switches the scheme
// (setColorScheme, and the system's as the host sees it) and checks the
// swatches' pixels, GTK's dark variant and what JS reports.
import React, {useEffect, useState} from 'react';
import {
  Appearance,
  Button,
  PlatformColor,
  Pressable,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
  useColorScheme,
} from 'react-native';

const SWATCHES = [
  ['sw-window', PlatformColor('window_bg_color')],
  ['sw-accent', PlatformColor('accent_bg_color')],
  // The first name the palette knows wins.
  ['sw-fallback', PlatformColor('no_such_color', 'success_bg_color')],
  // None known: black.
  ['sw-unknown', PlatformColor('no_such_color')],
  // GTK CSS and libadwaita 1.6 spellings.
  ['sw-css', PlatformColor('--view-bg-color')],
  ['sw-at', PlatformColor('@error_color')],
];

function Choice({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.choice}>
      <Text style={styles.choiceText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryAppearance() {
  const scheme = useColorScheme();
  const [changes, setChanges] = useState(0);
  useEffect(() => {
    const sub = Appearance.addChangeListener(() => setChanges(n => n + 1));
    return () => sub.remove();
  }, []);
  return (
    <View style={styles.root}>
      <Text nativeID="scheme" style={styles.fg}>
        scheme: {scheme}
      </Text>
      <Text style={styles.fg}>
        getColorScheme: {Appearance.getColorScheme()} · changes {changes}
      </Text>
      <View style={styles.row}>
        <Choice id="set-dark" label="dark" onPress={() => Appearance.setColorScheme('dark')} />
        <Choice id="set-light" label="light" onPress={() => Appearance.setColorScheme('light')} />
        <Choice
          id="set-system"
          label="system"
          onPress={() => Appearance.setColorScheme('unspecified')}
        />
      </View>
      <View style={styles.row}>
        {SWATCHES.map(([id, color]) => (
          <View key={id} nativeID={id} style={[styles.swatch, {backgroundColor: color}]} />
        ))}
      </View>
      <View nativeID="fg-box" style={styles.fgBox}>
        <Text nativeID="fg-text" style={styles.big}>
          ████
        </Text>
      </View>
      <View style={styles.row}>
        <Button nativeID="button" title="A GTK button" onPress={() => {}} />
        <Switch value={true} />
        <TextInput style={styles.input} placeholder="TextInput" />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 20, gap: 12, backgroundColor: PlatformColor('window_bg_color')},
  fg: {color: PlatformColor('window_fg_color'), fontSize: 16},
  row: {flexDirection: 'row', gap: 12, alignItems: 'center'},
  choice: {
    paddingVertical: 8,
    paddingHorizontal: 14,
    borderRadius: 6,
    backgroundColor: PlatformColor('accent_bg_color'),
  },
  choiceText: {color: PlatformColor('accent_fg_color'), fontWeight: 'bold'},
  swatch: {width: 60, height: 60, borderRadius: 8},
  // The text on a fixed mid gray: dark ink when light, light ink when dark.
  fgBox: {width: 140, padding: 8, backgroundColor: '#808080'},
  big: {fontSize: 28, color: PlatformColor('window_fg_color')},
  input: {width: 160, borderWidth: 1, borderColor: PlatformColor('borders'), borderRadius: 6, padding: 6},
});
