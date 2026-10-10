// React Navigation's native-stack and bottom tabs on Linux, through
// react-native-screens: a stack with header options (title, colors, a
// header button, a hidden header), a modal, and a tab navigator. The
// status line shows the stack and the focus events.
// rn-gtk-host --module GalleryNavigation --self-test pushes and pops
// screens (buttons, the header's back button, Alt+Left), opens and
// closes the modal and switches tabs.
import React, {useCallback, useRef, useState} from 'react';
import {Pressable, StyleSheet, Text, View, useColorScheme} from 'react-native';
import {
  DarkTheme,
  DefaultTheme,
  NavigationContainer,
  useFocusEffect,
  useNavigationContainerRef,
  usePreventRemove,
} from '@react-navigation/native';
import {createNativeStackNavigator} from '@react-navigation/native-stack';
import {createBottomTabNavigator} from '@react-navigation/bottom-tabs';

const Stack = createNativeStackNavigator();
const Tabs = createBottomTabNavigator();
const LogContext = React.createContext(() => {});

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

// Logs the screen's focus and blur (React Navigation's own events, which
// the stack has to keep in step with what's shown).
function useFocusLog(name) {
  const log = React.useContext(LogContext);
  useFocusEffect(
    useCallback(() => {
      log(`focus ${name}`);
      return () => log(`blur ${name}`);
    }, [log, name]),
  );
}

function HomeScreen({navigation}) {
  useFocusLog('Home');
  return (
    <View style={styles.screen}>
      <Text style={styles.title}>Home screen</Text>
      <View style={styles.row}>
        <Action id="nav-push" label="Push details" onPress={() => navigation.navigate('Details', {n: 1})} />
        <Action id="nav-modal" label="Open modal" onPress={() => navigation.navigate('Modal')} />
        <Action id="nav-tabs" label="Open tabs" onPress={() => navigation.navigate('Tabs')} />
        <Action id="nav-bare" label="No header" onPress={() => navigation.navigate('Bare')} />
        <Action id="nav-sheet" label="Form sheet" onPress={() => navigation.navigate('Sheet')} />
        <Action id="nav-guarded" label="Guarded" onPress={() => navigation.navigate('Guarded')} />
      </View>
    </View>
  );
}

function DetailsScreen({navigation, route}) {
  const n = route.params?.n ?? 1;
  useFocusLog(`Details ${n}`);
  const log = React.useContext(LogContext);
  // A search bar in the header (headerSearchBarOptions).
  React.useLayoutEffect(() => {
    navigation.setOptions({
      headerSearchBarOptions: {
        placeholder: 'Search details',
        onChangeText: e => log(`search "${e.nativeEvent.text}"`),
      },
    });
  }, [navigation, log]);
  return (
    <View style={styles.screen}>
      <Text style={styles.title}>Details screen #{n}</Text>
      <View style={styles.row}>
        <Action id="nav-push-more" label="Push another" onPress={() => navigation.push('Details', {n: n + 1})} />
        <Action id="nav-back" label="Go back" onPress={() => navigation.goBack()} />
        <Action id="nav-pop-top" label="Pop to top" onPress={() => navigation.popToTop()} />
      </View>
    </View>
  );
}

function ModalScreen({navigation}) {
  useFocusLog('Modal');
  return (
    <View style={[styles.screen, styles.modal]}>
      <Text style={styles.title}>Modal screen</Text>
      <Action id="nav-close" label="Close" onPress={() => navigation.goBack()} />
    </View>
  );
}

function SheetScreen({navigation}) {
  useFocusLog('Sheet');
  return (
    <View style={[styles.screen, styles.sheet]}>
      <Text style={styles.title}>Sheet screen</Text>
      <Action id="nav-sheet-close" label="Close" onPress={() => navigation.goBack()} />
    </View>
  );
}

// Going back is refused (usePreventRemove) until "Leave": the native stack
// can't pop it itself then (no swipe or Escape), its back button asks.
function GuardedScreen({navigation}) {
  useFocusLog('Guarded');
  const log = React.useContext(LogContext);
  const [leaving, setLeaving] = useState(false);
  usePreventRemove(!leaving, () => log('remove prevented'));
  React.useEffect(() => {
    if (leaving) navigation.goBack();
  }, [leaving, navigation]);
  return (
    <View style={styles.screen}>
      <Text style={styles.title}>Guarded screen</Text>
      <Action id="nav-guard-leave" label="Leave" onPress={() => setLeaving(true)} />
    </View>
  );
}

function BareScreen({navigation}) {
  useFocusLog('Bare');
  return (
    <View style={[styles.screen, styles.bare]}>
      <Text style={styles.title}>Screen without a header</Text>
      <Action id="nav-bare-back" label="Back" onPress={() => navigation.goBack()} />
    </View>
  );
}

function FeedTab() {
  useFocusLog('Feed');
  return (
    <View style={styles.screen}>
      <Text style={[styles.title, {color: '#FF3B30'}]}>Feed tab</Text>
    </View>
  );
}

function SettingsTab() {
  useFocusLog('Settings');
  return (
    <View style={styles.screen}>
      <Text style={styles.title}>Settings tab</Text>
    </View>
  );
}

function TabsScreen() {
  return (
    <Tabs.Navigator screenOptions={{headerShown: false, tabBarIcon: () => null, tabBarIconStyle: {display: 'none'}}}>
      <Tabs.Screen name="Feed" component={FeedTab} options={{tabBarAccessibilityLabel: 'Feed tab button'}} />
      <Tabs.Screen
        name="Settings"
        component={SettingsTab}
        options={{tabBarAccessibilityLabel: 'Settings tab button', tabBarBadge: 3}}
      />
    </Tabs.Navigator>
  );
}

// The focused route of each navigator, outermost first.
function describe(state) {
  const names = [];
  const label = r => (r.params?.n ? `${r.name} ${r.params.n}` : r.name);
  for (let s = state; s; s = s.routes[s.index]?.state) {
    const shown = s.type === 'stack' ? s.routes.slice(0, s.index + 1) : [s.routes[s.index]];
    names.push(shown.map(label).join(' > '));
  }
  return names.join(' / ');
}

// `onLog` (the Showcase's event log) gets each focus event too; `pushed`
// (a number) starts with that many Details screens on the stack, `open` (a
// screen's name) with that screen over Home.
export function NavigationDemo({onLog, style, pushed = 0, open}) {
  const scheme = useColorScheme();
  const [stack, setStack] = useState('Home');
  const [events, setEvents] = useState([]);
  const navigation = useNavigationContainerRef();
  const logRef = useRef(onLog);
  logRef.current = onLog;
  const log = useCallback(msg => {
    setEvents(e => [...e, msg].slice(-4));
    logRef.current?.(`navigation: ${msg}`);
  }, []);
  return (
    <LogContext.Provider value={log}>
      <View style={[styles.root, style]}>
        <Text nativeID="nav-stack" style={styles.status}>
          Stack: {stack}
        </Text>
        <Text style={styles.status}>Events: {events.join(', ') || '-'}</Text>
        <View style={styles.frame}>
          <NavigationContainer
            ref={navigation}
            onReady={() => setStack(describe(navigation.getRootState()))}
            initialState={
              pushed > 0
                ? {
                    index: pushed,
                    routes: [{name: 'Home'}, ...Array.from({length: pushed}, (_, i) => ({name: 'Details', params: {n: i + 1}}))],
                  }
                : open
                  ? {index: 1, routes: [{name: 'Home'}, {name: open}]}
                  : undefined
            }
            theme={scheme === 'dark' ? DarkTheme : DefaultTheme}
            onStateChange={state => setStack(describe(state))}>
            <Stack.Navigator>
              <Stack.Screen
                name="Home"
                component={HomeScreen}
                options={{
                  title: 'Navigation',
                  headerRight: () => (
                    <Pressable nativeID="nav-header-right" onPress={() => log('header button')} style={styles.headerButton}>
                      <Text style={styles.headerButtonText}>Info</Text>
                    </Pressable>
                  ),
                }}
              />
              <Stack.Screen
                name="Details"
                component={DetailsScreen}
                options={({route}) => ({
                  title: `Details ${route.params?.n ?? 1}`,
                  headerBackTitle: 'Back',
                  headerStyle: {backgroundColor: '#3584E4'},
                  headerTintColor: '#FFFFFF',
                })}
              />
              <Stack.Screen
                name="Modal"
                component={ModalScreen}
                options={({navigation}) => ({
                  presentation: 'modal',
                  headerLeft: () => (
                    <Pressable nativeID="nav-cancel" onPress={() => navigation.goBack()} style={styles.headerButton}>
                      <Text style={styles.headerButtonText}>Cancel</Text>
                    </Pressable>
                  ),
                  headerRight: () => (
                    <Pressable nativeID="nav-done" onPress={() => log('done')} style={styles.headerButton}>
                      <Text style={styles.headerButtonText}>Done</Text>
                    </Pressable>
                  ),
                })}
              />
              <Stack.Screen
                name="Sheet"
                component={SheetScreen}
                options={{presentation: 'formSheet', sheetAllowedDetents: [0.5], headerShown: false}}
              />
              <Stack.Screen name="Tabs" component={TabsScreen} options={{title: 'Tabs', headerBackTitle: 'Back'}} />
              <Stack.Screen name="Bare" component={BareScreen} options={{headerShown: false}} />
              <Stack.Screen name="Guarded" component={GuardedScreen} options={{headerBackTitle: 'Back'}} />
            </Stack.Navigator>
          </NavigationContainer>
        </View>
      </View>
    </LogContext.Provider>
  );
}

// --initial-props '{"pushed": 2}' starts two screens deep, '{"open":
// "Modal"}' with the modal open.
export default function GalleryNavigation({pushed, open}) {
  return <NavigationDemo pushed={pushed} open={open} />;
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 6},
  status: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  frame: {flex: 1, borderRadius: 12, overflow: 'hidden', borderWidth: 1, borderColor: '#C7C7CC'},
  screen: {flex: 1, padding: 20, gap: 12},
  modal: {backgroundColor: '#FFF4E5'},
  bare: {backgroundColor: '#E8F5E9'},
  sheet: {backgroundColor: '#EDE7F6'},
  title: {fontSize: 22, fontWeight: '600', color: '#8E8E93'},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 8},
  action: {backgroundColor: '#007AFF', paddingVertical: 8, paddingHorizontal: 14, borderRadius: 8, alignSelf: 'flex-start'},
  actionText: {color: '#FFFFFF', fontSize: 14},
  headerButton: {paddingHorizontal: 12, paddingVertical: 6},
  headerButtonText: {color: '#007AFF', fontSize: 15},
});
