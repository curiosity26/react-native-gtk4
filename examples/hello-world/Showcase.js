// A hands-on tour of the components and APIs. Each page says what to try,
// and the log at the bottom shows the events that reach JS. The chrome
// uses PlatformColor, so it follows light and dark (Appearance page).
//   npm run start:hello-world
//   npm run dev:hello-world -- --module Showcase --width 1100 --height 780
import React, {useCallback, useEffect, useRef, useState} from 'react';
import {
  AccessibilityInfo,
  ActivityIndicator,
  Alert,
  Animated,
  AppState,
  Appearance,
  Button,
  Clipboard,
  Easing,
  FlatList,
  I18nManager,
  Image,
  Linking,
  Modal,
  PixelRatio,
  Platform,
  PlatformColor,
  Pressable,
  ScrollView,
  SectionList,
  Share,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  TouchableHighlight,
  TouchableOpacity,
  TouchableWithoutFeedback,
  TurboModuleRegistry,
  Vibration,
  View,
  findNodeHandle,
  useColorScheme,
  useWindowDimensions,
} from 'react-native';
import {
  ContextMenu,
  Dialogs,
  MenuBar,
  Notifications,
  Windows,
  useWindow,
} from '@curiosity26/react-native-gtk4';

import {makeLibraryPages} from './ShowcaseLibraries';

const halves = require('./assets/halves.png');
const tile = require('./assets/tile.png');
const LOGO = 'https://reactnative.dev/img/tiny_logo.png';
const MOVIES = 'https://reactnative.dev/movies.json';

const LogContext = React.createContext(() => {});
const useLog = () => React.useContext(LogContext);

function Section({title, hint, children}) {
  return (
    <View style={styles.section}>
      <Text style={styles.sectionTitle}>{title}</Text>
      {hint ? <Text style={styles.hint}>{hint}</Text> : null}
      <View style={styles.sectionBody}>{children}</View>
    </View>
  );
}

// Pressable passes only `pressed` to style functions; track hover here.
function HoverPressable({style, ...props}) {
  const [hovered, setHovered] = useState(false);
  return (
    <Pressable
      {...props}
      onHoverIn={() => setHovered(true)}
      onHoverOut={() => setHovered(false)}
      style={state => style({...state, hovered})}
    />
  );
}

function Btn({title, onPress, color = '#007AFF'}) {
  return (
    <HoverPressable
      onPress={onPress}
      style={({pressed, hovered}) => [
        styles.btn,
        {backgroundColor: color, opacity: pressed ? 0.6 : hovered ? 0.85 : 1},
      ]}>
      <Text style={styles.btnText}>{title}</Text>
    </HoverPressable>
  );
}

// ---- Home ----------------------------------------------------------------

function Home() {
  const c = Platform.constants;
  const window = useWindowDimensions();
  const scheme = useColorScheme();
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <View style={styles.hero}>
        <Text style={styles.heroTitle}>Hello, World!</Text>
        <Text style={styles.heroSub}>React Native on GTK4</Text>
      </View>
      <Section title="Platform" hint="Platform.OS and Platform.constants, from the GTK host.">
        <Text style={styles.mono}>Platform.OS = {Platform.OS}</Text>
        <Text style={styles.mono}>window system = {String(c.windowSystem)}</Text>
        <Text style={styles.mono}>
          window = {Math.round(window.width)} x {Math.round(window.height)} (resize
          the window: useWindowDimensions follows it)
        </Text>
        <Text style={styles.mono}>
          React Native {c.reactNativeVersion?.major}.{c.reactNativeVersion?.minor}.
          {c.reactNativeVersion?.patch}
        </Text>
        <Text style={styles.mono}>
          {String(c.Release)} · GTK {String(c.gtkVersion)} · desktop {String(c.desktop || '-')}
        </Text>
        <Text style={styles.mono}>color scheme = {scheme} (see the Appearance page)</Text>
      </Section>
      <Section title="How to use this app">
        <Text style={styles.body}>
          Pick a page on the left. Every page says what to try, and the log at
          the bottom of the window shows each event JS receives. Ctrl+R
          reloads and Ctrl+D opens the dev menu.
        </Text>
      </Section>
    </ScrollView>
  );
}

// ---- Views and text ---------------------------------------------------------

function ViewsText() {
  const log = useLog();
  const [lines, setLines] = useState(2);
  const long =
    'GTK draws this paragraph with Pango. Tap "toggle numberOfLines" to ' +
    'switch between two lines with an ellipsis and the full text. React ' +
    'Native lays it out with Yoga, then measures it with the same font map ' +
    'GTK paints with, so the two always agree. This paragraph is long on ' +
    'purpose: it needs four or five lines at this width, so clamping it to ' +
    'two cuts it off with an ellipsis, and the full text pushes the button ' +
    'below it further down. Line breaks, wrapping and the ellipsis all come ' +
    "from Pango's layout of the same attributed string React Native measured.";
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section title="Borders, radii and shadows">
        <View style={styles.wrap}>
          <View style={[styles.swatch, {borderWidth: 4, borderColor: '#FF9500'}]} />
          <View style={[styles.swatch, {borderRadius: 40, backgroundColor: '#34C759'}]} />
          <View
            style={[
              styles.swatch,
              {borderTopLeftRadius: 30, borderBottomRightRadius: 30, backgroundColor: '#AF52DE'},
            ]}
          />
          <View style={[styles.swatch, {borderWidth: 3, borderStyle: 'dashed', borderColor: '#FF3B30'}]} />
          <View
            style={[
              styles.swatch,
              {
                backgroundColor: PlatformColor('view_bg_color'),
                boxShadow: '0 6px 16px rgba(0,0,0,0.35)',
                borderRadius: 12,
              },
            ]}
          />
          <View
            style={[
              styles.swatch,
              {experimental_backgroundImage: 'linear-gradient(135deg, #007AFF, #FF2D55)', borderRadius: 12},
            ]}
          />
          <View style={[styles.swatch, {backgroundColor: '#5AC8FA', opacity: 0.4}]} />
          <View style={[styles.swatch, {backgroundColor: '#FFCC00', transform: [{rotate: '20deg'}]}]} />
        </View>
      </Section>
      <Section title="Text styling">
        <Text style={[styles.fg, {fontSize: 24, fontWeight: 'bold'}]}>Bold 24</Text>
        <Text style={{fontStyle: 'italic', color: '#FF3B30'}}>Italic red</Text>
        <Text style={[styles.fg, {textDecorationLine: 'underline line-through'}]}>
          Underline and strike
        </Text>
        <Text style={[styles.fg, {letterSpacing: 4, textTransform: 'uppercase'}]}>letter spaced</Text>
        <Text style={[styles.fg, {fontFamily: 'monospace'}]}>monospace 0123456789</Text>
        <Text style={styles.fg}>
          Nested: <Text style={{fontWeight: 'bold'}}>bold</Text>,{' '}
          <Text style={{color: PlatformColor('accent_color')}} onPress={() => log('nested link pressed')}>
            a tappable link
          </Text>
          , and <Text style={{backgroundColor: '#FFEB3B', color: '#000000'}}>highlighted</Text>.
        </Text>
        <Text style={[styles.fg, {textAlign: 'right'}]}>Right aligned</Text>
        <Text style={styles.fg}>Unicode: Привет · こんにちは · مرحبا · 👋🎉</Text>
      </Section>
      <Section title="numberOfLines" hint="Toggle between two lines and the full paragraph.">
        <Text numberOfLines={lines} style={styles.body}>
          {long}
        </Text>
        <View style={styles.row}>
          <Btn
            title="toggle numberOfLines"
            onPress={() => {
              const next = lines ? 0 : 2;
              setLines(next);
              log(`numberOfLines = ${next || '0 (all lines)'}`);
            }}
          />
          <Text style={styles.body}>
            numberOfLines = {lines || '0 (all lines)'}
          </Text>
        </View>
      </Section>
      <Section
        title="Selectable text"
        hint="Drag to select, double-click a word, triple-click a paragraph, Shift+click to extend. Ctrl+C or right-click Copy copies it (all of it with nothing selected); paste into a field on the Inputs page, or middle-click paste the primary selection.">
        <Text selectable style={styles.body}>
          This paragraph is selectable. Select a few words with the mouse,
          then press Ctrl+C.{'\n'}A second paragraph, for triple-clicking.
        </Text>
        <Text selectable selectionColor="rgba(255, 45, 85, 0.35)" style={styles.body}>
          This one sets selectionColor to a translucent pink.
        </Text>
        <Pressable onPress={() => log('Pressable around selectable text pressed')} style={styles.pressBox}>
          <Text selectable style={styles.body}>
            Inside a Pressable: click to press it, drag to select (no press).
          </Text>
        </Pressable>
      </Section>
    </ScrollView>
  );
}

// ---- Buttons and touch ----------------------------------------------------

function Buttons() {
  const log = useLog();
  const [count, setCount] = useState(0);
  const [hovered, setHovered] = useState(false);
  const [pos, setPos] = useState(null);
  const [mouseInside, setMouseInside] = useState(false);
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Pressable"
        hint="Hover to lighten, press to darken, hold for a long press.">
        <Pressable
          onPress={() => {
            setCount(n => n + 1);
            log('Pressable onPress');
          }}
          onLongPress={() => log('Pressable onLongPress')}
          onHoverIn={() => setHovered(true)}
          onHoverOut={() => setHovered(false)}
          style={({pressed}) => [
            styles.bigBtn,
            {backgroundColor: pressed ? '#0040A0' : hovered ? '#3395FF' : '#007AFF'},
          ]}>
          {({pressed}) => (
            <Text style={styles.btnText}>
              {pressed ? 'pressing…' : hovered ? 'hovering' : 'press me'} ({count})
            </Text>
          )}
        </Pressable>
      </Section>
      <Section title="Touchables">
        <View style={styles.wrap}>
          <TouchableOpacity style={styles.touchable} onPress={() => log('TouchableOpacity')}>
            <Text style={styles.btnText}>TouchableOpacity</Text>
          </TouchableOpacity>
          <TouchableHighlight
            style={styles.touchable}
            underlayColor="#FF9500"
            onPress={() => log('TouchableHighlight')}>
            <Text style={styles.btnText}>TouchableHighlight</Text>
          </TouchableHighlight>
          <TouchableWithoutFeedback onPress={() => log('TouchableWithoutFeedback')}>
            <View style={[styles.touchable, {backgroundColor: '#8E8E93'}]}>
              <Text style={styles.btnText}>WithoutFeedback</Text>
            </View>
          </TouchableWithoutFeedback>
        </View>
      </Section>
      <Section
        title="Button"
        hint="Styled like a GTK button: hover and press it. color sets the background; disabled dims it.">
        <View style={styles.wrap}>
          <Button title="Button" onPress={() => log('Button onPress')} />
          <Button title="color" color="#3584E4" onPress={() => log('colored Button onPress')} />
          <Button title="Disabled" disabled onPress={() => log('should not fire')} />
        </View>
      </Section>
      <Section
        title="Desktop mouse props"
        hint="react-native-windows / react-native-macos spelling: onMouseEnter and onMouseLeave, tooltip (hover and wait), and onAuxClick (middle or right click).">
        <View style={styles.wrap}>
          <View
            style={[styles.mouseBox, mouseInside && styles.mouseBoxInside]}
            onMouseEnter={() => {
              setMouseInside(true);
              log('onMouseEnter');
            }}
            onMouseLeave={() => {
              setMouseInside(false);
              log('onMouseLeave');
            }}>
            <Text style={styles.body}>{mouseInside ? 'the mouse is inside' : 'move the mouse in'}</Text>
          </View>
          <View tooltip="A GTK tooltip, from the tooltip prop" style={styles.mouseBox}>
            <Text style={styles.body}>hover for a tooltip</Text>
          </View>
          <Pressable
            style={styles.mouseBox}
            onPress={() => log('onPress (left click)')}
            onAuxClick={e => log(`onAuxClick button ${e.nativeEvent.button} (${e.nativeEvent.button === 1 ? 'middle' : 'right'})`)}>
            <Text style={styles.body}>left, middle or right click</Text>
          </Pressable>
        </View>
      </Section>
      <Section
        title="Pointer events"
        hint="Move and click inside the box. Coordinates come from W3C pointer events.">
        <View
          style={styles.pad}
          onPointerMove={e =>
            setPos({x: e.nativeEvent.offsetX, y: e.nativeEvent.offsetY, kind: 'move'})
          }
          onPointerDown={e => {
            setPos({x: e.nativeEvent.offsetX, y: e.nativeEvent.offsetY, kind: 'down'});
            log(`pointerdown button ${e.nativeEvent.button}`);
          }}
          onPointerLeave={() => setPos(null)}>
          {pos ? (
            <View pointerEvents="none" style={[styles.dot, {left: pos.x - 8, top: pos.y - 8}]} />
          ) : null}
          <Text pointerEvents="none" style={styles.mono}>
            {pos ? `${pos.kind} at ${Math.round(pos.x)}, ${Math.round(pos.y)}` : 'pointer outside'}
          </Text>
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Inputs --------------------------------------------------------------

function Inputs() {
  const log = useLog();
  const [name, setName] = useState('');
  const [upper, setUpper] = useState('');
  const [secret, setSecret] = useState('');
  const [notes, setNotes] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [dark, setDark] = useState(false);
  const [loading, setLoading] = useState(true);
  const ref = useRef(null);
  return (
    <ScrollView contentContainerStyle={styles.page} keyboardShouldPersistTaps="handled">
      <Section title="TextInput" hint="Type in each field. Press Enter in the first one.">
        <TextInput
          ref={ref}
          style={styles.input}
          placeholder="Your name, then Enter"
          value={name}
          onChangeText={setName}
          onFocus={() => log('name focused')}
          onBlur={() => log('name blurred')}
          onSubmitEditing={e => log(`submitted "${e.nativeEvent.text}"`)}
        />
        <Text style={styles.body}>{name ? `Hello, ${name}!` : 'Hello, stranger.'}</Text>
        <TextInput
          style={styles.input}
          placeholder="Controlled: JS uppercases this"
          value={upper}
          onChangeText={t => setUpper(t.toUpperCase())}
        />
        <TextInput
          style={styles.input}
          placeholder="Password"
          secureTextEntry
          value={secret}
          onChangeText={setSecret}
        />
        <Text style={styles.hint}>{secret.length} characters typed</Text>
        <TextInput
          style={[styles.input, {minHeight: 60}]}
          multiline
          placeholder="Multiline: grows as you add lines (max 120 chars)"
          maxLength={120}
          value={notes}
          onChangeText={setNotes}
          onContentSizeChange={e =>
            log(`content height ${Math.round(e.nativeEvent.contentSize.height)}`)
          }
        />
        <Text style={styles.hint}>{notes.length}/120</Text>
        <TextInput style={styles.input} editable={false} value="Read only (editable=false)" />
        <View style={styles.row}>
          <Btn title="focus name" onPress={() => ref.current?.focus()} />
          <Btn title="blur" onPress={() => ref.current?.blur()} color="#8E8E93" />
          <Btn title="clear" onPress={() => ref.current?.clear()} color="#FF3B30" />
        </View>
      </Section>
      <Section title="Switch" hint="Click or drag the switches.">
        <View style={styles.row}>
          <Switch
            value={enabled}
            onValueChange={v => {
              setEnabled(v);
              log(`switch ${v ? 'on' : 'off'}`);
            }}
          />
          <Text style={styles.body}>enabled: {String(enabled)}</Text>
        </View>
        <View style={styles.row}>
          <Switch
            value={dark}
            onValueChange={setDark}
            trackColor={{false: '#C7C7CC', true: '#34C759'}}
            thumbColor={dark ? '#FFFFFF' : '#F2F2F7'}
          />
          <Text style={styles.body}>custom colors</Text>
        </View>
        <View style={styles.row}>
          <Switch value={true} disabled />
          <Text style={styles.body}>disabled</Text>
        </View>
      </Section>
      <Section title="ActivityIndicator" hint="The switch starts and stops all three.">
        <View style={styles.row}>
          <Switch value={loading} onValueChange={setLoading} />
          <ActivityIndicator animating={loading} />
          <ActivityIndicator animating={loading} size="large" color="#FF2D55" />
          <ActivityIndicator animating={loading} hidesWhenStopped={false} color="#34C759" />
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Keyboard ------------------------------------------------------------------

const ARROWS = {ArrowLeft: [-20, 0], ArrowRight: [20, 0], ArrowUp: [0, -20], ArrowDown: [0, 20]};

function keyLabel(e) {
  const {key, code, altKey, ctrlKey, metaKey, shiftKey, repeat} = e.nativeEvent;
  const mods = [ctrlKey && 'Ctrl', altKey && 'Alt', shiftKey && 'Shift', metaKey && 'Meta'].filter(Boolean);
  return `key "${key}" code ${code || '-'}${mods.length ? ' + ' + mods.join('+') : ''}${repeat ? ' (repeat)' : ''}`;
}

function FocusBox({label, style, ...props}) {
  const [focused, setFocused] = useState(false);
  return (
    <View
      focusable
      {...props}
      onFocus={e => {
        setFocused(true);
        props.onFocus?.(e);
      }}
      onBlur={e => {
        setFocused(false);
        props.onBlur?.(e);
      }}
      style={[styles.focusBox, focused && styles.focusBoxFocused, style]}>
      <Text style={styles.body}>{label}</Text>
      {props.children}
    </View>
  );
}

function Keyboard() {
  const log = useLog();
  const [last, setLast] = useState('nothing yet');
  const [dot, setDot] = useState({x: 90, y: 50});
  const box = useRef(null);
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Tab order"
        hint="Press Tab and Shift+Tab: focus moves through focusable views and GTK controls in tree order, with a focus ring. Enter or Space presses the focused Pressable or Button.">
        <View style={styles.row}>
          {['One', 'Two', 'Three'].map(name => (
            <HoverPressable
              key={name}
              onPress={() => log(`Pressable ${name} pressed`)}
              onFocus={() => log(`${name} focused`)}
              onBlur={() => log(`${name} blurred`)}
              style={({pressed, hovered}) => [
                styles.btn,
                {backgroundColor: '#007AFF', opacity: pressed ? 0.6 : hovered ? 0.85 : 1},
              ]}>
              <Text style={styles.btnText}>{name}</Text>
            </HoverPressable>
          ))}
          <TextInput style={[styles.input, {width: 160}]} placeholder="a TextInput" />
          <Switch value={true} />
          <Button title="Button" onPress={() => log('Button pressed')} />
          <Pressable focusable={false} onPress={() => log('not focusable pressed')} style={[styles.btn, {backgroundColor: '#8E8E93'}]}>
            <Text style={styles.btnText}>focusable={'{false}'}</Text>
          </Pressable>
        </View>
      </Section>
      <Section
        title="onKeyDown / onKeyUp"
        hint="This box has autoFocus. Click it (or Tab to it) and type: W3C key and code, with modifiers.">
        <FocusBox
          ref={box}
          autoFocus
          label={`Last: ${last}`}
          onKeyDown={e => setLast(`down ${keyLabel(e)}`)}
          onKeyUp={e => {
            setLast(`up ${keyLabel(e)}`);
            log(`keyUp ${keyLabel(e)}`);
          }}
        />
        <View style={styles.row}>
          <Btn title="ref.focus()" onPress={() => box.current?.focus()} />
          <Btn title="ref.blur()" color="#8E8E93" onPress={() => box.current?.blur()} />
        </View>
      </Section>
      <Section
        title="keyDownEvents"
        hint="This box lists the arrow keys in keyDownEvents, so it handles them itself (GTK would otherwise move focus with them): focus it and move the dot.">
        <FocusBox
          label="Arrow keys move the dot"
          style={{height: 120}}
          keyDownEvents={Object.keys(ARROWS).map(key => ({key}))}
          onKeyDown={e => {
            const d = ARROWS[e.nativeEvent.key];
            if (d) setDot(p => ({x: Math.max(0, Math.min(300, p.x + d[0])), y: Math.max(0, Math.min(90, p.y + d[1]))}));
          }}>
          <View pointerEvents="none" style={[styles.dot, {left: dot.x, top: dot.y}]} />
        </FocusBox>
        <FocusBox label="enableFocusRing={false}: no ring when focused" enableFocusRing={false} />
      </Section>
    </ScrollView>
  );
}

// ---- Accessibility ---------------------------------------------------------

function AccessibilityPage() {
  const log = useLog();
  const [wifi, setWifi] = useState(true);
  const [bold, setBold] = useState(false);
  const [level, setLevel] = useState(5);
  const [count, setCount] = useState(0);
  const [screenReader, setScreenReader] = useState(null);
  const [reduceMotion, setReduceMotion] = useState(null);
  const note = useRef(null);
  useEffect(() => {
    AccessibilityInfo.isScreenReaderEnabled().then(setScreenReader);
    AccessibilityInfo.isReduceMotionEnabled().then(setReduceMotion);
    const subs = [
      AccessibilityInfo.addEventListener('screenReaderChanged', v => {
        setScreenReader(v);
        log(`screenReaderChanged: ${v}`);
      }),
      AccessibilityInfo.addEventListener('reduceMotionChanged', v => {
        setReduceMotion(v);
        log(`reduceMotionChanged: ${v}`);
      }),
    ];
    return () => subs.forEach(s => s.remove());
  }, [log]);
  const adjust = name => {
    if (name === 'increment') setLevel(v => Math.min(10, v + 1));
    if (name === 'decrement') setLevel(v => Math.max(0, v - 1));
    log(`onAccessibilityAction ${name}`);
  };
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="What a screen reader sees"
        hint="Turn on Orca (Super+Alt+S, or Settings > Accessibility > Screen Reader) and Tab through, or inspect this window in Accerciser: roles, names, hints, states, values and actions come from the accessibility props.">
        <View accessibilityRole="header">
          <Text style={styles.sectionTitle}>A heading (accessibilityRole="header")</Text>
        </View>
        <View style={styles.wrap}>
          <Button title="Save" accessibilityHint="Saves the document" onPress={() => log('Save pressed')} />
          <Pressable
            role="checkbox"
            aria-checked={wifi}
            accessibilityLabel="Wi-Fi"
            onPress={() => setWifi(v => !v)}
            style={[styles.btn, {backgroundColor: '#5856D6'}]}>
            <Text style={styles.btnText}>Wi-Fi: {wifi ? 'on' : 'off'} (checkbox)</Text>
          </Pressable>
          <Pressable
            accessibilityRole="togglebutton"
            accessibilityState={{checked: bold}}
            accessibilityLabel="Bold"
            onPress={() => setBold(v => !v)}
            style={[styles.btn, {backgroundColor: bold ? '#007AFF' : '#8E8E93'}]}>
            <Text style={styles.btnText}>B (toggle button)</Text>
          </Pressable>
        </View>
        <View
          accessibilityRole="adjustable"
          accessibilityLabel="Level"
          accessibilityValue={{min: 0, max: 10, now: level}}
          accessibilityActions={[{name: 'increment'}, {name: 'decrement'}]}
          onAccessibilityAction={e => adjust(e.nativeEvent.actionName)}
          style={styles.row}>
          <Btn title="−" onPress={() => adjust('decrement')} />
          <Text style={styles.body}>Level {level} / 10 (an adjustable: a slider with a value and increment/decrement actions)</Text>
          <Btn title="+" onPress={() => adjust('increment')} />
        </View>
        <View accessible style={styles.pressBox}>
          <Text style={styles.body}>An accessible View</Text>
          <Text style={styles.hint}>is one element, named by its text</Text>
        </View>
        <View aria-hidden style={styles.pressBox}>
          <Text style={styles.body}>aria-hidden: screen readers skip this box</Text>
        </View>
        <View style={styles.row}>
          <Text nativeID="a11y-email" style={styles.body}>Email</Text>
          <TextInput accessibilityLabelledBy="a11y-email" style={[styles.input, {width: 240}]} placeholder="labelled by the text before it" />
        </View>
        <View style={styles.row} accessibilityLiveRegion="polite">
          <Text style={styles.body}>Live region: {count} clicks (announced when it changes)</Text>
        </View>
        <View style={styles.row}>
          <Btn title="click" onPress={() => setCount(n => n + 1)} />
        </View>
      </Section>
      <Section title="AccessibilityInfo">
        <Text style={styles.mono}>isScreenReaderEnabled() = {String(screenReader)}</Text>
        <Text style={styles.mono}>isReduceMotionEnabled() = {String(reduceMotion)} (GTK's gtk-enable-animations)</Text>
        <View style={styles.row}>
          <Btn
            title="announceForAccessibility"
            onPress={() => AccessibilityInfo.announceForAccessibility('Hello from React Native on GTK')}
          />
          <Btn
            title="setAccessibilityFocus"
            onPress={() => AccessibilityInfo.setAccessibilityFocus(findNodeHandle(note.current))}
          />
        </View>
        <View ref={note} accessible style={styles.pressBox}>
          <Text style={styles.body}>setAccessibilityFocus moves the screen reader here.</Text>
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Platform APIs ------------------------------------------------------------

function PlatformPage() {
  const log = useLog();
  const [initialURL, setInitialURL] = useState('…');
  const [appState, setAppState] = useState(AppState.currentState);
  const [clip, setClip] = useState('');
  const [rtl, setRtl] = useState(I18nManager.isRTL);
  const {fontScale, scale} = useWindowDimensions();
  useEffect(() => {
    Linking.getInitialURL().then(url => setInitialURL(url ?? 'none'));
    const subs = [
      Linking.addEventListener('url', ({url}) => log(`Linking url event: ${url}`)),
      AppState.addEventListener('change', s => {
        setAppState(s);
        log(`AppState ${s}`);
      }),
    ];
    return () => subs.forEach(s => s.remove());
  }, [log]);
  const open = url =>
    Linking.openURL(url).then(
      () => log(`opened ${url}`),
      e => log(`openURL failed: ${e.message}`),
    );
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Linking"
        hint="openURL uses the desktop's default handler. Start the app with a URL (rn-gtk-host --url, or an app's own command line / .desktop %u) for getInitialURL; a URL passed to a running app arrives as a 'url' event.">
        <Text style={styles.mono}>getInitialURL() = {initialURL}</Text>
        <View style={styles.row}>
          <Btn title="open reactnative.dev" onPress={() => open('https://reactnative.dev')} />
          <Btn title="mailto:" onPress={() => open('mailto:someone@example.com?subject=Hello')} />
          <Btn
            title="canOpenURL"
            onPress={async () => {
              for (const url of ['https://example.com', 'mailto:a@b.c', 'nosuchscheme:x']) {
                log(`canOpenURL(${url}) = ${await Linking.canOpenURL(url)}`);
              }
            }}
          />
        </View>
      </Section>
      <Section title="AppState" hint="Switch to another window and back: inactive while it isn't the active window, background when minimized (X11) or hidden.">
        <Text style={styles.mono}>AppState.currentState = {appState}</Text>
      </Section>
      <Section title="Clipboard" hint="GDK's clipboard: copy here, paste anywhere (and back).">
        <View style={styles.row}>
          <Btn title="setString('Hello from RN')" onPress={() => Clipboard.setString('Hello from RN')} />
          <Btn title="getString()" onPress={() => Clipboard.getString().then(setClip)} />
        </View>
        <Text style={styles.mono}>getString() = {JSON.stringify(clip)}</Text>
      </Section>
      <Section
        title="PixelRatio and font scale"
        hint="Settings > Accessibility > Large Text (or text-scaling-factor) scales Text, unless allowFontScaling={false}.">
        <Text style={styles.mono}>
          PixelRatio.get() = {PixelRatio.get()} · fontScale = {fontScale.toFixed(2)} · scale = {scale}
        </Text>
        <Text style={[styles.body, {fontSize: 18}]}>This text follows the font scale.</Text>
        <Text allowFontScaling={false} style={[styles.body, {fontSize: 18}]}>
          This one doesn't (allowFontScaling=false).
        </Text>
      </Section>
      <Section
        title="I18nManager"
        hint="Right-to-left comes from the locale; forceRTL is saved and applies at the next reload (Ctrl+R) or start, as on iOS.">
        <Text style={styles.mono}>
          isRTL = {String(I18nManager.isRTL)} · locale = {I18nManager.getConstants().localeIdentifier}
        </Text>
        <View style={styles.row}>
          <Btn
            title={rtl ? 'forceRTL(false)' : 'forceRTL(true)'}
            onPress={() => {
              I18nManager.forceRTL(!rtl);
              setRtl(!rtl);
              log(`forceRTL(${!rtl}): reload to apply`);
            }}
          />
          <View style={[styles.swatch, {width: 40, height: 40, backgroundColor: '#3584E4'}]} />
          <View style={[styles.swatch, {width: 40, height: 40, backgroundColor: '#E01B24'}]} />
          <Text style={styles.body}>(blue is first: on the right in RTL)</Text>
        </View>
      </Section>
      <Section title="Share and Vibration" hint="Linux has neither: Share resolves dismissed, Vibration does nothing.">
        <View style={styles.row}>
          <Btn
            title="Share.share"
            onPress={() => Share.share({message: 'Hello'}).then(r => log(`Share: ${r.action}`))}
          />
          <Btn
            title="Vibration.vibrate"
            onPress={() => {
              Vibration.vibrate();
              log('Vibration.vibrate(): no-op');
            }}
          />
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Modal -----------------------------------------------------------------

function ModalPage() {
  const log = useLog();
  const [open, setOpen] = useState(null);
  const [nested, setNested] = useState(false);
  const [animationType, setAnimationType] = useState('slide');
  const events = name => ({
    onShow: () => log(`${name}: onShow`),
    onDismiss: () => log(`${name}: onDismiss`),
    onRequestClose: () => {
      log(`${name}: onRequestClose`);
      if (name === 'nested') setNested(false);
      else setOpen(null);
    },
  });
  const style = open === 'sheet' ? 'formSheet' : open === 'page' ? 'pageSheet' : 'fullScreen';
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Modal"
        hint="Each opens as a window of its own over this one: modal, a dialog to Orca, focus moves in and comes back. Escape or the window's close button sends onRequestClose; closing plays the animation back, then onDismiss.">
        <Segmented
          options={['none', 'fade', 'slide'].map(t => [t, t])}
          value={animationType}
          onChange={setAnimationType} />
        <View style={styles.row}>
          <Btn title="Full screen" onPress={() => setOpen('full')} />
          <Btn title="Page sheet" onPress={() => setOpen('page')} />
          <Btn title="Form sheet" onPress={() => setOpen('sheet')} />
          <Btn title="Transparent" color="#5856D6" onPress={() => setOpen('clear')} />
        </View>
      </Section>
      <Modal
        visible={open === 'full' || open === 'page' || open === 'sheet'}
        animationType={animationType}
        presentationStyle={style}
        accessibilityLabel={`${style} modal`}
        {...events(style)}>
        <View style={[styles.page, {flex: 1, gap: 12, backgroundColor: PlatformColor('window_bg_color')}]}>
          <Text style={styles.heroTitle}>presentationStyle="{style}"</Text>
          <Text style={styles.body}>
            fullScreen fills the window it was opened from; the sheets are dialog-sized and can be
            resized (the content follows). Press Escape, use the close button, or:
          </Text>
          <TextInput style={styles.input} placeholder="Focus starts on the first control" />
          <View style={styles.row}>
            <Btn title="Open a modal over this one" onPress={() => setNested(true)} />
            <Btn title="Close" color="#FF3B30" onPress={() => setOpen(null)} />
          </View>
          <Modal
            visible={nested}
            animationType="fade"
            presentationStyle="formSheet"
            accessibilityLabel="Nested modal"
            {...events('nested')}>
            <View style={[styles.page, {flex: 1, gap: 12, backgroundColor: PlatformColor('window_bg_color')}]}>
              <Text style={styles.heroTitle}>A modal over a modal</Text>
              <Btn title="Close" onPress={() => setNested(false)} />
            </View>
          </Modal>
        </View>
      </Modal>
      <Modal visible={open === 'clear'} transparent animationType={animationType} {...events('transparent')}>
        <Pressable
          accessible={false}
          onPress={() => setOpen(null)}
          style={{flex: 1, backgroundColor: 'rgba(0,0,0,0.4)', alignItems: 'center', justifyContent: 'center'}}>
          <Pressable style={[styles.section, {width: 320, gap: 12}]}>
            <Text style={styles.sectionTitle}>transparent</Text>
            <Text style={styles.body}>
              The window paints nothing: the app shows through the backdrop. (X11 without a
              compositor can't, and dims the window instead.) Click outside to close.
            </Text>
            <Btn title="OK" onPress={() => setOpen(null)} />
          </Pressable>
        </Pressable>
      </Modal>
    </ScrollView>
  );
}

// ---- Dialogs ---------------------------------------------------------------

function DialogsPage() {
  const log = useLog();
  const [paths, setPaths] = useState('');
  const show = what => result => {
    log(`${what}: ${JSON.stringify(result)}`);
    setPaths(JSON.stringify(result, null, 1));
  };
  const fail = e => log(`failed: ${e.message}`);
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Alert"
        hint="A GTK message dialog over the window that's active (a Modal's too). Escape presses the cancel button; with none, cancelable lets it dismiss (onDismiss). Enter presses the default.">
        <View style={styles.row}>
          <Btn title="One button" onPress={() => Alert.alert('Saved', 'Your changes are saved.')} />
          <Btn
            title="Three buttons"
            onPress={() =>
              Alert.alert('Delete “notes.txt”?', 'It will be gone for good.', [
                {text: 'Keep', onPress: () => log('Alert: Keep')},
                {text: 'Delete', style: 'destructive', onPress: () => log('Alert: Delete')},
                {text: 'Cancel', style: 'cancel', onPress: () => log('Alert: Cancel')},
              ])
            }
          />
          <Btn
            title="Cancelable"
            onPress={() =>
              Alert.alert('Heads up', 'Press Escape to dismiss.', [{text: 'OK', onPress: () => log('Alert: OK')}], {
                cancelable: true,
                onDismiss: () => log('Alert: onDismiss'),
              })
            }
          />
        </View>
      </Section>
      <Section title="Alert.prompt" hint="plain-text, secure-text and login-password, with defaultValue and keyboardType.">
        <View style={styles.row}>
          <Btn
            title="Text"
            onPress={() => Alert.prompt('Your name', 'What should we call you?', t => log(`prompt: ${t}`), 'plain-text', 'Ada')}
          />
          <Btn
            title="Email"
            onPress={() =>
              Alert.prompt('Email', null, t => log(`email: ${t}`), 'plain-text', '', 'email-address')
            }
          />
          <Btn
            title="Password"
            onPress={() => Alert.prompt('Password', 'Unlock the vault', t => log(`password: ${'•'.repeat(t.length)}`), 'secure-text')}
          />
          <Btn
            title="Login"
            onPress={() =>
              Alert.prompt(
                'Sign in',
                'to example.com',
                [
                  {text: 'Cancel', style: 'cancel', onPress: () => log('login: cancelled')},
                  {text: 'Sign in', onPress: v => log(`login: ${v.login}`)},
                ],
                'login-password',
                'ada',
              )
            }
          />
        </View>
      </Section>
      <Section
        title="Dialogs (@curiosity26/react-native-gtk4)"
        hint="The desktop's file chooser (its portal), modal over the app. Paths come back; cancelling gives [] (null when saving). In 'Open several files', Ctrl+click or Shift+click files (or Ctrl+A), then Open: a double-click opens just that one.">
        <View style={styles.row}>
          <Btn
            title="Open a file"
            onPress={() =>
              Dialogs.openFile({
                title: 'Open an image or text file',
                filters: [
                  {name: 'Images', mimeTypes: ['image/*']},
                  {name: 'Text', extensions: ['txt', 'md']},
                ],
              }).then(show('openFile'), fail)
            }
          />
          <Btn title="Open several files" onPress={() => Dialogs.openFile({multiple: true}).then(show('openFile multiple'), fail)} />
          <Btn
            title="Save as…"
            onPress={() => Dialogs.saveFile({defaultName: 'untitled.txt', buttonLabel: 'Export'}).then(show('saveFile'), fail)}
          />
          <Btn title="Pick a folder" onPress={() => Dialogs.openFolder().then(show('openFolder'), fail)} />
        </View>
        {paths ? <Text style={styles.mono}>{paths}</Text> : null}
      </Section>
    </ScrollView>
  );
}

// ---- Menus -----------------------------------------------------------------

function MenusPage() {
  const log = useLog();
  const [bold, setBold] = useState(false);
  const [align, setAlign] = useState('left');
  const [bar, setBar] = useState(false);
  useEffect(() => {
    if (!bar) {
      MenuBar.clear();
      return;
    }
    MenuBar.setMenu([
      {
        title: 'File',
        items: [
          {title: 'New', shortcut: 'Ctrl+N', onSelect: () => log('MenuBar: New')},
          {title: 'Open…', shortcut: 'Ctrl+O', onSelect: () => log('MenuBar: Open')},
          {type: 'separator'},
          {title: 'Hide the menu bar', onSelect: () => setBar(false)},
        ],
      },
      {
        title: 'Format',
        items: [
          {title: 'Bold', shortcut: 'Ctrl+B', checked: bold, onSelect: () => setBold(b => !b)},
          {type: 'separator'},
          ...['left', 'center', 'right'].map(a => ({
            title: `Align ${a}`,
            type: 'radio',
            checked: align === a,
            onSelect: () => setAlign(a),
          })),
        ],
      },
    ]);
  }, [bar, bold, align, log]);
  useEffect(() => () => MenuBar.clear(), []);
  const formatItems = [
    {title: 'Bold', shortcut: 'Ctrl+B', checked: bold, onSelect: () => setBold(b => !b)},
    {
      title: 'Align',
      items: ['left', 'center', 'right'].map(a => ({
        title: a,
        type: 'radio',
        checked: align === a,
        onSelect: () => setAlign(a),
      })),
    },
    '-',
    {title: 'Copy text', onSelect: () => Clipboard.setString('Formatted text')},
    {title: 'Paste (disabled)', disabled: true},
  ];
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Context menus"
        hint="Right-click, or focus inside and press the Menu key or Shift+F10. Any View takes contextMenu; <ContextMenu> adds an onSelect for all items. Text fields keep GTK's own menu unless they have one.">
        <ContextMenu
          items={formatItems}
          onSelect={item => log(`ContextMenu: ${item.title}`)}
          style={[styles.pad, {height: 120, gap: 10, paddingHorizontal: 16}]}>
          <Text style={[styles.body, {fontWeight: bold ? 'bold' : 'normal', textAlign: align, alignSelf: 'stretch'}]}>
            Formatted text (bold {String(bold)}, aligned {align})
          </Text>
          <Pressable focusable style={styles.pressBox}>
            <Text style={styles.body}>Focus me, then press Menu</Text>
          </Pressable>
        </ContextMenu>
        <View style={styles.row}>
          <TextInput style={[styles.input, {flex: 1}]} placeholder="GTK's own menu (Cut, Copy, Paste, Emoji…)" />
          <TextInput
            style={[styles.input, {flex: 1}]}
            placeholder="A contextMenu of its own"
            contextMenu={[{title: 'Say hello', onSelect: () => log('TextInput menu: Say hello')}]}
          />
        </View>
      </Section>
      <Section
        title="Menu bar"
        hint="MenuBar.setMenu shows the app's menus under the title bar; their shortcuts work while they're closed. Call it again to update checkboxes and radio items.">
        <View style={styles.row}>
          <Btn title={bar ? 'Remove the menu bar' : 'Show a menu bar'} onPress={() => setBar(b => !b)} />
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Windows ---------------------------------------------------------------

// Shared by every window: they run in one JS runtime.
const notes = {text: 'Notes are shared between windows.', listeners: new Set()};

function useNotes() {
  const [text, setText] = useState(notes.text);
  useEffect(() => {
    const update = t => setText(t);
    notes.listeners.add(update);
    return () => notes.listeners.delete(update);
  }, []);
  const set = t => {
    notes.text = t;
    notes.listeners.forEach(l => l(t));
  };
  return [text, set];
}

// A window of its own (registered in index.js as ShowcaseWindow).
export function ShowcaseWindow({n}) {
  const window = useWindow();
  const {width, height} = useWindowDimensions();
  const [text, setText] = useNotes();
  return (
    <View style={[styles.page, {flex: 1, gap: 10, backgroundColor: PlatformColor('window_bg_color')}]}>
      <Text style={styles.sectionTitle}>Window {n} (id {window.id})</Text>
      <Text style={styles.mono}>
        useWindowDimensions() = {Math.round(width)} x {Math.round(height)} (this window's)
      </Text>
      <TextInput style={[styles.input, {minHeight: 80}]} value={text} onChangeText={setText} multiline />
      <View style={styles.row}>
        <Btn title="Rename" onPress={() => window.setTitle(`Window ${n}, renamed`)} />
        <Btn title="600 x 400" onPress={() => window.setSize(600, 400)} />
        <Btn title="Main window" onPress={() => Windows.main.focus()} />
        <Btn title="Close" color="#FF3B30" onPress={() => window.requestClose()} />
      </View>
    </View>
  );
}

let windowCount = 0;

function WindowsPage() {
  const log = useLog();
  const [text, setText] = useNotes();
  const [open, setOpen] = useState(() => Windows.getAll().filter(w => !w.main).length);
  const openWindow = interceptClose => {
    const n = ++windowCount;
    const w = Windows.open({
      component: 'ShowcaseWindow',
      initialProps: {n},
      title: `Window ${n}`,
      width: 480,
      height: 320,
      minWidth: 320,
      minHeight: 240,
      interceptClose,
    });
    setOpen(c => c + 1);
    w.addListener('focus', () => log(`window ${n}: focus`));
    w.addListener('resize', e => log(`window ${n}: resize ${e.width}x${e.height}`));
    w.addListener('close-requested', () => {
      log(`window ${n}: close-requested`);
      if (interceptClose) {
        Alert.alert(`Close window ${n}?`, null, [
          {text: 'Cancel', style: 'cancel'},
          {text: 'Close', style: 'destructive', onPress: () => w.close()},
        ]);
      }
    });
    w.addListener('closed', () => {
      log(`window ${n}: closed`);
      setOpen(c => c - 1);
    });
  };
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Windows (@curiosity26/react-native-gtk4)"
        hint="Each window shows a registered component as a surface of its own, in this JS runtime: edit the notes here or there. useWindowDimensions reports each window's own size. Closing this window while others are open hides it; the app quits with its last window.">
        <View style={styles.row}>
          <Btn title="Open a window" onPress={() => openWindow(false)} />
          <Btn title="Open one that asks before closing" onPress={() => openWindow(true)} />
          <Btn title="Rename this window" onPress={() => Windows.main.setTitle('Showcase — Windows')} />
        </View>
        <Text style={styles.mono}>{open} other window(s) open</Text>
        <TextInput style={[styles.input, {minHeight: 80}]} value={text} onChangeText={setText} multiline />
      </Section>
    </ScrollView>
  );
}

// ---- Drag and drop -----------------------------------------------------------

function DropZone({types, title}) {
  const log = useLog();
  const [over, setOver] = useState(false);
  const [dropped, setDropped] = useState(null);
  return (
    <View
      draggedTypes={types}
      onDragEnter={e => {
        setOver(true);
        log(`${title}: drag enter (${e.nativeEvent.dataTransfer.types.join(', ')})`);
      }}
      onDragLeave={() => setOver(false)}
      onDrop={e => {
        setOver(false);
        const t = e.nativeEvent.dataTransfer;
        log(`${title}: drop, ${t.files.length} file(s)`);
        setDropped(t);
      }}
      style={[
        styles.pad,
        {flex: 1, height: 190, padding: 12, borderStyle: 'dashed', borderWidth: 2},
        over && {borderColor: PlatformColor('accent_bg_color'), backgroundColor: PlatformColor('shade_color')},
      ]}>
      <Text style={styles.sectionTitle}>{title}</Text>
      <Text style={styles.hint}>draggedTypes={JSON.stringify(types)}</Text>
      {dropped?.files.slice(0, 3).map(f =>
        f.type.startsWith('image/') ? (
          <Image key={f.uri} source={{uri: f.uri}} style={{width: 48, height: 48, borderRadius: 4}} />
        ) : (
          <Text key={f.uri} style={styles.mono} numberOfLines={1}>
            {f.name} · {f.type} · {f.size} bytes
          </Text>
        ),
      )}
      {dropped?.urls?.map(u => (
        <Text key={u} style={styles.mono} numberOfLines={1}>
          {u}
        </Text>
      ))}
      {dropped?.text != null ? (
        <Text style={styles.mono} numberOfLines={3}>
          “{dropped.text}”
        </Text>
      ) : null}
    </View>
  );
}

function DragDropPage() {
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Dropping in"
        hint="Drag files from Files, links or text from a browser, or an image, onto these. Each takes what its draggedTypes say (react-native-macos' props), and gets onDragEnter, onDragLeave and onDrop.">
        <View style={styles.row}>
          <DropZone title="Files and links" types={['fileUrl']} />
          <DropZone title="Text" types={['string']} />
          <DropZone title="Images" types={['image', 'fileUrl']} />
        </View>
      </Section>
      <Section
        title="Dragging out"
        hint="Select some text, then drag the selection (to a text editor, a browser, or the Text zone above). The image has draggable: drag it to Files or an image editor.">
        <Text selectable style={styles.body}>
          Select a few words of this sentence and drag them somewhere else.
        </Text>
        <Image draggable source={halves} style={{width: 96, height: 96, borderRadius: 8}} />
      </Section>
    </ScrollView>
  );
}

// ---- Notifications ------------------------------------------------------------

function NotificationsPage() {
  const log = useLog();
  const [count, setCount] = useState(0);
  useEffect(() => {
    const sub = Notifications.addListener('press', e => log(`notification ${e.id}: ${e.action}`));
    return () => sub.remove();
  }, [log]);
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Notifications (@curiosity26/react-native-gtk4)"
        hint="GNOME's banners and notification list. Clicking one brings this window back and sends 'press'; buttons send their id. The same id replaces a notification. No system tray: stock GNOME has none.">
        <View style={styles.row}>
          <Btn
            title="Simple"
            onPress={() => Notifications.show({title: 'Hello from React Native', body: 'Sent with GNotification.'})}
          />
          <Btn
            title="With buttons"
            onPress={() =>
              Notifications.show({
                id: 'message',
                title: 'New message from Ada',
                body: 'Are we still on for Friday?',
                icon: 'mail-unread-symbolic',
                buttons: [
                  {id: 'reply', title: 'Reply'},
                  {id: 'later', title: 'Later'},
                ],
                onPress: action => log(`onPress: ${action}`),
              })
            }
          />
          <Btn
            title={`Count (${count})`}
            onPress={() => {
              setCount(c => c + 1);
              Notifications.show({id: 'counter', title: `Counted to ${count + 1}`, body: 'Same id: it replaces the last one.'});
            }}
          />
          <Btn
            title="Urgent"
            color="#FF3B30"
            onPress={() => Notifications.show({title: 'Battery low', body: '5% left', priority: 'urgent', icon: 'battery-caution-symbolic'})}
          />
          <Btn title="Withdraw 'With buttons'" onPress={() => Notifications.close('message')} />
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Lists -----------------------------------------------------------------

const ROWS = Array.from({length: 1000}, (_, i) => ({id: String(i), title: `Row ${i + 1}`}));
const SECTIONS = [
  {title: 'Fruit', data: ['Apple', 'Banana', 'Cherry']},
  {title: 'Vegetables', data: ['Carrot', 'Leek', 'Pea', 'Potato']},
  {title: 'Nuts', data: ['Almond', 'Cashew']},
];

function Lists() {
  const log = useLog();
  const [selected, setSelected] = useState(null);
  const [offset, setOffset] = useState(0);
  const list = useRef(null);
  const renderItem = useCallback(
    ({item}) => (
      <HoverPressable
        onPress={() => {
          setSelected(item.id);
          log(`tapped ${item.title}`);
        }}
        style={({hovered}) => [
          styles.listRow,
          item.id === selected && styles.listRowSelected,
          hovered && item.id !== selected && styles.listRowHover,
        ]}>
        <Text style={item.id === selected ? styles.btnText : styles.body}>{item.title}</Text>
      </HoverPressable>
    ),
    [selected, log],
  );
  return (
    <View style={[styles.page, {flex: 1}]}>
      <Section title="Horizontal ScrollView" hint="Scroll sideways with the wheel, touchpad or scrollbar.">
        <ScrollView horizontal style={{height: 56}}>
          {Array.from({length: 30}, (_, i) => (
            <View key={i} style={[styles.chip, {backgroundColor: `hsl(${i * 12}, 70%, 55%)`}]}>
              <Text style={styles.btnText}>{i + 1}</Text>
            </View>
          ))}
        </ScrollView>
      </Section>
      <View style={[styles.row, {flex: 1, alignItems: 'stretch'}]}>
        <View style={styles.listCol}>
          <Text style={styles.sectionTitle}>FlatList, 1000 rows</Text>
          <Text style={styles.hint}>
            Scroll, then tap a row. offset {Math.round(offset)}
          </Text>
          <View style={styles.row}>
            <Btn title="top" onPress={() => list.current?.scrollToOffset({offset: 0})} />
            <Btn title="row 500" onPress={() => list.current?.scrollToIndex({index: 499})} />
            <Btn title="end" onPress={() => list.current?.scrollToEnd()} />
          </View>
          <FlatList
            ref={list}
            style={styles.list}
            data={ROWS}
            extraData={selected}
            renderItem={renderItem}
            getItemLayout={(_, index) => ({length: 40, offset: 40 * index, index})}
            onScroll={e => setOffset(e.nativeEvent.contentOffset.y)}
            scrollEventThrottle={16}
          />
        </View>
        <View style={styles.listCol}>
          <Text style={styles.sectionTitle}>SectionList</Text>
          <Text style={styles.hint}>Headers stick to the top as you scroll.</Text>
          <SectionList
            style={styles.list}
            sections={[...SECTIONS, ...SECTIONS.map(s => ({...s, title: `${s.title} again`}))]}
            keyExtractor={(item, i) => item + i}
            stickySectionHeadersEnabled
            renderSectionHeader={({section}) => (
              <Text style={styles.sectionHeader}>{section.title}</Text>
            )}
            renderItem={({item}) => (
              <Pressable onPress={() => log(`picked ${item}`)} style={styles.listRow}>
                <Text style={styles.body}>{item}</Text>
              </Pressable>
            )}
          />
        </View>
      </View>
    </View>
  );
}

// ---- Images -----------------------------------------------------------------

function Images() {
  const log = useLog();
  const modes = ['cover', 'contain', 'stretch', 'center', 'repeat'];
  const [mode, setMode] = useState(0);
  const [status, setStatus] = useState('loading…');
  const [nonce, setNonce] = useState(0);
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section title="resizeMode" hint="Cycle through the modes on one image.">
        <View style={styles.row}>
          <Image
            source={modes[mode] === 'repeat' ? tile : halves}
            resizeMode={modes[mode]}
            style={styles.imageBig}
          />
          <Btn title={`resizeMode: ${modes[mode]}`} onPress={() => setMode(m => (m + 1) % modes.length)} />
        </View>
      </Section>
      <Section title="Styling">
        <View style={styles.wrap}>
          <Image source={halves} style={[styles.image, {borderRadius: 50}]} resizeMode="cover" />
          <Image source={halves} style={styles.image} tintColor="#FF2D55" />
          <Image source={halves} style={[styles.image, {opacity: 0.4}]} />
          <Image source={halves} style={[styles.image, {borderWidth: 4, borderColor: '#007AFF'}]} />
        </View>
      </Section>
      <Section title="Network image" hint="Loaded over HTTPS with libsoup. Reload to fetch it again.">
        <View style={styles.row}>
          <Image
            key={nonce}
            source={{uri: `${LOGO}?n=${nonce}`}}
            style={styles.image}
            onLoadStart={() => setStatus('loading…')}
            onLoad={e => {
              const {width, height} = e.nativeEvent.source;
              setStatus(`loaded ${width}x${height}`);
              log('network image loaded');
            }}
            onError={e => {
              setStatus(`error: ${e.nativeEvent.error}`);
              log('network image failed');
            }}
          />
          <Text style={styles.body}>{status}</Text>
          <Btn title="reload image" onPress={() => setNonce(n => n + 1)} />
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Animation ---------------------------------------------------------------

function Animation() {
  const log = useLog();
  const spin = useRef(new Animated.Value(0)).current;
  const slide = useRef(new Animated.Value(0)).current;
  const pulse = useRef(new Animated.Value(1)).current;
  const [spinning, setSpinning] = useState(true);
  const slid = useRef(false);
  const loop = useRef(null);

  useEffect(() => {
    if (!spinning) return;
    spin.setValue(0);
    loop.current = Animated.loop(
      Animated.timing(spin, {
        toValue: 1,
        duration: 1500,
        easing: Easing.linear,
        useNativeDriver: true,
      }),
    );
    loop.current.start();
    return () => loop.current?.stop();
  }, [spinning, spin]);

  const rotate = spin.interpolate({inputRange: [0, 1], outputRange: ['0deg', '360deg']});
  const translateX = slide.interpolate({inputRange: [0, 1], outputRange: [0, 300]});

  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section title="Native driver" hint="GTK's frame clock drives this, not JS.">
        <View style={styles.row}>
          <Animated.View style={[styles.spinner, {transform: [{rotate}]}]} />
          <Btn title={spinning ? 'stop' : 'spin'} onPress={() => setSpinning(s => !s)} />
        </View>
      </Section>
      <Section title="Spring" hint="Slide the box across and back.">
        <Animated.View style={[styles.slider, {transform: [{translateX}]}]} />
        <View style={styles.row}>
          <Btn
            title="slide"
            onPress={() => {
              slid.current = !slid.current;
              Animated.spring(slide, {
                toValue: slid.current ? 1 : 0,
                useNativeDriver: true,
              }).start(() => log('spring finished'));
            }}
          />
        </View>
      </Section>
      <Section title="Sequence" hint="Fade out, scale up, and back.">
        <Animated.View
          style={[styles.pulse, {opacity: pulse, transform: [{scale: Animated.subtract(2, pulse)}]}]}
        />
        <View style={styles.row}>
          <Btn
            title="pulse"
            onPress={() =>
              Animated.sequence([
                Animated.timing(pulse, {toValue: 0.3, duration: 300, useNativeDriver: true}),
                Animated.timing(pulse, {toValue: 1, duration: 300, useNativeDriver: true}),
              ]).start(() => log('pulse finished'))
            }
          />
        </View>
      </Section>
    </ScrollView>
  );
}

// ---- Networking --------------------------------------------------------------

function Network() {
  const log = useLog();
  const [state, setState] = useState({kind: 'idle'});
  const load = async () => {
    setState({kind: 'loading'});
    const started = Date.now();
    try {
      const res = await fetch(MOVIES);
      const json = await res.json();
      setState({kind: 'done', status: res.status, json, ms: Date.now() - started});
      log(`fetch ${res.status} in ${Date.now() - started} ms`);
    } catch (e) {
      setState({kind: 'error', message: String(e)});
      log('fetch failed');
    }
  };
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section title="fetch" hint={`GETs ${MOVIES} with libsoup.`}>
        <View style={styles.row}>
          <Btn title="fetch movies" onPress={load} />
          {state.kind === 'loading' ? <ActivityIndicator /> : null}
        </View>
        {state.kind === 'done' ? (
          <View>
            <Text style={styles.mono}>
              HTTP {state.status} in {state.ms} ms: {state.json.title}
            </Text>
            {state.json.movies.map(m => (
              <Text key={m.id} style={styles.body}>
                {m.title} ({m.releaseYear})
              </Text>
            ))}
          </View>
        ) : null}
        {state.kind === 'error' ? <Text style={{color: '#FF3B30'}}>{state.message}</Text> : null}
      </Section>
    </ScrollView>
  );
}

// ---- Appearance ---------------------------------------------------------------

// Harness-only (rn-gtk-host): flips GNOME's own dark style.
const desktop = TurboModuleRegistry.get('ShowcaseDesktop');

const SCHEMES = [
  ['System', 'unspecified'],
  ['Light', 'light'],
  ['Dark', 'dark'],
];

const NAMED_COLORS = [
  'window_bg_color',
  'window_fg_color',
  'view_bg_color',
  'view_fg_color',
  'card_bg_color',
  'headerbar_bg_color',
  'sidebar_bg_color',
  'accent_bg_color',
  'accent_color',
  'destructive_bg_color',
  'success_bg_color',
  'warning_bg_color',
  'error_color',
  'borders',
];

function Segmented({options, value, onChange}) {
  return (
    <View style={styles.segmented}>
      {options.map(([label, key]) => (
        <Pressable
          key={key}
          onPress={() => onChange(key)}
          style={[styles.segment, key === value && styles.segmentActive]}>
          <Text style={[styles.segmentText, key === value && styles.segmentTextActive]}>
            {label}
          </Text>
        </Pressable>
      ))}
    </View>
  );
}

function AppearancePage() {
  const log = useLog();
  const scheme = useColorScheme();
  // Appearance has no getter for the override; the Showcase remembers it.
  const [override, setOverride] = useState(appearanceOverride);
  const [events, setEvents] = useState(0);
  const [gnome, setGnome] = useState(() => desktop?.getColorScheme() ?? null);
  useEffect(() => {
    const sub = Appearance.addChangeListener(({colorScheme}) => {
      setEvents(n => n + 1);
      setGnome(desktop?.getColorScheme() ?? null);
      log(`appearanceChanged: ${colorScheme}`);
    });
    return () => sub.remove();
  }, [log]);
  const gnomeDark = gnome === 'prefer-dark';
  return (
    <ScrollView contentContainerStyle={styles.page}>
      <Section
        title="Color scheme"
        hint="System follows the desktop's style; Light and Dark override it for this app (Appearance.setColorScheme).">
        <Segmented
          options={SCHEMES}
          value={override}
          onChange={key => {
            appearanceOverride = key;
            setOverride(key);
            Appearance.setColorScheme(key);
            log(`setColorScheme('${key}')`);
          }}
        />
        <Text style={styles.mono}>useColorScheme() = {scheme}</Text>
        <Text style={styles.mono}>Appearance.getColorScheme() = {Appearance.getColorScheme()}</Text>
        <Text style={styles.mono}>change events on this page: {events}</Text>
      </Section>
      <Section
        title="Follow the system"
        hint="With System selected above, flip GNOME's own dark style (Settings > Appearance writes the same setting). The host follows it through the XDG Settings portal.">
        {desktop && gnome != null ? (
          <View style={styles.row}>
            <Btn
              title={gnomeDark ? "Turn GNOME's dark style off" : "Turn GNOME's dark style on"}
              onPress={() => {
                const next = gnomeDark ? 'default' : 'prefer-dark';
                desktop.setColorScheme(next);
                setGnome(next);
                log(`gsettings color-scheme ${next}`);
              }}
            />
            <Text style={styles.body}>
              org.gnome.desktop.interface color-scheme = '{gnome}' · read from{' '}
              {desktop.getAppearanceSource()}
            </Text>
          </View>
        ) : (
          <Text style={styles.body}>
            Not available here (GNOME's settings schema or rn-gtk-host's ShowcaseDesktop module is missing).
          </Text>
        )}
      </Section>
      <Section
        title="PlatformColor"
        hint="libadwaita's named colors: PlatformColor('window_bg_color'). They repaint when the scheme changes.">
        <View style={styles.wrap}>
          {NAMED_COLORS.map(name => (
            <View key={name} style={styles.namedColor}>
              <View style={[styles.namedSwatch, {backgroundColor: PlatformColor(name)}]} />
              <Text style={styles.namedLabel}>{name}</Text>
            </View>
          ))}
        </View>
      </Section>
      <Section title="GTK widgets" hint="GTK draws these from its theme's light or dark variant.">
        <View style={styles.row}>
          <Switch value={true} />
          <Switch value={false} />
          <ActivityIndicator />
          <Button title="Button" onPress={() => log('Button onPress')} />
          <Button title="Suggested" color="#3584E4" onPress={() => log('suggested Button onPress')} />
        </View>
        <TextInput
          style={styles.input}
          placeholder="Type, then right-click for GTK's menu"
          placeholderTextColor="#8E8E93"
        />
      </Section>
    </ScrollView>
  );
}

// Kept across page switches.
let appearanceOverride = 'unspecified';

// ---- Shell -------------------------------------------------------------------

const PAGES = [
  ['Home', Home],
  ['Appearance', AppearancePage],
  ['Views & Text', ViewsText],
  ['Buttons', Buttons],
  ['Inputs', Inputs],
  ['Keyboard', Keyboard],
  ['Accessibility', AccessibilityPage],
  ['Lists', Lists],
  ['Images', Images],
  ['Animation', Animation],
  ['Network', Network],
  ['Platform', PlatformPage],
  ['Modal', ModalPage],
  ['Dialogs', DialogsPage],
  ['Menus', MenusPage],
  ['Windows', WindowsPage],
  ['Drag & Drop', DragDropPage],
  ['Notifications', NotificationsPage],
  // Community libraries' Linux ports (packages/): ShowcaseLibraries.js.
  ...makeLibraryPages(() => ({Section, Btn, styles, useLog})),
];

// initialPage (a page name or index) opens that page first, e.g.
// rn-gtk-host --module Showcase --initial-props '{"initialPage":"Lists"}';
// colorScheme ('light' or 'dark') starts with that override.
export default function Showcase({initialPage = 0, colorScheme}) {
  useState(() => {
    if (colorScheme) {
      appearanceOverride = colorScheme;
      Appearance.setColorScheme(colorScheme);
    }
  });
  const [page, setPage] = useState(() => {
    const i = PAGES.findIndex(([name]) => name === initialPage);
    return i >= 0 ? i : Number(initialPage) || 0;
  });
  const [events, setEvents] = useState([]);
  const log = useCallback(msg => {
    const t = new Date().toLocaleTimeString();
    setEvents(e => [`${t}  ${msg}`, ...e].slice(0, 4));
  }, []);
  const Page = PAGES[page][1];
  return (
    <LogContext.Provider value={log}>
      <View style={styles.root}>
        <ScrollView style={styles.sidebar} contentContainerStyle={styles.sidebarContent}>
          <Text style={styles.brand}>RN GTK4</Text>
          {PAGES.map(([name], i) => (
            <HoverPressable
              key={name}
              onPress={() => setPage(i)}
              style={({hovered}) => [
                styles.nav,
                i === page && styles.navActive,
                hovered && i !== page && styles.navHover,
              ]}>
              <Text style={[styles.navText, i === page && styles.navTextActive]}>{name}</Text>
            </HoverPressable>
          ))}
        </ScrollView>
        <View style={styles.main}>
          <View style={styles.content}>
            <Page />
          </View>
          <View style={styles.log}>
            <Text style={styles.logTitle}>Event log</Text>
            {events.length === 0 ? (
              <Text style={styles.logLine}>Nothing yet. Interact with the page above.</Text>
            ) : (
              events.map((e, i) => (
                <Text key={i} style={[styles.logLine, i === 0 && {color: '#FFFFFF'}]}>
                  {e}
                </Text>
              ))
            )}
          </View>
        </View>
      </View>
    </LogContext.Provider>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, flexDirection: 'row', backgroundColor: PlatformColor('window_bg_color')},
  sidebar: {width: 180, flexGrow: 0, backgroundColor: '#1C1C1E'},
  sidebarContent: {paddingTop: 16, paddingHorizontal: 8, paddingBottom: 16},
  brand: {color: '#FFFFFF', fontSize: 18, fontWeight: 'bold', marginBottom: 16, marginLeft: 8},
  nav: {paddingVertical: 10, paddingHorizontal: 12, borderRadius: 8, marginBottom: 2, cursor: 'pointer'},
  navActive: {backgroundColor: '#007AFF'},
  navHover: {backgroundColor: '#3A3A3C'},
  navText: {color: '#C7C7CC', fontSize: 14},
  navTextActive: {color: '#FFFFFF', fontWeight: '600'},
  main: {flex: 1},
  content: {flex: 1},
  log: {height: 110, backgroundColor: '#2C2C2E', padding: 10},
  logTitle: {color: '#8E8E93', fontSize: 11, fontWeight: 'bold', marginBottom: 4},
  logLine: {color: '#AEAEB2', fontFamily: 'monospace', fontSize: 12},
  page: {padding: 20},
  hero: {
    alignItems: 'center',
    padding: 32,
    backgroundColor: PlatformColor('card_bg_color'),
    borderRadius: 16,
    borderWidth: 2,
    borderColor: '#007AFF',
    marginBottom: 16,
  },
  heroTitle: {fontSize: 36, fontWeight: 'bold', color: PlatformColor('card_fg_color')},
  heroSub: {marginTop: 8, fontSize: 16, color: '#6E6E73'},
  section: {backgroundColor: PlatformColor('card_bg_color'), borderRadius: 12, padding: 16, marginBottom: 16},
  sectionTitle: {fontSize: 17, fontWeight: '600', color: PlatformColor('card_fg_color')},
  hint: {fontSize: 13, color: '#8E8E93', marginTop: 2},
  sectionBody: {marginTop: 12, gap: 8},
  sectionHeader: {
    backgroundColor: PlatformColor('headerbar_bg_color'),
    color: PlatformColor('headerbar_fg_color'),
    paddingVertical: 4,
    paddingHorizontal: 12,
    fontWeight: '600',
  },
  body: {fontSize: 14, color: PlatformColor('card_fg_color')},
  fg: {color: PlatformColor('card_fg_color')},
  mono: {fontFamily: 'monospace', fontSize: 13, color: PlatformColor('card_fg_color')},
  row: {flexDirection: 'row', alignItems: 'center', gap: 12, flexWrap: 'wrap'},
  wrap: {flexDirection: 'row', flexWrap: 'wrap', gap: 16, alignItems: 'center'},
  swatch: {width: 80, height: 80, backgroundColor: '#E5E5EA'},
  btn: {paddingVertical: 8, paddingHorizontal: 14, borderRadius: 8, cursor: 'pointer'},
  btnText: {color: '#FFFFFF', fontWeight: '600'},
  bigBtn: {padding: 20, borderRadius: 12, alignItems: 'center', cursor: 'pointer'},
  touchable: {padding: 12, borderRadius: 8, backgroundColor: '#5856D6'},
  pad: {
    height: 160,
    borderRadius: 12,
    backgroundColor: PlatformColor('view_bg_color'),
    borderWidth: 1,
    borderColor: PlatformColor('borders'),
    alignItems: 'center',
    justifyContent: 'center',
    overflow: 'hidden',
  },
  dot: {position: 'absolute', width: 16, height: 16, borderRadius: 8, backgroundColor: '#FF2D55'},
  input: {
    borderWidth: 1,
    borderColor: PlatformColor('borders'),
    borderRadius: 8,
    paddingHorizontal: 10,
    paddingVertical: 8,
    fontSize: 14,
    backgroundColor: PlatformColor('view_bg_color'),
    color: PlatformColor('view_fg_color'),
  },
  chip: {width: 48, height: 48, borderRadius: 24, marginRight: 8, alignItems: 'center', justifyContent: 'center'},
  listCol: {flex: 1, backgroundColor: PlatformColor('card_bg_color'), borderRadius: 12, padding: 12, gap: 6},
  list: {flex: 1, borderTopWidth: 1, borderColor: PlatformColor('borders')},
  listRow: {height: 40, justifyContent: 'center', paddingHorizontal: 12, cursor: 'pointer'},
  listRowSelected: {backgroundColor: PlatformColor('accent_bg_color')},
  listRowHover: {backgroundColor: PlatformColor('shade_color')},
  image: {width: 100, height: 100, backgroundColor: '#E5E5EA'},
  imageBig: {width: 260, height: 160, backgroundColor: '#E5E5EA'},
  spinner: {width: 80, height: 80, borderRadius: 12, backgroundColor: '#FF9500'},
  slider: {width: 60, height: 60, borderRadius: 30, backgroundColor: '#34C759'},
  focusBox: {
    padding: 14,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: PlatformColor('borders'),
    backgroundColor: PlatformColor('view_bg_color'),
    overflow: 'hidden',
  },
  focusBoxFocused: {borderColor: PlatformColor('accent_bg_color')},
  mouseBox: {
    width: 220,
    height: 70,
    borderRadius: 10,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: PlatformColor('view_bg_color'),
    borderWidth: 1,
    borderColor: PlatformColor('borders'),
  },
  mouseBoxInside: {backgroundColor: PlatformColor('accent_bg_color')},
  pressBox: {padding: 10, borderRadius: 8, backgroundColor: PlatformColor('shade_color')},
  segmented: {
    flexDirection: 'row',
    alignSelf: 'flex-start',
    borderRadius: 8,
    padding: 3,
    gap: 2,
    backgroundColor: PlatformColor('shade_color'),
  },
  segment: {paddingVertical: 6, paddingHorizontal: 18, borderRadius: 6, cursor: 'pointer'},
  segmentActive: {backgroundColor: PlatformColor('accent_bg_color')},
  segmentText: {color: PlatformColor('card_fg_color'), fontWeight: '600'},
  segmentTextActive: {color: PlatformColor('accent_fg_color')},
  namedColor: {width: 130, alignItems: 'center', gap: 4},
  namedSwatch: {
    width: 64,
    height: 40,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: PlatformColor('borders'),
  },
  namedLabel: {fontSize: 11, color: PlatformColor('card_fg_color')},
  pulse: {width: 80, height: 80, borderRadius: 40, backgroundColor: '#AF52DE', alignSelf: 'flex-start', margin: 20},
});
