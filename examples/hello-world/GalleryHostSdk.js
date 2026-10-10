// The host's API for libraries that drive Fabric themselves (what
// Reanimated needs, rngtk/Extensions.h), through rn-gtk-host's
// RNGtkHostSdkProbe module: threads, frames, the runtime, a commit hook
// and an event listener, props without a commit, Hermes.
// rn-gtk-host --module GalleryHostSdk --self-test presses each button.
import React, {useRef, useState} from 'react';
import {Pressable, StyleSheet, Text, TurboModuleRegistry, View, findNodeHandle} from 'react-native';

const Probe = TurboModuleRegistry.get('RNGtkHostSdkProbe');

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryHostSdk() {
  const [lines, setLines] = useState({});
  const [bumped, setBumped] = useState(0);
  const target = useRef(null);
  const show = (key, text) => setLines(l => ({...l, [key]: text}));
  if (!Probe) return <Text style={styles.line}>RNGtkHostSdkProbe is rn-gtk-host's.</Text>;
  return (
    <View style={styles.root}>
      <View style={styles.row}>
        <Action id="sdk-threads" label="Threads" onPress={() => Probe.threads().then(t => show('threads', t))} />
        <Action id="sdk-frames" label="30 frames" onPress={() => Probe.frames(30).then(t => show('frames', t))} />
        <Action
          id="sdk-global"
          label="Runtime global"
          onPress={() => {
            Probe.setGlobal(7);
            const poll = () => (global.__rngtkSdkProbe === 7 ? show('global', 'global 7') : setTimeout(poll, 16));
            poll();
          }}
        />
        <Action id="sdk-hook" label="Hook Fabric" onPress={() => show('hook', `hooked ${Probe.hookFabric() ? 'yes' : 'no'}`)} />
        <Action
          id="sdk-bump"
          label="Commit"
          onPress={() => {
            setBumped(b => b + 1);
            setTimeout(() => show('counts', Probe.counts()), 50);
          }}
        />
        <Action id="sdk-opacity" label="Opacity by tag" onPress={() => Probe.setOpacity(findNodeHandle(target.current), 0.25)} />
        <Action id="sdk-hermes" label="Hermes" onPress={() => show('hermes', `hermes ${Probe.hermes()}`)} />
      </View>
      {Object.entries(lines).map(([k, v]) => (
        <Text key={k} style={styles.line}>
          {v}
        </Text>
      ))}
      <Text style={styles.line}>bumped {bumped}</Text>
      <View ref={target} nativeID="sdk-target" collapsable={false} style={styles.target} />
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 8},
  row: {flexDirection: 'row', flexWrap: 'wrap', gap: 8},
  action: {backgroundColor: '#007AFF', paddingVertical: 8, paddingHorizontal: 14, borderRadius: 8},
  actionText: {color: '#FFFFFF'},
  line: {fontFamily: 'monospace', fontSize: 13, color: '#6E6E73'},
  target: {width: 120, height: 60, backgroundColor: '#E01B24', borderRadius: 8},
});
