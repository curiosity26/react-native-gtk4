// react-native-gesture-handler on Linux (packages/gesture-handler): the
// hook API's gestures (tap, double tap, long press, pan, fling, hover) on
// GestureDetectors, and the builder API (Gesture.Tap(), Gesture.Pan());
// relations (pinch and rotation together, a pan that waits for a double
// tap to fail), a manual gesture, the buttons (RectButton, Touchable) and
// the library's ScrollView (in GesturesRelations, the second row).
// The callbacks set React state, so they run on JS (runOnJS: with
// react-native-reanimated installed, gesture callbacks are worklets on the
// UI runtime otherwise; GalleryReanimated has those).
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
  GestureStateManager,
  RectButton,
  ScrollView as GHScrollView,
  Touchable,
  useFlingGesture,
  useHoverGesture,
  useLongPressGesture,
  useManualGesture,
  usePanGesture,
  usePinchGesture,
  useRotationGesture,
  useSimultaneousGestures,
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
    runOnJS: true,
    onActivate: () => {
      tap();
      log('tap');
    },
  });
  const doubleTapGesture = useTapGesture({
    runOnJS: true,
    numberOfTaps: 2,
    onActivate: () => {
      double();
      log('double tap');
    },
  });
  const longPressGesture = useLongPressGesture({
    runOnJS: true,
    minDuration: 300,
    onActivate: () => {
      long();
      log('long press');
    },
  });
  const panGesture = usePanGesture({
    runOnJS: true,
    onActivate: () => setPan(p => ({...p, state: 'active'})),
    onUpdate: e => setPan({x: Math.round(e.translationX), y: Math.round(e.translationY), state: 'active'}),
    onDeactivate: () => setPan(p => ({...p, state: 'ended'})),
  });
  const flingGesture = useFlingGesture({
    runOnJS: true,
    direction: Directions.RIGHT,
    onActivate: () => {
      fling();
      log('fling right');
    },
  });
  const hoverGesture = useHoverGesture({
    runOnJS: true,
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

// Relations, buttons, the native gesture of a ScrollView, a manual gesture.
export function GesturesRelations({onLog}) {
  const log = msg => onLog?.(`gestures: ${msg}`);
  const [transform, setTransform] = useState({scale: 1, rotation: 0, pinching: false, rotating: false});
  const [box, setBox] = useState({x: 0, y: 0, doubles: 0});
  const [manual, setManual] = useState('idle');
  const [rect, rectPress] = useCounter();
  const [touchables, touchablePress] = useCounter();
  const [scrolled, setScrolled] = useState('no');

  const pinch = usePinchGesture({
    runOnJS: true,
    onActivate: () => setTransform(t => ({...t, pinching: true})),
    onUpdate: e => setTransform(t => ({...t, scale: e.scale})),
    onDeactivate: () => setTransform(t => ({...t, pinching: false})),
  });
  const rotation = useRotationGesture({
    runOnJS: true,
    onActivate: () => setTransform(t => ({...t, rotating: true})),
    onUpdate: e => setTransform(t => ({...t, rotation: e.rotation})),
    onDeactivate: () => setTransform(t => ({...t, rotating: false})),
  });
  const pinchRotate = useSimultaneousGestures(pinch, rotation);

  const doubleTap = useTapGesture({
    runOnJS: true,
    numberOfTaps: 2,
    maxDistance: 10,
    onActivate: () => setBox(b => ({...b, doubles: b.doubles + 1})),
  });
  const waitingPan = usePanGesture({
    runOnJS: true,
    requireToFail: doubleTap,
    onUpdate: e => setBox(b => ({...b, x: Math.round(e.translationX), y: Math.round(e.translationY)})),
  });
  const tapOrPan = useSimultaneousGestures(doubleTap, waitingPan);

  const manualGesture = useManualGesture({
    runOnJS: true,
    onTouchesDown: e => {
      setManual('down');
      GestureStateManager.activate(e.handlerTag);
    },
    onActivate: () => setManual('active'),
    onTouchesUp: e => GestureStateManager.deactivate(e.handlerTag),
    onDeactivate: () => setManual('ended'),
  });
  const pct = n => n.toFixed(2);
  return (
    <View style={styles.root}>
      <Text nativeID="gh-relations" style={styles.status}>
        scale {pct(transform.scale)} rotation {pct(transform.rotation)}
        {transform.pinching ? ' pinching' : ''}
        {transform.rotating ? ' rotating' : ''} · double taps {box.doubles} · waiting pan {box.x},{box.y} · manual{' '}
        {manual}
      </Text>
      <Text style={styles.status}>
        RectButton presses {rect} · Touchable presses {touchables} · scrolled {scrolled}
      </Text>
      <View style={styles.row}>
        <GestureDetector gesture={pinchRotate}>
          <View nativeID="gh-pinch" style={styles.pinchArea}>
            <View
              style={[
                styles.box,
                {
                  backgroundColor: '#E01B24',
                  transform: [{scale: transform.scale}, {rotate: `${transform.rotation}rad`}],
                },
              ]}>
              <Text style={styles.boxText}>Pinch & rotate</Text>
            </View>
          </View>
        </GestureDetector>
        <GestureDetector gesture={tapOrPan}>
          <Box
            id="gh-wait"
            label="Pan (after double tap fails)"
            color={box.doubles % 2 ? '#26A269' : '#1C71D8'}
            style={{transform: [{translateX: box.x}, {translateY: box.y}]}}
          />
        </GestureDetector>
        <GestureDetector gesture={manualGesture}>
          <Box id="gh-manual" label={`Manual (${manual})`} color="#986A44" />
        </GestureDetector>
      </View>
      <View style={styles.row}>
        <RectButton
          nativeID="gh-rect"
          style={[styles.box, {backgroundColor: '#3D3846'}]}
          onPress={() => {
            rectPress();
            log('RectButton');
          }}>
          <Text style={styles.boxText}>RectButton</Text>
        </RectButton>
        <Touchable
          nativeID="gh-touchable"
          style={[styles.box, {backgroundColor: '#813D9C'}]}
          activeOpacity={0.5}
          onPress={() => {
            touchablePress();
            log('Touchable');
          }}>
          <Text style={styles.boxText}>Touchable</Text>
        </Touchable>
        <GHScrollView
          nativeID="gh-scroll"
          style={styles.scroll}
          onScroll={() => setScrolled(v => (v === 'no' ? 'yes' : v))}
          onActivate={() => setScrolled('by its native gesture')}
          scrollEventThrottle={16}>
          {Array.from({length: 20}, (_, i) => (
            <Text key={i} style={styles.item}>
              Row {i + 1}
            </Text>
          ))}
        </GHScrollView>
      </View>
    </View>
  );
}

export default function GalleryGestures() {
  return (
    <GestureHandlerRootView style={{flex: 1}}>
      <GesturesDemo />
      <GesturesRelations />
    </GestureHandlerRootView>
  );
}

const styles = StyleSheet.create({
  root: {padding: 16, gap: 12},
  status: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 12},
  box: {width: 120, height: 80, borderRadius: 10, alignItems: 'center', justifyContent: 'center', gap: 6},
  boxText: {color: '#FFFFFF', fontWeight: '600'},
  panArea: {width: 360, height: 160, borderRadius: 12, borderWidth: 1, borderColor: '#C7C7CC', padding: 20},
  press: {backgroundColor: 'rgba(255,255,255,0.3)', paddingHorizontal: 10, paddingVertical: 4, borderRadius: 6},
  pressText: {color: '#FFFFFF'},
  pinchArea: {width: 200, height: 160, alignItems: 'center', justifyContent: 'center', borderRadius: 12, borderWidth: 1, borderColor: '#C7C7CC'},
  scroll: {width: 160, height: 80, borderWidth: 1, borderColor: '#C7C7CC', borderRadius: 8},
  item: {paddingVertical: 6, paddingHorizontal: 10, color: '#6E6E73'},
});
