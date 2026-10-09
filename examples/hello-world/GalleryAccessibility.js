// Accessibility: roles, names, states, values and actions over AT-SPI, and
// AccessibilityInfo.
// rn-gtk-host --module GalleryAccessibility --self-test reads the tree the
// way a screen reader does (scripts/a11y-probe.py) and checks it.
import React, {useEffect, useRef, useState} from 'react';
import {
  AccessibilityInfo,
  Button,
  Pressable,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
  findNodeHandle,
} from 'react-native';

export default function GalleryAccessibility() {
  const [volume, setVolume] = useState(3);
  const [count, setCount] = useState(0);
  const [saves, setSaves] = useState(0);
  const [screenReader, setScreenReader] = useState('?');
  const [reduceMotion, setReduceMotion] = useState('?');
  const [changes, setChanges] = useState(0);
  const target = useRef(null);
  useEffect(() => {
    AccessibilityInfo.isScreenReaderEnabled().then(v => setScreenReader(v ? 'on' : 'off'));
    AccessibilityInfo.isReduceMotionEnabled().then(v => setReduceMotion(v ? 'on' : 'off'));
    const subs = [
      AccessibilityInfo.addEventListener('screenReaderChanged', v => {
        setScreenReader(v ? 'on' : 'off');
        setChanges(n => n + 1);
      }),
      AccessibilityInfo.addEventListener('reduceMotionChanged', v => {
        setReduceMotion(v ? 'on' : 'off');
        setChanges(n => n + 1);
      }),
    ];
    return () => subs.forEach(s => s.remove());
  }, []);
  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        screen reader: {screenReader} · reduce motion: {reduceMotion} · changes {changes} ·
        volume {volume} · saves {saves}
      </Text>
      <View accessibilityRole="header">
        <Text style={styles.title}>Settings</Text>
      </View>
      <View style={styles.row}>
        <Button
          title="Save"
          accessibilityHint="Saves the file"
          onPress={() => setSaves(n => n + 1)}
        />
        <Pressable
          role="checkbox"
          aria-checked
          accessibilityLabel="Wi-Fi"
          style={styles.box}
          onPress={() => {}}>
          <Text>Wi-Fi ✓</Text>
        </Pressable>
        <Pressable
          accessibilityRole="togglebutton"
          accessibilityState={{checked: true}}
          accessibilityLabel="Bold"
          style={styles.box}
          onPress={() => {}}>
          <Text>B</Text>
        </Pressable>
        <Pressable
          nativeID="disabled-action"
          accessibilityRole="button"
          aria-disabled
          accessibilityLabel="Disabled action"
          style={styles.box}>
          <Text>disabled</Text>
        </Pressable>
      </View>
      <View nativeID="battery" accessible style={styles.box}>
        <Text nativeID="battery-text">Battery</Text>
        <Text>80%</Text>
      </View>
      <View
        accessibilityRole="adjustable"
        accessibilityLabel="Volume"
        accessibilityValue={{min: 0, max: 10, now: volume}}
        accessibilityActions={[{name: 'increment'}, {name: 'decrement'}]}
        onAccessibilityAction={e => {
          const name = e.nativeEvent.actionName;
          if (name === 'increment') setVolume(v => Math.min(10, v + 1));
          if (name === 'decrement') setVolume(v => Math.max(0, v - 1));
        }}
        style={styles.box}>
        <Text>volume {volume}</Text>
      </View>
      <View accessibilityElementsHidden>
        <Text>Hidden from screen readers</Text>
      </View>
      <Text nativeID="plain-text">A plain paragraph</Text>
      <View style={styles.row}>
        <TextInput nativeID="name-field" accessibilityLabel="Name field" style={styles.input} />
        <Text nativeID="email-label">Email address</Text>
        <TextInput nativeID="email-field" accessibilityLabelledBy="email-label" style={styles.input} />
        <Switch accessibilityLabel="Dark mode" value={false} />
      </View>
      <View accessibilityLiveRegion="polite">
        <Text>count {count}</Text>
      </View>
      <View style={styles.row}>
        <Button nativeID="count" title="Count" onPress={() => setCount(n => n + 1)} />
        <Button
          nativeID="announce"
          title="Announce"
          onPress={() => AccessibilityInfo.announceForAccessibility('Hello from React Native')}
        />
        <Button
          nativeID="focus-note"
          title="Focus the note"
          onPress={() => AccessibilityInfo.setAccessibilityFocus(findNodeHandle(target.current))}
        />
      </View>
      <View ref={target} nativeID="note" accessible style={styles.box}>
        <Text>A note for the screen reader</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 20, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 13, color: '#000000'},
  title: {fontSize: 22, fontWeight: 'bold', color: '#000000'},
  row: {flexDirection: 'row', gap: 10, alignItems: 'center'},
  box: {padding: 8, borderWidth: 1, borderColor: '#BBBBBB', borderRadius: 6, alignSelf: 'flex-start'},
  input: {width: 140, borderWidth: 1, borderColor: '#BBBBBB', padding: 4},
});
