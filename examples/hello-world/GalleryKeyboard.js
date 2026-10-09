// Keyboard focus and keys.
// rn-gtk-host --module GalleryKeyboard --self-test moves focus with Tab
// and Shift+Tab, presses keys on the focused view and checks focus events,
// key events, keyDownEvents, Enter/Space activation, the focus ring,
// autoFocus and ref.focus() / blur().
import React, {useRef, useState} from 'react';
import {Button, Pressable, StyleSheet, Switch, Text, TextInput, View} from 'react-native';

function describe(e) {
  const {key, code, altKey, ctrlKey, metaKey, shiftKey} = e.nativeEvent;
  const mods = [ctrlKey && 'ctrl', altKey && 'alt', shiftKey && 'shift', metaKey && 'meta']
    .filter(Boolean)
    .join('+');
  return `${key === ' ' ? 'Space' : key} ${code}${mods ? ' ' + mods : ''}`;
}

export default function GalleryKeyboard() {
  const [focused, setFocused] = useState('none');
  const [keyDown, setKeyDown] = useState('-');
  const [keyUp, setKeyUp] = useState('-');
  const [presses, setPresses] = useState(0);
  const [buttonPresses, setButtonPresses] = useState(0);
  const [inputKey, setInputKey] = useState('-');
  const [handledKey, setHandledKey] = useState('-');
  const v1 = useRef(null);
  const focus = id => ({
    onFocus: () => setFocused(id),
    onBlur: () => setFocused(f => (f === id ? 'none' : f)),
  });
  return (
    <View style={styles.root}>
      <Text style={styles.status}>focused: {focused}</Text>
      <Text style={styles.status}>
        keydown {keyDown} · keyup {keyUp} · input {inputKey} · handled {handledKey}
      </Text>
      <Text style={styles.status}>
        presses {presses} · button {buttonPresses}
      </Text>
      <View style={styles.row}>
        <View nativeID="first" focusable autoFocus style={styles.box} {...focus('first')}>
          <Text>first</Text>
        </View>
        <Pressable
          nativeID="p1"
          style={styles.box}
          onPress={() => setPresses(n => n + 1)}
          {...focus('p1')}>
          <Text>Pressable</Text>
        </Pressable>
        <TextInput
          nativeID="t1"
          style={styles.input}
          placeholder="TextInput"
          onKeyDown={e => setInputKey(describe(e))}
          {...focus('t1')}
        />
        <View
          nativeID="v1"
          ref={v1}
          focusable
          style={styles.box}
          onKeyDown={e => {
            setKeyDown(describe(e));
            if (e.nativeEvent.key === 'b') v1.current?.blur();
          }}
          onKeyUp={e => setKeyUp(describe(e))}
          {...focus('v1')}>
          <Text>keys</Text>
        </View>
        <Switch nativeID="sw" value={false} />
        <Button nativeID="b1" title="Button" onPress={() => setButtonPresses(n => n + 1)} />
      </View>
      <View style={styles.row}>
        <Pressable nativeID="p2" focusable={false} style={styles.box} onPress={() => {}}>
          <Text>not focusable</Text>
        </Pressable>
        <View style={styles.box}>
          <Text>plain View</Text>
        </View>
        <View
          nativeID="vh"
          focusable
          style={styles.box}
          keyDownEvents={[{key: 'Tab'}, {code: 'KeyA', ctrlKey: true}]}
          onKeyDown={e => setHandledKey(describe(e))}
          {...focus('vh')}>
          <Text>handles Tab, Ctrl+A</Text>
        </View>
        <View nativeID="noring" focusable enableFocusRing={false} style={styles.box} {...focus('noring')}>
          <Text>no ring</Text>
        </View>
        <Pressable nativeID="focus-v1" style={styles.box} onPress={() => v1.current?.focus()}>
          <Text>focus keys view</Text>
        </Pressable>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 20, gap: 12, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#000000'},
  row: {flexDirection: 'row', gap: 12, alignItems: 'center', flexWrap: 'wrap'},
  box: {
    width: 110,
    height: 50,
    borderRadius: 8,
    backgroundColor: '#FFFFFF',
    borderWidth: 1,
    borderColor: '#BBBBBB',
    alignItems: 'center',
    justifyContent: 'center',
  },
  input: {width: 140, borderWidth: 1, borderColor: '#BBBBBB', borderRadius: 6, padding: 6, color: '#000000'},
});
