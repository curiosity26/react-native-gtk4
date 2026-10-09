// The Showcase's pages for community libraries with Linux ports in this
// repository's packages/ (each upstream library plus its
// @curiosity26/react-native-gtk4-<name> package, autolinked by run-linux
// and package-linux). The repository's rn-gtk-host doesn't autolink: there
// the pages say the native side is missing.
//   cd examples/hello-world && npx react-native run-linux
import React, {useCallback, useEffect, useState} from 'react';
import {ScrollView, StyleSheet, Text, TextInput, View, useWindowDimensions} from 'react-native';

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

  return [
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
});
