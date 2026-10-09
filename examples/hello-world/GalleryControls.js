// TextInput, Switch and ActivityIndicator as GTK controls.
// rn-gtk-host --module GalleryControls --self-test types into the inputs
// through GTK's editing path and checks what reaches JS and back.
import React, {useRef, useState} from 'react';
import {
  ActivityIndicator,
  Pressable,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
  useWindowDimensions,
} from 'react-native';

function Field({label, children}) {
  return (
    <View style={styles.field}>
      <Text style={styles.label}>{label}</Text>
      {children}
    </View>
  );
}

export default function GalleryControls() {
  const [upper, setUpper] = useState('');
  const [limited, setLimited] = useState('');
  const [contentHeight, setContentHeight] = useState(0);
  const [submitted, setSubmitted] = useState('-');
  const [focused, setFocused] = useState('no');
  const [lastKey, setLastKey] = useState('-');
  const [parentPresses, setParentPresses] = useState(0);
  const [on, setOn] = useState(false);
  const [lockedAttempts, setLockedAttempts] = useState(0);
  const [spinning, setSpinning] = useState(false);
  const focusRef = useRef(null);
  const window = useWindowDimensions();

  return (
    <View style={styles.root}>
      <View style={styles.column}>
        <Field label="controlled, uppercased by JS">
          <TextInput
            nativeID="upper"
            style={styles.input}
            value={upper}
            onChangeText={t => setUpper(t.toUpperCase())}
            placeholder="Type here"
            placeholderTextColor="#FF3B30"
          />
          <Text nativeID="upper-status" style={styles.status}>
            upper: {upper || '-'}
          </Text>
        </Field>
        <Field label="maxLength 5">
          <TextInput
            nativeID="limited"
            style={styles.input}
            maxLength={5}
            onChangeText={setLimited}
          />
          <Text style={styles.status}>max: {limited || '-'}</Text>
        </Field>
        <Field label="secureTextEntry / plain">
          <View style={styles.pair}>
            <TextInput
              nativeID="secure"
              style={[styles.input, styles.half]}
              secureTextEntry
              defaultValue="WWWWWW"
            />
            <TextInput
              nativeID="plain"
              style={[styles.input, styles.half]}
              defaultValue="WWWWWW"
            />
          </View>
        </Field>
        <Field label="multiline, grows with its content">
          <TextInput
            nativeID="multi"
            style={[styles.input, styles.multi]}
            multiline
            placeholder="Several lines"
            onContentSizeChange={e =>
              setContentHeight(Math.round(e.nativeEvent.contentSize.height))
            }
          />
          <Text style={styles.status}>content height {contentHeight}</Text>
        </Field>
      </View>

      <View style={styles.column}>
        <Field label="onSubmitEditing, onKeyPress">
          <TextInput
            nativeID="submit"
            style={styles.input}
            onSubmitEditing={e => setSubmitted(e.nativeEvent.text)}
            onKeyPress={e => setLastKey(e.nativeEvent.key)}
          />
          <Text style={styles.status}>
            submitted: {submitted} · key: {lastKey}
          </Text>
        </Field>
        <Field label="focus() / blur(), onFocus / onBlur">
          <TextInput
            nativeID="focusable"
            ref={focusRef}
            style={styles.input}
            onFocus={() => setFocused('yes')}
            onBlur={() => setFocused('no')}
          />
          <View style={styles.pair}>
            <Pressable
              nativeID="focus-btn"
              style={styles.button}
              onPress={() => focusRef.current?.focus()}>
              <Text style={styles.buttonText}>focus</Text>
            </Pressable>
            <Pressable
              nativeID="blur-btn"
              style={styles.button}
              onPress={() => focusRef.current?.blur()}>
              <Text style={styles.buttonText}>blur</Text>
            </Pressable>
            <Text style={styles.status}>focus: {focused}</Text>
          </View>
        </Field>
        <Field label="TextInput inside a Pressable">
          <Pressable
            nativeID="press-parent"
            style={styles.parent}
            onPress={() => setParentPresses(n => n + 1)}>
            <TextInput nativeID="in-pressable" style={styles.input} />
            <Text style={styles.status}>parent presses {parentPresses}</Text>
          </Pressable>
        </Field>
      </View>

      <View style={styles.column}>
        <Field label="Switch">
          <View style={styles.pair}>
            <Switch
              nativeID="switch"
              value={on}
              onValueChange={setOn}
              trackColor={{false: '#C7C7CC', true: '#34C759'}}
            />
            <Text style={styles.status}>switch: {on ? 'on' : 'off'}</Text>
          </View>
        </Field>
        <Field label="Switch controlled to off">
          <View style={styles.pair}>
            <Switch
              nativeID="locked"
              value={false}
              onValueChange={() => setLockedAttempts(n => n + 1)}
            />
            <Text style={styles.status}>locked attempts {lockedAttempts}</Text>
          </View>
        </Field>
        <Field label="ActivityIndicator large / stopped">
          <View style={styles.pair}>
            <ActivityIndicator nativeID="spinner" size="large" color="#007AFF" />
            <ActivityIndicator nativeID="stopped" animating={spinning} />
            <Pressable
              nativeID="toggle-spinner"
              style={styles.button}
              onPress={() => setSpinning(s => !s)}>
              <Text style={styles.buttonText}>toggle</Text>
            </Pressable>
          </View>
        </Field>
        <Field label="useWindowDimensions (resize the window)">
          <Text nativeID="window-size" style={styles.status}>
            window {Math.round(window.width)} x {Math.round(window.height)}
          </Text>
        </Field>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, flexDirection: 'row', backgroundColor: '#F5F5F7', padding: 10},
  column: {width: 300, marginRight: 10},
  field: {marginBottom: 14},
  label: {fontSize: 12, color: '#6E6E73', marginBottom: 4},
  input: {
    height: 36,
    paddingHorizontal: 10,
    fontSize: 16,
    color: '#1C1C1E',
    backgroundColor: '#FFFFFF',
    borderWidth: 1,
    borderColor: '#C7C7CC',
    borderRadius: 8,
  },
  half: {width: 140},
  multi: {height: undefined, minHeight: 36, paddingVertical: 8},
  pair: {flexDirection: 'row', alignItems: 'center', gap: 10},
  status: {fontSize: 12, color: '#1C1C1E', marginTop: 4},
  button: {
    paddingVertical: 6,
    paddingHorizontal: 12,
    borderRadius: 6,
    backgroundColor: '#007AFF',
  },
  buttonText: {color: '#FFFFFF', fontSize: 13, fontWeight: '600'},
  parent: {padding: 8, borderRadius: 8, backgroundColor: '#E5E5EA'},
});
