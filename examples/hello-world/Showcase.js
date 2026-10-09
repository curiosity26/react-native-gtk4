// A hands-on tour of every Phase 1 component. Each page says what to try,
// and the log at the bottom shows the events that reach JS.
//   npm run start:hello-world
//   npm run dev:hello-world -- --module Showcase --width 1100 --height 780
import React, {useCallback, useEffect, useRef, useState} from 'react';
import {
  ActivityIndicator,
  Animated,
  Button,
  Easing,
  FlatList,
  Image,
  Platform,
  Pressable,
  ScrollView,
  SectionList,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  TouchableHighlight,
  TouchableOpacity,
  TouchableWithoutFeedback,
  View,
  useWindowDimensions,
} from 'react-native';

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

// Not shown yet: the color scheme (no Appearance module).
function Home() {
  const c = Platform.constants;
  const window = useWindowDimensions();
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
              {backgroundColor: '#fff', boxShadow: '0 6px 16px rgba(0,0,0,0.35)', borderRadius: 12},
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
        <Text style={{fontSize: 24, fontWeight: 'bold'}}>Bold 24</Text>
        <Text style={{fontStyle: 'italic', color: '#FF3B30'}}>Italic red</Text>
        <Text style={{textDecorationLine: 'underline line-through'}}>Underline and strike</Text>
        <Text style={{letterSpacing: 4, textTransform: 'uppercase'}}>letter spaced</Text>
        <Text style={{fontFamily: 'monospace'}}>monospace 0123456789</Text>
        <Text>
          Nested: <Text style={{fontWeight: 'bold'}}>bold</Text>,{' '}
          <Text style={{color: '#007AFF'}} onPress={() => log('nested link pressed')}>
            a tappable link
          </Text>
          , and <Text style={{backgroundColor: '#FFEB3B'}}>highlighted</Text>.
        </Text>
        <Text style={{textAlign: 'right'}}>Right aligned</Text>
        <Text>Unicode: Привет · こんにちは · مرحبا · 👋🎉</Text>
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
        hint="Right-click for Copy, then paste into a field on the Inputs page. Drag selection isn't supported yet.">
        <Text selectable style={styles.body}>
          This paragraph is selectable: right-click it and choose Copy to put
          the whole paragraph on the clipboard.
        </Text>
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
      <Section title="Touchables and Button">
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
          {/* Button's own styles are iOS/Android only: give it a color. */}
          <Button title="Button" color="#D1D1D6" onPress={() => log('Button onPress')} />
          <Button
            title="Disabled"
            color="#D1D1D6"
            disabled
            onPress={() => log('should not fire')}
          />
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

// ---- Shell -------------------------------------------------------------------

const PAGES = [
  ['Home', Home],
  ['Views & Text', ViewsText],
  ['Buttons', Buttons],
  ['Inputs', Inputs],
  ['Lists', Lists],
  ['Images', Images],
  ['Animation', Animation],
  ['Network', Network],
];

// initialPage (a page name or index) opens that page first, e.g.
// rn-gtk-host --module Showcase --initial-props '{"initialPage":"Lists"}'.
export default function Showcase({initialPage = 0}) {
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
        <View style={styles.sidebar}>
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
        </View>
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
  root: {flex: 1, flexDirection: 'row', backgroundColor: '#F5F5F7'},
  sidebar: {width: 180, backgroundColor: '#1C1C1E', paddingTop: 16, paddingHorizontal: 8},
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
    backgroundColor: '#FFFFFF',
    borderRadius: 16,
    borderWidth: 2,
    borderColor: '#007AFF',
    marginBottom: 16,
  },
  heroTitle: {fontSize: 36, fontWeight: 'bold', color: '#1C1C1E'},
  heroSub: {marginTop: 8, fontSize: 16, color: '#6E6E73'},
  section: {backgroundColor: '#FFFFFF', borderRadius: 12, padding: 16, marginBottom: 16},
  sectionTitle: {fontSize: 17, fontWeight: '600', color: '#1C1C1E'},
  hint: {fontSize: 13, color: '#8E8E93', marginTop: 2},
  sectionBody: {marginTop: 12, gap: 8},
  sectionHeader: {backgroundColor: '#E5E5EA', paddingVertical: 4, paddingHorizontal: 12, fontWeight: '600'},
  body: {fontSize: 14, color: '#1C1C1E'},
  mono: {fontFamily: 'monospace', fontSize: 13, color: '#1C1C1E'},
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
    backgroundColor: '#F2F2F7',
    borderWidth: 1,
    borderColor: '#D1D1D6',
    alignItems: 'center',
    justifyContent: 'center',
    overflow: 'hidden',
  },
  dot: {position: 'absolute', width: 16, height: 16, borderRadius: 8, backgroundColor: '#FF2D55'},
  input: {
    borderWidth: 1,
    borderColor: '#C7C7CC',
    borderRadius: 8,
    paddingHorizontal: 10,
    paddingVertical: 8,
    fontSize: 14,
    backgroundColor: '#FFFFFF',
    color: '#1C1C1E',
  },
  chip: {width: 48, height: 48, borderRadius: 24, marginRight: 8, alignItems: 'center', justifyContent: 'center'},
  listCol: {flex: 1, backgroundColor: '#FFFFFF', borderRadius: 12, padding: 12, gap: 6},
  list: {flex: 1, borderTopWidth: 1, borderColor: '#E5E5EA'},
  listRow: {height: 40, justifyContent: 'center', paddingHorizontal: 12, cursor: 'pointer'},
  listRowSelected: {backgroundColor: '#007AFF'},
  listRowHover: {backgroundColor: '#F2F2F7'},
  image: {width: 100, height: 100, backgroundColor: '#E5E5EA'},
  imageBig: {width: 260, height: 160, backgroundColor: '#E5E5EA'},
  spinner: {width: 80, height: 80, borderRadius: 12, backgroundColor: '#FF9500'},
  slider: {width: 60, height: 60, borderRadius: 30, backgroundColor: '#34C759'},
  pulse: {width: 80, height: 80, borderRadius: 40, backgroundColor: '#AF52DE', alignSelf: 'flex-start', margin: 20},
});
