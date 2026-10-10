// react-native-reanimated on Linux (packages/reanimated, on
// packages/worklets' UI runtime): shared values and animated styles driven
// by withTiming, withSpring and withDecay on GTK's frame clock; an
// animated scroll handler; measure and scrollTo from the UI runtime; a pan
// (gesture-handler) moving a box with worklets only, no JS; an animation
// that keeps going while the JS thread is busy; and many boxes animating
// at once, with the frame rate the UI runtime sees (useFrameCallback).
// rn-gtk-host --module GalleryReanimated --self-test presses each button,
// drags the box and times frames.
import React, {useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';
import Animated, {
  Easing,
  interpolate,
  measure,
  runOnJS,
  runOnUI,
  scrollTo,
  useAnimatedReaction,
  useAnimatedRef,
  useAnimatedScrollHandler,
  useAnimatedStyle,
  useFrameCallback,
  useSharedValue,
  withDecay,
  withRepeat,
  withSpring,
  withTiming,
} from 'react-native-reanimated';
import {GestureDetector, GestureHandlerRootView, usePanGesture} from 'react-native-gesture-handler';

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

function useLines() {
  const [lines, setLines] = useState({});
  return [lines, (key, text) => setLines(l => ({...l, [key]: text}))];
}

function Lines({lines}) {
  return Object.entries(lines).map(([k, v]) => (
    <Text key={k} style={styles.line}>
      {v}
    </Text>
  ));
}

// withTiming, withSpring, withDecay; and a timing that runs on while JS is
// blocked.
export function ReanimatedAnimations() {
  const [lines, show] = useLines();
  const x = useSharedValue(0);
  const scale = useSharedValue(1);
  const decay = useSharedValue(0);
  const blocked = useSharedValue(0);
  const timingStyle = useAnimatedStyle(() => ({transform: [{translateX: x.value}]}));
  const springStyle = useAnimatedStyle(() => ({transform: [{scale: scale.value}]}));
  const decayStyle = useAnimatedStyle(() => ({transform: [{translateX: decay.value}]}));
  const blockedStyle = useAnimatedStyle(() => ({transform: [{translateX: blocked.value}]}));
  return (
    <View style={styles.section}>
      <View style={styles.row}>
        <Action
          id="re-timing-go"
          label="withTiming"
          onPress={() => {
            x.value = withTiming(x.value > 0 ? 0 : 200, {duration: 400, easing: Easing.inOut(Easing.quad)}, done => {
              runOnJS(show)('timing', `timing done ${done}`);
            });
          }}
        />
        <Action
          id="re-spring-go"
          label="withSpring"
          onPress={() => {
            scale.value = withSpring(scale.value > 1 ? 1 : 1.5, {damping: 12}, done => {
              runOnJS(show)('spring', `spring done ${done}`);
            });
          }}
        />
        <Action
          id="re-decay-go"
          label="withDecay"
          onPress={() => {
            decay.value = 0;
            decay.value = withDecay({velocity: 1500, clamp: [0, 200]}, done => {
              runOnJS(show)('decay', `decay done at ${Math.round(decay.value)}`);
            });
          }}
        />
        <Action
          id="re-block-go"
          label="Animate, then block JS"
          onPress={() => {
            blocked.value = 0;
            blocked.value = withTiming(200, {duration: 300, easing: Easing.linear});
            // After the write reaches the UI runtime (worklets sends it at
            // the end of this task).
            setTimeout(() => {
              const until = Date.now() + 900;
              while (Date.now() < until) {}
              show('blocked', 'JS unblocked');
            }, 0);
          }}
        />
      </View>
      <View style={styles.track}>
        <Animated.View nativeID="re-timing" style={[styles.box, timingStyle]}>
          <Text style={styles.boxText}>timing</Text>
        </Animated.View>
      </View>
      <View style={styles.row}>
        <Animated.View nativeID="re-spring" style={[styles.box, {backgroundColor: '#26A269'}, springStyle]}>
          <Text style={styles.boxText}>spring</Text>
        </Animated.View>
      </View>
      <View style={styles.track}>
        <Animated.View nativeID="re-decay" style={[styles.box, {backgroundColor: '#E66100'}, decayStyle]}>
          <Text style={styles.boxText}>decay</Text>
        </Animated.View>
      </View>
      <View style={styles.track}>
        <Animated.View nativeID="re-blocked" style={[styles.box, {backgroundColor: '#9141AC'}, blockedStyle]}>
          <Text style={styles.boxText}>JS busy</Text>
        </Animated.View>
      </View>
      <Lines lines={lines} />
    </View>
  );
}

// useAnimatedScrollHandler, measure and scrollTo (from the UI runtime),
// and a pan that moves a box with worklets alone.
export function ReanimatedInteraction() {
  const [lines, show] = useLines();
  const scrollRef = useAnimatedRef();
  const boxRef = useAnimatedRef();
  const scrollY = useSharedValue(0);
  const dragX = useSharedValue(0);
  const onScroll = useAnimatedScrollHandler({
    onScroll: e => {
      scrollY.value = e.contentOffset.y;
    },
  });
  useAnimatedReaction(
    () => Math.floor(scrollY.value / 100),
    (step, previous) => {
      if (step !== previous && step > 0) runOnJS(show)('scroll', `scrolled past ${step * 100}`);
    },
  );
  const barStyle = useAnimatedStyle(() => ({
    width: interpolate(scrollY.value, [0, 400], [0, 160], 'clamp'),
  }));
  const drag = usePanGesture({
    onUpdate: e => {
      'worklet';
      dragX.value = e.translationX;
    },
    onDeactivate: () => {
      'worklet';
      const at = Math.round(dragX.value);
      dragX.value = withSpring(0, {damping: 20}, done => {
        if (done) runOnJS(show)('drag', `dragged to ${at}, sprang back`);
      });
    },
  });
  const dragStyle = useAnimatedStyle(() => ({transform: [{translateX: dragX.value}]}));
  return (
    <View style={styles.section}>
      <View style={styles.row}>
        <Action
          id="re-measure-go"
          label="measure + scrollTo"
          onPress={() =>
            runOnUI(() => {
              'worklet';
              const m = measure(boxRef);
              scrollTo(scrollRef, 0, 300, false);
              runOnJS(show)('measure', m ? `measured ${Math.round(m.width)}x${Math.round(m.height)}` : 'measured null');
            })()
          }
        />
      </View>
      <View style={styles.row}>
        <Animated.ScrollView
          ref={scrollRef}
          nativeID="re-scroll"
          style={styles.scroll}
          onScroll={onScroll}
          scrollEventThrottle={16}>
          {Array.from({length: 40}, (_, i) => (
            <Text key={i} style={styles.item}>
              Row {i + 1}
            </Text>
          ))}
        </Animated.ScrollView>
        <View style={{gap: 8}}>
          <Animated.View nativeID="re-scroll-bar" style={[styles.bar, barStyle]} />
          <Animated.View ref={boxRef} nativeID="re-measured" style={[styles.box, {backgroundColor: '#1C71D8'}]}>
            <Text style={styles.boxText}>measured</Text>
          </Animated.View>
        </View>
        <View style={styles.dragArea}>
          <GestureDetector gesture={drag}>
            <Animated.View nativeID="re-drag" style={[styles.box, {backgroundColor: '#C01C28'}, dragStyle]}>
              <Text style={styles.boxText}>Drag me</Text>
            </Animated.View>
          </GestureDetector>
        </View>
      </View>
      <Lines lines={lines} />
    </View>
  );
}

// Many boxes animating at once, and the frame rate the UI runtime sees.
const BENCH_BOXES = 60;
function BenchBox({progress, index}) {
  const style = useAnimatedStyle(() => ({
    transform: [
      {translateX: interpolate(progress.value, [0, 1], [0, 40 + (index % 5) * 20])},
      {rotate: `${progress.value * (index % 2 ? 180 : -180)}deg`},
    ],
    opacity: interpolate(progress.value, [0, 1], [1, 0.4]),
  }));
  return <Animated.View style={[styles.benchBox, style]} />;
}

export function ReanimatedBench() {
  const [lines, show] = useLines();
  const progress = useSharedValue(0);
  const stats = useSharedValue({frames: 0, start: 0, last: 0, worst: 0});
  const frames = useFrameCallback(info => {
    'worklet';
    const s = stats.value;
    if (s.frames === 0) s.start = info.timestamp;
    else s.worst = Math.max(s.worst, info.timestamp - s.last);
    s.last = info.timestamp;
    s.frames += 1;
    if (s.frames === 241) {
      const avg = (s.last - s.start) / 240;
      runOnJS(show)('bench', `${BENCH_BOXES} boxes: 240 frames, ${(1000 / avg).toFixed(0)} fps, worst ${s.worst.toFixed(1)} ms`);
    }
    stats.value = s;
  }, false);
  return (
    <View style={styles.section}>
      <View style={styles.row}>
        <Action
          id="re-bench-go"
          label={`Animate ${BENCH_BOXES} boxes`}
          onPress={() => {
            stats.value = {frames: 0, start: 0, last: 0, worst: 0};
            progress.value = 0;
            progress.value = withRepeat(withTiming(1, {duration: 800}), -1, true);
            frames.setActive(true);
          }}
        />
        <Action
          id="re-bench-stop"
          label="Stop"
          onPress={() => {
            progress.value = 0;
            frames.setActive(false);
            show('bench-stop', 'bench stopped');
          }}
        />
      </View>
      <View nativeID="re-bench" style={styles.bench}>
        {Array.from({length: BENCH_BOXES}, (_, i) => (
          <BenchBox key={i} index={i} progress={progress} />
        ))}
      </View>
      <Lines lines={lines} />
    </View>
  );
}

export default function GalleryReanimated() {
  return (
    <GestureHandlerRootView style={{flex: 1}}>
      <Animated.ScrollView contentContainerStyle={styles.root}>
        <ReanimatedAnimations />
        <ReanimatedInteraction />
        <ReanimatedBench />
      </Animated.ScrollView>
    </GestureHandlerRootView>
  );
}

const styles = StyleSheet.create({
  root: {padding: 16, gap: 16},
  section: {gap: 8},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 8, alignItems: 'flex-start'},
  action: {backgroundColor: '#007AFF', paddingVertical: 8, paddingHorizontal: 14, borderRadius: 8},
  actionText: {color: '#FFFFFF'},
  line: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  track: {height: 50, width: 320},
  box: {width: 100, height: 44, borderRadius: 8, backgroundColor: '#3584E4', alignItems: 'center', justifyContent: 'center'},
  boxText: {color: '#FFFFFF', fontWeight: '600'},
  scroll: {width: 160, height: 120, borderWidth: 1, borderColor: '#C7C7CC', borderRadius: 8},
  item: {paddingVertical: 6, paddingHorizontal: 10, color: '#6E6E73'},
  bar: {height: 8, borderRadius: 4, backgroundColor: '#26A269'},
  dragArea: {width: 300, height: 120, borderRadius: 12, borderWidth: 1, borderColor: '#C7C7CC', padding: 20},
  bench: {flexDirection: 'row', flexWrap: 'wrap', gap: 6, width: 600},
  benchBox: {width: 24, height: 24, borderRadius: 4, backgroundColor: '#3584E4'},
});
