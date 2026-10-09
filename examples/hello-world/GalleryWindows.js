// More windows (Windows from @curiosity26/react-native-gtk4): the main
// window opens GalleryWindowChild windows, which share this JS runtime (the
// counter) and see their own size with useWindowDimensions. Window events
// show in the status lines.
// rn-gtk-host --module GalleryWindows --self-test opens, resizes and closes
// them, and ends by closing the main window and then the last one.
import React, {useEffect, useState, useSyncExternalStore} from 'react';
import {Modal, Pressable, StyleSheet, Text, View, useWindowDimensions} from 'react-native';
import {Windows, useWindow} from '@curiosity26/react-native-gtk4';

// A store both windows read: one runtime.
let count = 0;
const subscribers = new Set();
const counter = {
  get: () => count,
  subscribe: fn => {
    subscribers.add(fn);
    return () => subscribers.delete(fn);
  },
  increment: () => {
    count++;
    subscribers.forEach(fn => fn());
  },
};

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export function GalleryWindowChild({label}) {
  const window = useWindow();
  const {width, height} = useWindowDimensions();
  const shared = useSyncExternalStore(counter.subscribe, counter.get);
  const [modal, setModal] = useState(false);
  return (
    <View nativeID={`child-${label}`} style={styles.root}>
      <Text style={styles.status}>
        {label} · window {window.id} · {Math.round(width)} x {Math.round(height)} · count {shared}
      </Text>
      <View style={styles.row}>
        <Action id={`close-${label}`} label="Close this window" onPress={() => window.close()} />
        <Action id={`title-${label}`} label="Rename" onPress={() => window.setTitle(`${label} (renamed)`)} />
        <Action id={`modal-${label}`} label="Modal here" onPress={() => setModal(true)} />
      </View>
      <Modal visible={modal} presentationStyle="formSheet" onRequestClose={() => setModal(false)}>
        <View style={styles.root}>
          <Text style={styles.status}>A modal over {label}</Text>
          <Action id={`modal-close-${label}`} label="Close" onPress={() => setModal(false)} />
        </View>
      </Modal>
    </View>
  );
}

export default function GalleryWindows() {
  const {width, height} = useWindowDimensions();
  const shared = useSyncExternalStore(counter.subscribe, counter.get);
  const [events, setEvents] = useState([]);
  const [open, setOpen] = useState([]);
  const log = line => setEvents(e => [...e, line].slice(-6));

  useEffect(() => {
    const main = Windows.main;
    const subs = ['close-requested', 'closed', 'focus', 'blur'].map(type =>
      main.addListener(type, () => log(`main ${type}`)),
    );
    return () => subs.forEach(s => s.remove());
  }, []);

  const openChild = (label, options = {}) => {
    const w = Windows.open({
      component: 'GalleryWindowChild',
      initialProps: {label},
      title: `Child ${label}`,
      width: 420,
      height: 300,
      minWidth: 200,
      minHeight: 150,
      ...options,
    });
    for (const type of ['close-requested', 'closed', 'resize']) {
      w.addListener(type, e => log(`${label} ${type}${type === 'resize' ? ` ${e.width}x${e.height}` : ''}`));
    }
    w.addListener('closed', () => setOpen(o => o.filter(x => x !== label)));
    setOpen(o => [...o, label]);
  };

  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        main window {Windows.main.id} · {Math.round(width)} x {Math.round(height)} · count {shared} · open [
        {open.join(',')}]
      </Text>
      <Text nativeID="events" style={styles.status}>
        events: {events.join(' | ')}
      </Text>
      <View style={styles.row}>
        <Action id="open-a" label="Open window A" onPress={() => openChild('A')} />
        <Action id="open-b" label="Open B (asks before closing)" onPress={() => openChild('B', {interceptClose: true})} />
        <Action id="increment" label="count + 1" onPress={counter.increment} />
        <Action id="main-title" label="Rename this window" onPress={() => Windows.main.setTitle('Windows gallery')} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#1C1C1E'},
  row: {flexDirection: 'row', gap: 8, flexWrap: 'wrap'},
  action: {paddingVertical: 8, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  actionText: {color: '#FFFFFF', fontSize: 14},
});
