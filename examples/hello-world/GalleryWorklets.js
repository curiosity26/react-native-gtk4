// react-native-worklets on Linux (packages/worklets): worklets run on its
// UI runtime (a Hermes runtime of its own, on GTK's main thread), and back
// on the JS one; a synchronizable shared between them; requestAnimationFrame
// on the UI runtime; gesture-handler's bindings there.
// rn-gtk-host --module GalleryWorklets --self-test presses each button.
import React, {useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';
import {
  createSynchronizable,
  getRuntimeKind,
  isUIRuntime,
  runOnUISync,
  scheduleOnRN,
  scheduleOnUI,
} from 'react-native-worklets';
import {GestureDetector, GestureHandlerRootView, useTapGesture} from 'react-native-gesture-handler';

const counter = createSynchronizable(0);

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export function WorkletsDemo() {
  const [lines, setLines] = useState({});
  const show = (key, text) => setLines(l => ({...l, [key]: text}));
  return (
    <View style={styles.root}>
      <Text style={styles.line}>JS runtime kind {String(getRuntimeKind())}</Text>
      <View style={styles.row}>
        <Action
          id="wk-schedule"
          label="scheduleOnUI → scheduleOnRN"
          onPress={() =>
            scheduleOnUI(() => {
              'worklet';
              scheduleOnRN(show, 'ui', `on UI ${isUIRuntime() ? 'yes' : 'no'}, 6 * 7 = ${6 * 7}`);
            })
          }
        />
        <Action
          id="wk-sync"
          label="runOnUISync"
          onPress={() => {
            const result = runOnUISync(
              (a, b) => {
                'worklet';
                return {sum: a + b, ui: isUIRuntime()};
              },
              2,
              3,
            );
            show('sync', `sync ${result.sum} (on UI ${result.ui ? 'yes' : 'no'})`);
          }}
        />
        <Action
          id="wk-synchronizable"
          label="Synchronizable"
          onPress={() => {
            scheduleOnUI(() => {
              'worklet';
              counter.setBlocking(v => v + 42);
            });
            const poll = () => {
              const v = counter.getBlocking();
              if (v >= 42) show('synchronizable', `synchronizable ${v}`);
              else setTimeout(poll, 16);
            };
            poll();
          }}
        />
        <Action
          id="wk-frames"
          label="Frames on UI"
          onPress={() =>
            scheduleOnUI(() => {
              'worklet';
              let n = 0;
              let first = 0;
              const tick = t => {
                if (n === 0) first = t;
                n += 1;
                if (n < 10) requestAnimationFrame(tick);
                else scheduleOnRN(show, 'frames', `UI frames ${n} over ${Math.round(t - first) > 0 ? 'time' : 'no time'}`);
              };
              requestAnimationFrame(tick);
            })
          }
        />
        <Action
          id="wk-gh"
          label="Gesture handler on UI"
          onPress={() =>
            scheduleOnUI(() => {
              'worklet';
              scheduleOnRN(show, 'gh', `_setGestureStateSync ${typeof globalThis._setGestureStateSync}`);
            })
          }
        />
      </View>
      {Object.entries(lines).map(([k, v]) => (
        <Text key={k} style={styles.line}>
          {v}
        </Text>
      ))}
    </View>
  );
}

// A gesture loads gesture-handler's gesture code, which installs its
// bindings on the UI runtime when worklets is there.
function TapBox() {
  const [taps, setTaps] = useState(0);
  const tap = useTapGesture({onActivate: () => setTaps(t => t + 1)});
  return (
    <GestureDetector gesture={tap}>
      <View nativeID="wk-tap" style={styles.box}>
        <Text style={styles.actionText}>Tapped {taps}</Text>
      </View>
    </GestureDetector>
  );
}

export default function GalleryWorklets() {
  return (
    <GestureHandlerRootView style={{flex: 1}}>
      <WorkletsDemo />
      <TapBox />
    </GestureHandlerRootView>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 8},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 8},
  action: {backgroundColor: '#007AFF', paddingVertical: 8, paddingHorizontal: 14, borderRadius: 8},
  actionText: {color: '#FFFFFF'},
  line: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  box: {margin: 16, width: 120, height: 60, borderRadius: 8, backgroundColor: '#26A269', alignItems: 'center', justifyContent: 'center'},
});
