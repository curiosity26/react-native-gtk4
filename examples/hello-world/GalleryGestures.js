// react-native-gesture-handler on Linux (packages/gesture-handler): the
// hook API's gestures (tap, double tap, long press, pan, fling, hover) on
// GestureDetectors, and the builder API (Gesture.Tap(), Gesture.Pan()).
// The pan box holds a Pressable: a drag cancels its press (the JS
// responder loses the touch when the pan activates), a click presses it.
// rn-gtk-host --module GalleryGestures --self-test drives each with the
// mouse and checks the callbacks.
import React, {useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';
import {
  Directions,
  Gesture,
  GestureDetector,
  GestureHandlerRootView,
  useFlingGesture,
  useHoverGesture,
  useLongPressGesture,
  usePanGesture,
  useTapGesture,
} from 'react-native-gesture-handler';

function Box({id, label, color = '#3584E4', style, children}) {
  return (
    <View nativeID={id} style={[styles.box, {backgroundColor: color}, style]}>
      <Text style={styles.boxText}>{label}</Text>
      {children}
    </View>
  );
}

function useCounter() {
  const [n, setN] = useState(0);
  return [n, () => setN(c => c + 1)];
}

export function GesturesDemo({onLog}) {
  const log = msg => onLog?.(`gestures: ${msg}`);
  const [taps, tap] = useCounter();
  const [doubles, double] = useCounter();
  const [longs, long] = useCounter();
  const [flings, fling] = useCounter();
  const [presses, press] = useCounter();
  const [legacyTaps, legacyTap] = useCounter();
  const [pan, setPan] = useState({x: 0, y: 0, state: 'idle'});
  const [legacyPan, setLegacyPan] = useState({x: 0, y: 0});
  const [hover, setHover] = useState('out');

  const tapGesture = useTapGesture({
    onActivate: () => {
      tap();
      log('tap');
    },
  });
  const doubleTapGesture = useTapGesture({
    numberOfTaps: 2,
    onActivate: () => {
      double();
      log('double tap');
    },
  });
  const longPressGesture = useLongPressGesture({
    minDuration: 300,
    onActivate: () => {
      long();
      log('long press');
    },
  });
  const panGesture = usePanGesture({
    onActivate: () => setPan(p => ({...p, state: 'active'})),
    onUpdate: e => setPan({x: Math.round(e.translationX), y: Math.round(e.translationY), state: 'active'}),
    onDeactivate: () => setPan(p => ({...p, state: 'ended'})),
  });
  const flingGesture = useFlingGesture({
    direction: Directions.RIGHT,
    onActivate: () => {
      fling();
      log('fling right');
    },
  });
  const hoverGesture = useHoverGesture({
    onActivate: () => setHover('in'),
    onDeactivate: () => setHover('out'),
  });
  const legacyTapGesture = Gesture.Tap()
    .runOnJS(true)
    .onEnd((_e, success) => {
      if (success) legacyTap();
    });
  const legacyPanGesture = Gesture.Pan()
    .runOnJS(true)
    .onUpdate(e => setLegacyPan({x: Math.round(e.translationX), y: Math.round(e.translationY)}));

  return (
    <View style={styles.root}>
      <Text nativeID="gh-status" style={styles.status}>
        taps {taps} · double taps {doubles} · long presses {longs} · flings {flings} · hover {hover}
      </Text>
      <Text style={styles.status}>
        pan {pan.x},{pan.y} {pan.state} · presses {presses} · legacy taps {legacyTaps} · legacy pan {legacyPan.x},
        {legacyPan.y}
      </Text>
      <View style={styles.row}>
        <GestureDetector gesture={tapGesture}>
          <Box id="gh-tap" label="Tap" />
        </GestureDetector>
        <GestureDetector gesture={doubleTapGesture}>
          <Box id="gh-double" label="Double tap" color="#26A269" />
        </GestureDetector>
        <GestureDetector gesture={longPressGesture}>
          <Box id="gh-long" label="Long press" color="#E66100" />
        </GestureDetector>
        <GestureDetector gesture={flingGesture}>
          <Box id="gh-fling" label="Fling →" color="#9141AC" />
        </GestureDetector>
        <GestureDetector gesture={hoverGesture}>
          <Box id="gh-hover" label={`Hover (${hover})`} color={hover === 'in' ? '#C01C28' : '#77767B'} />
        </GestureDetector>
      </View>
      <View style={styles.row}>
        <GestureDetector gesture={panGesture}>
          <View nativeID="gh-pan-area" style={styles.panArea}>
            <Box id="gh-pan" label="Drag me" style={{transform: [{translateX: pan.x}, {translateY: pan.y}]}}>
              <Pressable
                nativeID="gh-press"
                onPress={() => {
                  press();
                  log('pressable pressed');
                }}
                style={styles.press}>
                <Text style={styles.pressText}>Press</Text>
              </Pressable>
            </Box>
          </View>
        </GestureDetector>
        <View style={{gap: 12}}>
          <GestureDetector gesture={legacyTapGesture}>
            <Box id="gh-legacy-tap" label="Gesture.Tap()" color="#1C71D8" />
          </GestureDetector>
          <GestureDetector gesture={legacyPanGesture}>
            <Box id="gh-legacy-pan" label="Gesture.Pan()" color="#2EC27E" />
          </GestureDetector>
        </View>
      </View>
    </View>
  );
}

export default function GalleryGestures() {
  return (
    <GestureHandlerRootView style={{flex: 1}}>
      <GesturesDemo />
    </GestureHandlerRootView>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 12},
  status: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 12},
  box: {width: 120, height: 80, borderRadius: 10, alignItems: 'center', justifyContent: 'center', gap: 6},
  boxText: {color: '#FFFFFF', fontWeight: '600'},
  panArea: {width: 360, height: 220, borderRadius: 12, borderWidth: 1, borderColor: '#C7C7CC', padding: 20},
  press: {backgroundColor: 'rgba(255,255,255,0.3)', paddingHorizontal: 10, paddingVertical: 4, borderRadius: 6},
  pressText: {color: '#FFFFFF'},
});
