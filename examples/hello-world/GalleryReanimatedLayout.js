// react-native-reanimated on Linux, part 2: layout animations (entering,
// exiting, layout transitions), CSS animations and transitions, and
// gesture-handler's Reanimated components (ReanimatedSwipeable,
// ReanimatedDrawerLayout).
// rn-gtk-host --module GalleryReanimatedLayout --self-test adds and removes
// items, runs the CSS animations, swipes a row open and opens the drawer.
import React, {useRef, useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';
import Animated, {
  FadeIn,
  FadeOut,
  LinearTransition,
  SlideInLeft,
  ZoomOut,
  css,
} from 'react-native-reanimated';
import {GestureHandlerRootView} from 'react-native-gesture-handler';
import ReanimatedSwipeable from 'react-native-gesture-handler/ReanimatedSwipeable';
import ReanimatedDrawerLayout, {DrawerType} from 'react-native-gesture-handler/ReanimatedDrawerLayout';

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

const COLORS = ['#3584E4', '#26A269', '#E66100', '#9141AC', '#C01C28', '#1C71D8'];

// Items fade and slide in, zoom out, and the others move to make room.
export function ReanimatedLayoutDemo() {
  const next = useRef(3);
  const [items, setItems] = useState([1, 2]);
  return (
    <View style={styles.section}>
      <View style={styles.row}>
        <Action id="rl-add" label="Add (FadeIn)" onPress={() => setItems(list => [next.current++, ...list])} />
        <Action id="rl-add-slide" label="Add (SlideInLeft)" onPress={() => setItems(list => [...list, -next.current++])} />
        <Action id="rl-remove" label="Remove first (FadeOut)" onPress={() => setItems(list => list.slice(1))} />
        <Action id="rl-remove-last" label="Remove last (ZoomOut)" onPress={() => setItems(list => list.slice(0, -1))} />
      </View>
      <Text nativeID="rl-count" style={styles.line}>
        items {items.length}
      </Text>
      <View style={styles.list}>
        {items.map((n, i) => (
          <Animated.View
            key={n}
            entering={n < 0 ? SlideInLeft.duration(400) : FadeIn.duration(400)}
            exiting={i === 0 ? FadeOut.duration(400) : ZoomOut.duration(400)}
            layout={LinearTransition.duration(400)}
            style={[styles.item, {backgroundColor: COLORS[Math.abs(n) % COLORS.length]}]}>
            {/* Reanimated finds the entering animation by the view's
                nativeID: the test's goes on a child. */}
            <View nativeID={`rl-item-${Math.abs(n)}`} style={styles.itemLabel}>
              <Text style={styles.itemText}>Item {Math.abs(n)}</Text>
            </View>
          </Animated.View>
        ))}
      </View>
    </View>
  );
}

const pulse = css.keyframes({
  from: {width: 60},
  to: {width: 200},
});

// A CSS animation (keyframes, two iterations, alternating) and CSS
// transitions (width and opacity follow the style).
export function ReanimatedCSSDemo() {
  const [run, setRun] = useState(0);
  const [wide, setWide] = useState(false);
  const [ended, setEnded] = useState('');
  const [transitions, setTransitions] = useState(0);
  return (
    <View style={styles.section}>
      <View style={styles.row}>
        <Action id="rl-css-run" label="CSS animation" onPress={() => setRun(r => r + 1)} />
        <Action id="rl-css-toggle" label="CSS transition" onPress={() => setWide(w => !w)} />
      </View>
      <View style={styles.track}>
        {run > 0 && (
          <Animated.View
            key={run}
            nativeID="rl-css-anim"
            onCSSAnimationEnd={() => setEnded(`css animation ${run} ended`)}
            style={[
              styles.cssBox,
              {
                animationName: pulse,
                animationDuration: '400ms',
                animationIterationCount: 2,
                animationDirection: 'alternate',
                animationTimingFunction: 'ease-in-out',
              },
            ]}
          />
        )}
      </View>
      <View style={styles.track}>
        <Animated.View
          nativeID="rl-css-transition"
          onCSSTransitionEnd={() => setTransitions(n => n + 1)}
          style={[
            styles.cssBox,
            {
              backgroundColor: '#26A269',
              width: wide ? 240 : 80,
              opacity: wide ? 0.5 : 1,
              transitionProperty: ['width', 'opacity'],
              transitionDuration: 400,
              transitionTimingFunction: 'ease',
            },
          ]}
        />
      </View>
      <Text style={styles.line}>
        transition {wide ? 'wide' : 'narrow'}, {transitions} ended · {ended}
      </Text>
    </View>
  );
}

// gesture-handler's Reanimated components.
export function ReanimatedSwipeDrawerDemo() {
  const [status, setStatus] = useState({swipe: 'closed', drawer: 'closed', deleted: 0});
  const drawer = useRef(null);
  const swipeable = useRef(null);
  return (
    <View style={[styles.section, {height: 220}]}>
      <ReanimatedDrawerLayout
        ref={drawer}
        drawerWidth={220}
        drawerType={DrawerType.FRONT}
        onDrawerOpen={() => setStatus(s => ({...s, drawer: 'open'}))}
        onDrawerClose={() => setStatus(s => ({...s, drawer: 'closed'}))}
        renderNavigationView={() => (
          <View nativeID="rl-drawer" style={styles.drawer}>
            <Text style={styles.itemText}>Drawer</Text>
            <Action id="rl-drawer-close" label="Close" onPress={() => drawer.current?.closeDrawer()} />
          </View>
        )}>
        <View style={styles.drawerContent}>
          <View style={styles.row}>
            <Action id="rl-drawer-open" label="Open drawer" onPress={() => drawer.current?.openDrawer()} />
            <Text nativeID="rl-swipe-status" style={styles.line}>
              swipeable {status.swipe} · drawer {status.drawer} · deleted {status.deleted}
            </Text>
          </View>
          <ReanimatedSwipeable
            ref={swipeable}
            friction={1}
            rightThreshold={40}
            onSwipeableOpen={direction => setStatus(s => ({...s, swipe: `open ${direction}`}))}
            onSwipeableClose={() => setStatus(s => ({...s, swipe: 'closed'}))}
            renderRightActions={() => (
              <Pressable
                nativeID="rl-swipe-delete"
                onPress={() => {
                  setStatus(s => ({...s, deleted: s.deleted + 1}));
                  swipeable.current?.close();
                }}
                style={styles.deleteAction}>
                <Text style={styles.itemText}>Delete</Text>
              </Pressable>
            )}>
            <View nativeID="rl-swipe-row" style={styles.swipeRow}>
              <Text>Swipe this row left</Text>
            </View>
          </ReanimatedSwipeable>
        </View>
      </ReanimatedDrawerLayout>
    </View>
  );
}

export default function GalleryReanimatedLayout() {
  return (
    <GestureHandlerRootView style={{flex: 1}}>
      <View style={styles.root}>
        <ReanimatedLayoutDemo />
        <ReanimatedCSSDemo />
        <ReanimatedSwipeDrawerDemo />
      </View>
    </GestureHandlerRootView>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 12},
  section: {gap: 8},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 8, alignItems: 'center'},
  action: {backgroundColor: '#007AFF', paddingVertical: 8, paddingHorizontal: 14, borderRadius: 8},
  actionText: {color: '#FFFFFF'},
  line: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  list: {flexDirection: 'row', gap: 8, height: 56, alignItems: 'center'},
  item: {width: 90, height: 48, borderRadius: 8},
  itemLabel: {flex: 1, alignItems: 'center', justifyContent: 'center'},
  itemText: {color: '#FFFFFF', fontWeight: '600'},
  track: {height: 36, justifyContent: 'center'},
  cssBox: {height: 28, width: 60, borderRadius: 6, backgroundColor: '#3584E4'},
  drawer: {flex: 1, backgroundColor: '#3D3846', padding: 16, gap: 12},
  drawerContent: {flex: 1, gap: 12, borderWidth: 1, borderColor: '#C7C7CC', borderRadius: 8, padding: 12, overflow: 'hidden'},
  swipeRow: {height: 56, backgroundColor: '#FFFFFF', justifyContent: 'center', paddingHorizontal: 16, borderWidth: 1, borderColor: '#E0E0E0'},
  deleteAction: {width: 100, backgroundColor: '#C01C28', alignItems: 'center', justifyContent: 'center'},
});
