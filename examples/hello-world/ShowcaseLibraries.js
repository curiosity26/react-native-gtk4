// The Showcase's pages for community libraries with Linux ports in this
// repository's packages/ (each upstream library plus its
// @curiosity26/react-native-gtk4-<name> package, autolinked by run-linux
// and package-linux). The repository's rn-gtk-host doesn't autolink: there
// the pages say the native side is missing.
//   cd examples/hello-world && npx react-native run-linux
import React, {useCallback, useEffect, useState} from 'react';
import {Animated, Easing, Image, ScrollView, StyleSheet, Text, TextInput, View, useWindowDimensions} from 'react-native';

// Loaded when a page opens: netinfo throws at import without its native
// module.
function load(name) {
  try {
    switch (name) {
      case 'async-storage':
        return require('@react-native-async-storage/async-storage');
      case 'netinfo':
        return require('@react-native-community/netinfo');
      case 'safe-area':
        return require('react-native-safe-area-context');
      case 'ionicons':
        return require('@react-native-vector-icons/ionicons');
      case 'fontawesome6':
        return require('@react-native-vector-icons/fontawesome6');
      case 'svg':
        return require('react-native-svg');
      case 'webview':
        return require('react-native-webview');
      case 'navigation':
        return require('./GalleryNavigation');
      case 'worklets':
        return require('./GalleryWorklets');
      case 'reanimated':
        return {
          ...require('react-native-gesture-handler'),
          ...require('./GalleryReanimated'),
          ...require('./GalleryReanimatedLayout'),
        };
      case 'gestures':
        return {...require('react-native-gesture-handler'), ...require('./GalleryGestures')};
    }
  } catch (e) {
    return {error: e};
  }
  return null;
}

// Once per run of the app, however often the page opens.
let launch = null;
function countLaunch(AsyncStorage) {
  launch ??= (async () => {
    const n = Number(await AsyncStorage.getItem('launches')) + 1;
    await AsyncStorage.setItem('launches', String(n));
    return n;
  })();
  return launch;
}

// `helpers` returns the Showcase's Section, Btn, styles and useLog (read
// when the pages render: Showcase.js defines them after its page list).
export function makeLibraryPages(helpers) {
  function Missing({what, error}) {
    const {styles} = helpers();
    return (
      <Text style={styles.hint}>
        {what} isn't available here: {String(error?.message || error)}. The packaged Showcase
        (npx react-native package-linux) and run-linux autolink it; rn-gtk-host doesn't.
      </Text>
    );
  }

  // ---- Storage: @react-native-async-storage/async-storage -------------------

  function StoragePage() {
    const {Section, Btn, styles, useLog} = helpers();
    const log = useLog();
    const lib = load('async-storage');
    const [launches, setLaunches] = useState(null);
    const [key, setKey] = useState('greeting');
    const [value, setValue] = useState('Hello from GTK');
    const [shown, setShown] = useState('');
    const AsyncStorage = lib?.default;
    const db = React.useMemo(() => (lib?.createAsyncStorage ? lib.createAsyncStorage('showcase') : null), [lib]);
    useEffect(() => {
      if (!AsyncStorage) return;
      countLaunch(AsyncStorage).then(setLaunches, e => setLaunches(`error: ${e.message}`));
    }, [AsyncStorage]);
    const run = useCallback(
      async (label, fn) => {
        try {
          const r = await fn();
          const text = r === undefined ? 'done' : JSON.stringify(r);
          setShown(`${label}: ${text}`);
          log(`async-storage ${label}: ${text}`);
        } catch (e) {
          setShown(`${label} failed: ${e.message}`);
        }
      },
      [log],
    );
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="AsyncStorage (@react-native-async-storage/async-storage)"
          hint="The library's own JS on a Linux TurboModule (packages/async-storage): each database is a JSON file under $XDG_DATA_HOME/<app id>/async-storage/, written atomically. Values survive restarts.">
          {lib?.error || !AsyncStorage ? (
            <Missing what="AsyncStorage" error={lib?.error || 'no module'} />
          ) : (
            <>
              <Text style={styles.mono}>
                Launches counted in the default storage: {launches === null ? '…' : String(launches)}
              </Text>
              <View style={[styles.row, {gap: 8}]}>
                <TextInput style={local.input} value={key} onChangeText={setKey} placeholder="key" />
                <TextInput style={[local.input, {flex: 2}]} value={value} onChangeText={setValue} placeholder="value" />
              </View>
              <View style={styles.row}>
                <Btn title="Save" onPress={() => run(`setItem(${key})`, () => db.setItem(key, value))} />
                <Btn title="Load" onPress={() => run(`getItem(${key})`, () => db.getItem(key))} />
                <Btn title="Remove" onPress={() => run(`removeItem(${key})`, () => db.removeItem(key))} />
                <Btn title="All keys" onPress={() => run('getAllKeys()', () => db.getAllKeys())} />
                <Btn title="Clear" color="#FF3B30" onPress={() => run('clear()', () => db.clear())} />
              </View>
              <Text style={styles.mono}>{shown || 'The "showcase" database: save a value, restart the app, load it.'}</Text>
            </>
          )}
        </Section>
      </ScrollView>
    );
  }

  // ---- NetInfo: @react-native-community/netinfo -----------------------------

  function NetInfoPage() {
    const {Section, Btn, styles, useLog} = helpers();
    const log = useLog();
    const lib = load('netinfo');
    const [state, setState] = useState(null);
    const NetInfo = lib?.default;
    useEffect(() => {
      if (!NetInfo) return;
      const unsubscribe = NetInfo.addEventListener(s => {
        setState(s);
        log(`netinfo: ${s.type}, connected ${s.isConnected}, internet ${s.isInternetReachable}`);
      });
      return unsubscribe;
    }, [NetInfo, log]);
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="NetInfo (@react-native-community/netinfo)"
          hint="On GIO's GNetworkMonitor (packages/netinfo): connected, internet reachable (connectivity FULL) and metered (isConnectionExpensive), with the type, SSID and address from NetworkManager when the system bus has it. Turn Wi-Fi or the network off in Settings to see an event.">
          {lib?.error || !NetInfo ? (
            <Missing what="NetInfo" error={lib?.error || 'no module'} />
          ) : (
            <>
              <View style={styles.row}>
                <Btn title="Refresh" onPress={() => NetInfo.refresh().then(setState)} />
              </View>
              <Text style={styles.mono}>{state ? JSON.stringify(state, null, 2) : 'Waiting for the first state…'}</Text>
            </>
          )}
        </Section>
      </ScrollView>
    );
  }

  // ---- Safe area: react-native-safe-area-context ----------------------------

  function SafeAreaPage() {
    const {Section, styles} = helpers();
    const lib = load('safe-area');
    const window = useWindowDimensions();
    if (lib?.error || !lib?.SafeAreaProvider) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="Safe area (react-native-safe-area-context)">
            <Missing what="react-native-safe-area-context" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    const {SafeAreaProvider, SafeAreaView, initialWindowMetrics, useSafeAreaInsets, useSafeAreaFrame} = lib;
    function Metrics() {
      const insets = useSafeAreaInsets();
      const frame = useSafeAreaFrame();
      return (
        <>
          <Text style={styles.mono}>useSafeAreaInsets() = {JSON.stringify(insets)}</Text>
          <Text style={styles.mono}>useSafeAreaFrame() = {JSON.stringify(frame)}</Text>
        </>
      );
    }
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="Safe area (react-native-safe-area-context)"
          hint="The package's own components, with initialWindowMetrics from a Linux TurboModule (packages/safe-area-context). A desktop window has nothing over its content, so the insets are zero; the frame follows the window (resize it).">
          <Text style={styles.mono}>initialWindowMetrics = {JSON.stringify(initialWindowMetrics)}</Text>
          <Text style={styles.mono}>
            window = {Math.round(window.width)} x {Math.round(window.height)}
          </Text>
          <SafeAreaProvider initialMetrics={initialWindowMetrics} style={local.provider}>
            <SafeAreaView edges={['top', 'bottom', 'left', 'right']} style={local.safeArea}>
              <Metrics />
              <Text style={styles.hint}>Inside a SafeAreaView (the blue border).</Text>
            </SafeAreaView>
          </SafeAreaProvider>
        </Section>
      </ScrollView>
    );
  }

  // ---- Icons: @react-native-vector-icons/* ----------------------------------

  const IONICONS = ['home', 'settings', 'heart', 'star', 'search', 'camera', 'mail', 'notifications', 'cloud-download', 'logo-github', 'rocket', 'trash'];
  const FA_SOLID = ['house', 'gear', 'heart', 'star', 'magnifying-glass', 'camera', 'envelope', 'bell'];
  const FA_BRANDS = ['github', 'linux', 'react', 'ubuntu', 'fedora', 'gitlab'];

  function IconsPage() {
    const {Section, styles} = helpers();
    const ion = load('ionicons');
    const fa = load('fontawesome6');
    const Ionicons = ion?.Ionicons || ion?.default;
    const FontAwesome6 = fa?.FontAwesome6 || fa?.default;
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="Ionicons (@react-native-vector-icons/ionicons)"
          hint="Icon fonts from the app's node_modules, copied next to it and registered with fontconfig at startup (packages/vector-icons). Text finds them by the PostScript name the library sets as fontFamily.">
          {!Ionicons ? (
            <Missing what="Ionicons" error={ion?.error || 'no module'} />
          ) : (
            <View style={local.grid}>
              {IONICONS.map(name => (
                <View key={name} style={local.cell}>
                  <Ionicons name={name} size={32} color="#007AFF" />
                  <Text style={local.caption}>{name}</Text>
                </View>
              ))}
            </View>
          )}
        </Section>
        <Section
          title="Font Awesome 6 (@react-native-vector-icons/fontawesome6)"
          hint="Three fonts, one family: 'FontAwesome6Free-Solid' and 'FontAwesome6Brands-Regular' are PostScript names, which fontconfig only knows as aliases the package adds.">
          {!FontAwesome6 ? (
            <Missing what="FontAwesome6" error={fa?.error || 'no module'} />
          ) : (
            <>
              <View style={local.grid}>
                {FA_SOLID.map(name => (
                  <View key={name} style={local.cell}>
                    <FontAwesome6 name={name} iconStyle="solid" size={28} color="#34C759" />
                    <Text style={local.caption}>{name}</Text>
                  </View>
                ))}
              </View>
              <View style={local.grid}>
                {FA_BRANDS.map(name => (
                  <View key={name} style={local.cell}>
                    <FontAwesome6 name={name} iconStyle="brand" size={28} color="#AF52DE" />
                    <Text style={local.caption}>{name}</Text>
                  </View>
                ))}
              </View>
            </>
          )}
        </Section>
      </ScrollView>
    );
  }

  // ---- SVG: react-native-svg ---------------------------------------------------

  const LOGO_XML = `<svg viewBox="0 0 100 100"><circle cx="50" cy="50" r="45" fill="#20232a"/>
    <g fill="none" stroke="#61dafb" stroke-width="4"><ellipse cx="50" cy="50" rx="38" ry="14"/>
    <ellipse cx="50" cy="50" rx="38" ry="14" transform="rotate(60 50 50)"/>
    <ellipse cx="50" cy="50" rx="38" ry="14" transform="rotate(120 50 50)"/></g>
    <circle cx="50" cy="50" r="7" fill="#61dafb"/></svg>`;

  function SvgPage() {
    const {Section, Btn, styles, useLog} = helpers();
    const log = useLog();
    const lib = load('svg');
    const [spinning, setSpinning] = useState(true);
    const [png, setPng] = useState(null);
    const ref = React.useRef(null);
    if (lib?.error || !lib?.Svg) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="SVG (react-native-svg)">
            <Missing what="react-native-svg" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    const AnimatedSvg = animatedSvgParts(lib);
    const {Svg, Circle, Rect, Path, G, Text: SvgText, TSpan, Defs, LinearGradient, RadialGradient, Stop, ClipPath, Pattern, Line, Ellipse, Polygon, Filter, FeGaussianBlur, FeOffset, Use, SvgXml} = lib;
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="SVG (react-native-svg)"
          hint="The library's components on Linux (packages/svg): each <Svg> turns its elements back into an SVG document, which librsvg draws. Shapes, paths, gradients, clip paths, patterns, text, filters and <Use>.">
          <View style={[styles.row, {flexWrap: 'wrap', gap: 12}]}>
            <Svg width={160} height={120} viewBox="0 0 160 120" ref={ref}>
              <Defs>
                <LinearGradient id="sky" x1="0" y1="0" x2="1" y2="1">
                  <Stop offset="0" stopColor="#007AFF" />
                  <Stop offset="1" stopColor="#AF52DE" />
                </LinearGradient>
                <RadialGradient id="sun" cx="50%" cy="50%" r="50%">
                  <Stop offset="0" stopColor="#FFD60A" />
                  <Stop offset="1" stopColor="#FF9500" stopOpacity="0.6" />
                </RadialGradient>
              </Defs>
              <Rect x="0" y="0" width="160" height="120" rx="12" fill="url(#sky)" />
              <Circle cx="120" cy="36" r="22" fill="url(#sun)" />
              <Path d="M0 120 L50 60 L80 90 L110 55 L160 120 Z" fill="#34C759" stroke="#1E7F3A" strokeWidth="2" />
            </Svg>
            <Svg width={120} height={120} viewBox="-60 -60 120 120">
              <AnimatedSvg.SpinningG spinning={spinning}>
                <Polygon points="0,-50 14,-15 50,-15 21,6 32,42 0,20 -32,42 -21,6 -50,-15 -14,-15" fill="#FF3B30" stroke="#8E1E17" strokeWidth="3" strokeLinejoin="round" />
              </AnimatedSvg.SpinningG>
            </Svg>
            <Svg width={120} height={120}>
              <Defs>
                <ClipPath id="clip">
                  <Circle cx="60" cy="60" r="50" />
                </ClipPath>
                <Pattern id="stripes" width="12" height="12" patternUnits="userSpaceOnUse">
                  <Rect width="6" height="12" fill="#5856D6" />
                </Pattern>
              </Defs>
              <Rect width="120" height="120" fill="url(#stripes)" clipPath="url(#clip)" />
              <Line x1="10" y1="110" x2="110" y2="10" stroke="#FF9500" strokeWidth="6" strokeLinecap="round" strokeDasharray="12 10" />
            </Svg>
            <Svg width={180} height={120}>
              <Defs>
                <Filter id="shadow">
                  <FeOffset in="SourceAlpha" dx="3" dy="4" result="moved" />
                  <FeGaussianBlur in="moved" stdDeviation="3" />
                </Filter>
                <Ellipse id="pill" cx="40" cy="30" rx="34" ry="18" />
              </Defs>
              <Use href="#pill" x="4" y="2" fill="#000" opacity="0.4" filter="url(#shadow)" />
              <Use href="#pill" fill="#FF2D55" />
              <SvgText x="10" y="90" fontSize="22" fontWeight="bold" fill="#007AFF">
                Hello <TSpan fill="#FF9500" fontStyle="italic">SVG</TSpan>
              </SvgText>
            </Svg>
            <SvgXml xml={LOGO_XML} width={120} height={120} />
          </View>
          <AnimatedSvg.Demo log={log} styles={styles} />
          <View style={styles.row}>
            <Btn title={spinning ? 'Stop the star' : 'Spin the star'} onPress={() => setSpinning(on => !on)} />
            <Btn
              title="toDataURL()"
              onPress={() =>
                ref.current?.toDataURL(data => {
                  setPng(data);
                  log(`svg toDataURL: ${data.length} base64 characters`);
                })
              }
            />
          </View>
          {png ? (
            <View style={styles.row}>
              <Text style={styles.hint}>The first drawing, back as a PNG:</Text>
              <Image source={{uri: `data:image/png;base64,${png}`}} style={{width: 80, height: 60}} />
            </View>
          ) : null}
        </Section>
      </ScrollView>
    );
  }

  // Animated SVG: a circle's radius on the native driver (the C++ Animated
  // module, frame by frame on GTK's frame clock), and a ring's dash offset on
  // the JS driver (react-native-svg's setNativeProps, a commit a frame).
  const svgAnimated = new WeakMap();
  function animatedSvgParts(lib) {
    if (svgAnimated.has(lib)) return svgAnimated.get(lib);
    const AnimatedCircle = Animated.createAnimatedComponent(lib.Circle);
    function Demo({log, styles}) {
      const radius = React.useRef(new Animated.Value(10)).current;
      const dash = React.useRef(new Animated.Value(0)).current;
      useEffect(() => {
        const loops = [
          Animated.loop(
            Animated.sequence([
              Animated.timing(radius, {toValue: 34, duration: 900, easing: Easing.inOut(Easing.quad), useNativeDriver: true}),
              Animated.timing(radius, {toValue: 10, duration: 900, easing: Easing.inOut(Easing.quad), useNativeDriver: true}),
            ]),
          ),
          Animated.loop(Animated.timing(dash, {toValue: 1, duration: 1600, easing: Easing.linear, useNativeDriver: false})),
        ];
        loops.forEach(l => l.start());
        return () => loops.forEach(l => l.stop());
      }, [radius, dash]);
      const C = 2 * Math.PI * 34;
      return (
        <View style={styles.row}>
          <lib.Svg width={90} height={90}>
            <AnimatedCircle cx="45" cy="45" r={radius} fill="#FF2D55" />
          </lib.Svg>
          <lib.Svg width={90} height={90}>
            <lib.Circle cx="45" cy="45" r="34" stroke="#E5E5EA" strokeWidth="8" fill="none" />
            <AnimatedCircle
              cx="45"
              cy="45"
              r="34"
              stroke="#007AFF"
              strokeWidth="8"
              fill="none"
              strokeLinecap="round"
              strokeDasharray={`${C * 0.3} ${C}`}
              strokeDashoffset={dash.interpolate({inputRange: [0, 1], outputRange: [0, -C]})}
            />
          </lib.Svg>
          <Text style={styles.hint}>Native driver (radius) and JS driver (dash offset).</Text>
        </View>
      );
    }
    // A <G> turning once every 3 seconds: an animated `rotation`, which
    // react-native-svg's G.setNativeProps turns into its matrix each frame
    // (the JS driver: rotation isn't a native-driver prop).
    const AnimatedG = Animated.createAnimatedComponent(lib.G);
    function SpinningG({spinning, children}) {
      const angle = React.useRef(new Animated.Value(0)).current;
      useEffect(() => {
        if (!spinning) return;
        let from = 0;
        angle.stopAnimation(v => (from = v % 360));
        angle.setValue(from);
        const loop = Animated.loop(
          Animated.timing(angle, {toValue: from + 360, duration: 3000, easing: Easing.linear, useNativeDriver: false}),
        );
        loop.start();
        return () => loop.stop();
      }, [spinning, angle]);
      return <AnimatedG rotation={angle}>{children}</AnimatedG>;
    }
    const parts = {Demo, SpinningG};
    svgAnimated.set(lib, parts);
    return parts;
  }

  // ---- WebView: react-native-webview ---------------------------------------------

  const PAGE = `<!doctype html><html><body style="font-family: sans-serif; padding: 12px">
    <h3>Hello from WebKitGTK</h3>
    <button onclick="window.ReactNativeWebView.postMessage('clicked at ' + new Date().toLocaleTimeString())">Send a message to React Native</button>
    <p id="got">Nothing from React Native yet.</p>
    <script>window.addEventListener('message', e => { document.getElementById('got').textContent = 'From React Native: ' + e.data; });</script>
    </body></html>`;

  function WebViewPage() {
    const {Section, Btn, styles, useLog} = helpers();
    const log = useLog();
    const lib = load('webview');
    const htmlRef = React.useRef(null);
    const webRef = React.useRef(null);
    const [nav, setNav] = useState(null);
    const WebView = lib?.WebView || lib?.default;
    if (lib?.error || !WebView) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="WebView (react-native-webview)">
            <Missing what="react-native-webview" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="WebView (react-native-webview)"
          hint="The library's own JS (its iOS variant) on WebKitGTK 6.0 (packages/webview). The page and React Native talk through window.ReactNativeWebView.postMessage and webView.postMessage.">
          <View style={local.web}>
            <WebView
              ref={htmlRef}
              originWhitelist={['*']}
              source={{html: PAGE}}
              onMessage={e => log(`webview message: ${e.nativeEvent.data}`)}
              onLoadEnd={() => log('webview (html) loaded')}
            />
          </View>
          <View style={styles.row}>
            <Btn title="postMessage to the page" onPress={() => htmlRef.current?.postMessage(`hello at ${new Date().toLocaleTimeString()}`)} />
            <Btn title="injectJavaScript" onPress={() => htmlRef.current?.injectJavaScript("document.body.style.background = '#E5F1FF'; window.ReactNativeWebView.postMessage('the injected script ran'); true;")} />
          </View>
        </Section>
        <Section title="A web page" hint="reactnative.dev, with the navigation state from onNavigationStateChange.">
          <View style={styles.row}>
            <Btn title="Back" onPress={() => webRef.current?.goBack()} />
            <Btn title="Forward" onPress={() => webRef.current?.goForward()} />
            <Btn title="Reload" onPress={() => webRef.current?.reload()} />
          </View>
          <Text style={styles.mono}>
            {nav ? `${nav.loading ? 'loading' : 'loaded'} ${nav.url} (back ${nav.canGoBack}, forward ${nav.canGoForward})` : '…'}
          </Text>
          <View style={[local.web, {height: 320}]}>
            <WebView
              ref={webRef}
              source={{uri: 'https://reactnative.dev'}}
              onNavigationStateChange={setNav}
              onError={e => log(`webview error: ${e.nativeEvent.description}`)}
            />
          </View>
        </Section>
      </ScrollView>
    );
  }

  // ---- Navigation: react-native-screens + React Navigation ------------------

  function NavigationPage() {
    const {Section, styles, useLog} = helpers();
    const log = useLog();
    const lib = load('navigation');
    if (lib?.error || !lib?.NavigationDemo) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="Navigation (react-native-screens)">
            <Missing what="React Navigation" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    const {NavigationDemo} = lib;
    return (
      <View style={[styles.page, {flex: 1}]}>
        <Section
          title="Navigation (@react-navigation/native-stack, bottom-tabs; react-native-screens)"
          hint="A native stack with header options (title, colors, header buttons, a search bar, no header), a modal, a form sheet, a guarded screen (usePreventRemove) and tabs. With packages/screens it's libadwaita's navigation view and header bars (slide transitions; back with the header's button, a swipe, Escape, Alt+Left or the mouse's back button); without it, react-native-screens' web components."
        />
        <NavigationDemo onLog={log} style={{padding: 0}} />
      </View>
    );
  }

  // ---- Gestures: react-native-gesture-handler --------------------------------

  function GesturesPage() {
    const {Section, styles, useLog} = helpers();
    const log = useLog();
    const lib = load('gestures');
    if (lib?.error || !lib?.GesturesDemo) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="Gestures (react-native-gesture-handler)">
            <Missing what="react-native-gesture-handler" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    const {GestureHandlerRootView, GesturesDemo, GesturesRelations} = lib;
    return (
      <GestureHandlerRootView style={{flex: 1}}>
        <ScrollView contentContainerStyle={styles.page}>
          <Section
            title="Gestures (react-native-gesture-handler)"
            hint="The library's own JS on its native side for Linux (packages/gesture-handler): recognizers ported from its web implementation, running on GTK's main thread. Tap, double tap, long press, pan, fling and hover on GestureDetectors (the hook API), the builder API; a pan over a Pressable cancels its press."
          />
          <GesturesDemo onLog={log} />
          <Section
            title="Relations, buttons, scrolling"
            hint="Pinch and rotate together (a touchpad pinch, or two fingers on a touchscreen); a pan that waits for a double tap to fail; a manual gesture activated from JS; RectButton and Touchable (press feedback); the library's ScrollView, whose native gesture activates when it scrolls."
          />
          <GesturesRelations onLog={log} />
        </ScrollView>
      </GestureHandlerRootView>
    );
  }

  // ---- Worklets: react-native-worklets ---------------------------------------

  function WorkletsPage() {
    const {Section, styles} = helpers();
    const lib = load('worklets');
    if (lib?.error || !lib?.WorkletsDemo) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="Worklets (react-native-worklets)">
            <Missing what="react-native-worklets" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    const {WorkletsDemo} = lib;
    return (
      <ScrollView contentContainerStyle={styles.page}>
        <Section
          title="Worklets (react-native-worklets)"
          hint="The library's shared C++ on Linux (packages/worklets): 'worklet' functions (its Babel plugin) run on its UI runtime, a Hermes runtime of its own on GTK's main thread, and call back to JS; synchronizables are shared between them; requestAnimationFrame on the UI runtime follows the window's frame clock."
        />
        <WorkletsDemo />
      </ScrollView>
    );
  }

  // ---- Reanimated: react-native-reanimated ------------------------------------

  function ReanimatedPage() {
    const {Section, styles} = helpers();
    const lib = load('reanimated');
    if (lib?.error || !lib?.ReanimatedAnimations) {
      return (
        <ScrollView contentContainerStyle={styles.page}>
          <Section title="Reanimated (react-native-reanimated)">
            <Missing what="react-native-reanimated" error={lib?.error || 'no module'} />
          </Section>
        </ScrollView>
      );
    }
    const {
      GestureHandlerRootView,
      ReanimatedAnimations,
      ReanimatedInteraction,
      ReanimatedBench,
      ReanimatedLayoutDemo,
      ReanimatedCSSDemo,
      ReanimatedSwipeDrawerDemo,
    } = lib;
    return (
      <GestureHandlerRootView style={{flex: 1}}>
        <ScrollView contentContainerStyle={styles.page}>
          <Section
            title="Reanimated (react-native-reanimated)"
            hint="The library's C++ engine on Linux (packages/reanimated), on react-native-worklets' UI runtime: shared values and animated styles run on GTK's main thread, frame by frame on the window's frame clock. The last box keeps moving while the JS thread is blocked."
          />
          <ReanimatedAnimations />
          <Section
            title="Scrolling, measuring, gestures"
            hint="useAnimatedScrollHandler drives the bar; measure and scrollTo run on the UI runtime; the red box follows a pan with worklets only, and springs back."
          />
          <ReanimatedInteraction />
          <Section title="Many views" hint="Each box has its own animated style; the frame rate is what the UI runtime's useFrameCallback sees." />
          <ReanimatedBench />
          <Section
            title="Layout animations"
            hint="Items fade or slide in (entering), fade or zoom out (exiting), and the others move to make room (layout transitions)."
          />
          <ReanimatedLayoutDemo />
          <Section
            title="CSS animations and transitions"
            hint="Keyframes with an iteration count and direction; width and opacity transitioning when the style changes."
          />
          <ReanimatedCSSDemo />
          <Section
            title="Swipeable and drawer"
            hint="gesture-handler's ReanimatedSwipeable (swipe the row left) and ReanimatedDrawerLayout."
          />
          <ReanimatedSwipeDrawerDemo />
        </ScrollView>
      </GestureHandlerRootView>
    );
  }

  return [
    ['Navigation', NavigationPage],
    ['Gestures', GesturesPage],
    ['Worklets', WorkletsPage],
    ['Reanimated', ReanimatedPage],
    ['SVG', SvgPage],
    ['WebView', WebViewPage],
    ['Storage', StoragePage],
    ['NetInfo', NetInfoPage],
    ['Safe Area', SafeAreaPage],
    ['Icons', IconsPage],
  ];
}

const local = StyleSheet.create({
  input: {
    flex: 1,
    borderWidth: 1,
    borderColor: '#C7C7CC',
    borderRadius: 6,
    paddingHorizontal: 8,
    paddingVertical: 4,
  },
  provider: {height: 140, marginTop: 8},
  safeArea: {flex: 1, borderWidth: 2, borderColor: '#007AFF', borderRadius: 8, padding: 8, gap: 4},
  grid: {flexDirection: 'row', flexWrap: 'wrap', gap: 8},
  cell: {width: 96, alignItems: 'center', paddingVertical: 8, gap: 4},
  caption: {fontSize: 11, color: '#8E8E93'},
  web: {height: 200, borderWidth: 1, borderColor: '#C7C7CC', borderRadius: 8, overflow: 'hidden'},
});
