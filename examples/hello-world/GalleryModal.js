// <Modal>: full screen (slide), a form sheet (fade), a transparent one, and
// a modal opened from a modal. Every event shows in the status line.
// rn-gtk-host --module GalleryModal --self-test opens and closes them (a
// button inside, Escape, the window's close button) and checks the windows.
import React, {useState} from 'react';
import {Modal, Pressable, StyleSheet, Text, View, useWindowDimensions} from 'react-native';

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryModal() {
  const [open, setOpen] = useState(null); // 'full' | 'sheet' | 'clear'
  const [nested, setNested] = useState(false);
  const [counts, setCounts] = useState({show: 0, dismiss: 0, request: 0});
  const [last, setLast] = useState('-');
  const {width} = useWindowDimensions();
  const count = (what, name) => {
    setCounts(c => ({...c, [what]: c[what] + 1}));
    setLast(`${what} ${name}`);
  };
  const props = name => ({
    onShow: () => count('show', name),
    onDismiss: () => count('dismiss', name),
    onRequestClose: () => {
      count('request', name);
      if (name === 'nested') setNested(false);
      else setOpen(null);
    },
  });

  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        shown {counts.show} · dismissed {counts.dismiss} · requests {counts.request} · last {last}
      </Text>
      <Text style={styles.status}>window width {Math.round(width)}</Text>
      <View style={styles.row}>
        <Action id="open-full" label="Full screen (slide)" onPress={() => setOpen('full')} />
        <Action id="open-sheet" label="Form sheet (fade)" onPress={() => setOpen('sheet')} />
        <Action id="open-clear" label="Transparent (fade)" onPress={() => setOpen('clear')} />
        <Action
          id="open-both"
          label="Two at once"
          onPress={() => {
            setOpen('full');
            setNested(true);
          }}
        />
      </View>

      <Modal
        visible={open === 'full'}
        animationType="slide"
        accessibilityLabel="Full screen modal"
        {...props('full')}>
        <View nativeID="full-body" style={styles.body}>
          <Text style={styles.title}>Full screen</Text>
          <Text style={styles.status}>Escape or the button closes it.</Text>
          <View style={styles.row}>
            <Action id="open-nested" label="Open another" onPress={() => setNested(true)} />
            <Action
              id="close-full"
              label="Close"
              onPress={() => {
                setOpen(null);
                setNested(false);
              }}
            />
          </View>
          <Modal
            visible={nested}
            presentationStyle="formSheet"
            accessibilityLabel="Nested modal"
            {...props('nested')}>
            <View nativeID="nested-body" style={styles.body}>
              <Text style={styles.title}>A modal over a modal</Text>
              <Action id="close-nested" label="Close" onPress={() => setNested(false)} />
            </View>
          </Modal>
        </View>
      </Modal>

      <Modal
        visible={open === 'sheet'}
        animationType="fade"
        presentationStyle="formSheet"
        accessibilityLabel="Form sheet"
        {...props('sheet')}>
        <View nativeID="sheet-body" style={styles.body}>
          <Text style={styles.title}>Form sheet</Text>
          <Action id="close-sheet" label="Done" onPress={() => setOpen(null)} />
        </View>
      </Modal>

      <Modal
        visible={open === 'clear'}
        animationType="fade"
        transparent
        accessibilityLabel="Transparent modal"
        {...props('clear')}>
        <View nativeID="clear-backdrop" style={styles.backdrop}>
          <View style={styles.card}>
            <Text style={styles.title}>Over the app</Text>
            <Action id="close-clear" label="OK" onPress={() => setOpen(null)} />
          </View>
        </View>
      </Modal>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 8, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#1C1C1E'},
  row: {flexDirection: 'row', gap: 8},
  action: {paddingVertical: 8, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  actionText: {color: '#FFFFFF', fontSize: 14},
  body: {flex: 1, padding: 24, gap: 12},
  title: {fontSize: 20, fontWeight: 'bold', color: '#1C1C1E'},
  backdrop: {flex: 1, backgroundColor: 'rgba(0,0,0,0.4)', alignItems: 'center', justifyContent: 'center'},
  card: {width: 280, padding: 24, gap: 12, borderRadius: 12, backgroundColor: '#FFFFFF'},
});
