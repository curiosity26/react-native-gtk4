// Platform APIs: Linking, AppState, Clipboard, Vibration, Share,
// PixelRatio / fontScale and I18nManager.
// rn-gtk-host --module GalleryPlatform --self-test (with --url, and --rtl
// for right-to-left) drives them and checks what JS sees; it gives the
// app its own XDG directories with a handler for rngtk-test: URLs.
import React, {useEffect, useState} from 'react';
import {
  AppState,
  Clipboard,
  I18nManager,
  Linking,
  PixelRatio,
  Pressable,
  Share,
  StyleSheet,
  Text,
  Vibration,
  View,
  useWindowDimensions,
} from 'react-native';

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryPlatform() {
  const [initialURL, setInitialURL] = useState('?');
  const [lastURL, setLastURL] = useState('none');
  const [canOpen, setCanOpen] = useState('-');
  const [canOpenUnknown, setCanOpenUnknown] = useState('-');
  const [opened, setOpened] = useState('-');
  const [openBad, setOpenBad] = useState('-');
  const [appState, setAppState] = useState(AppState.currentState);
  const [focusEvents, setFocusEvents] = useState({focus: 0, blur: 0});
  const [clipboard, setClipboard] = useState('-');
  const [vibrated, setVibrated] = useState('no');
  const [share, setShare] = useState('-');
  const {fontScale} = useWindowDimensions();

  useEffect(() => {
    Linking.getInitialURL().then(url => setInitialURL(url ?? 'none'));
    const subs = [
      Linking.addEventListener('url', ({url}) => setLastURL(url)),
      AppState.addEventListener('change', setAppState),
      AppState.addEventListener('focus', () => setFocusEvents(e => ({...e, focus: e.focus + 1}))),
      AppState.addEventListener('blur', () => setFocusEvents(e => ({...e, blur: e.blur + 1}))),
    ];
    return () => subs.forEach(s => s.remove());
  }, []);

  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        initial: {initialURL} · last url: {lastURL}
      </Text>
      <Text style={styles.status}>
        can rngtk-test: {canOpen} · can nosuch: {canOpenUnknown} · opened: {opened} · open bad: {openBad}
      </Text>
      <Text style={styles.status}>
        app state {appState} · focus {focusEvents.focus} · blur {focusEvents.blur}
      </Text>
      <Text style={styles.status}>
        clipboard: {clipboard} · vibrated {vibrated} · share: {share}
      </Text>
      <Text style={styles.status}>
        isRTL {String(I18nManager.isRTL)} · locale {String(I18nManager.getConstants().localeIdentifier)} ·
        fontScale {fontScale.toFixed(2)} · PixelRatio {PixelRatio.get()} · getFontScale{' '}
        {PixelRatio.getFontScale().toFixed(2)}
      </Text>
      <View style={styles.row}>
        <Action
          id="can-open"
          label="canOpenURL"
          onPress={() => {
            Linking.canOpenURL('rngtk-test:probe').then(v => setCanOpen(v ? 'yes' : 'no'));
            Linking.canOpenURL('nosuchscheme-rngtk:1').then(v => setCanOpenUnknown(v ? 'yes' : 'no'));
          }}
        />
        <Action
          id="open"
          label="openURL"
          onPress={() =>
            Linking.openURL('rngtk-test:opened?x=1').then(
              () => setOpened('ok'),
              e => setOpened(`error ${e.message}`),
            )
          }
        />
        <Action
          id="open-bad"
          label="openURL (no handler)"
          onPress={() =>
            Linking.openURL('nosuchscheme-rngtk:1').then(
              () => setOpenBad('resolved'),
              () => setOpenBad('rejected'),
            )
          }
        />
        <Action
          id="clipboard"
          label="Clipboard"
          onPress={() => {
            Clipboard.setString('clipboard from JS');
            Clipboard.getString().then(setClipboard);
          }}
        />
        <Action
          id="vibrate"
          label="Vibration"
          onPress={() => {
            Vibration.vibrate();
            Vibration.vibrate([0, 100, 50, 100]);
            Vibration.cancel();
            setVibrated('yes');
          }}
        />
        <Action
          id="share"
          label="Share"
          onPress={() => Share.share({message: 'hello'}).then(r => setShare(r.action))}
        />
        <Action id="force-rtl" label="forceRTL(true)" onPress={() => I18nManager.forceRTL(true)} />
        <Action id="force-ltr" label="forceRTL(false)" onPress={() => I18nManager.forceRTL(false)} />
      </View>
      <View nativeID="row" style={styles.row}>
        <View nativeID="first" style={[styles.box, {backgroundColor: '#3584E4'}]} />
        <View nativeID="second" style={[styles.box, {backgroundColor: '#E01B24'}]} />
      </View>
      <View style={styles.column}>
        <Text nativeID="scaled" style={styles.sample}>
          Scaled
        </Text>
        <Text nativeID="unscaled" allowFontScaling={false} style={styles.sample}>
          Unscaled
        </Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 20, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 13, color: '#000000'},
  row: {flexDirection: 'row', gap: 8, alignItems: 'center', flexWrap: 'wrap'},
  column: {alignItems: 'flex-start'},
  action: {paddingVertical: 6, paddingHorizontal: 10, borderRadius: 6, backgroundColor: '#E6E6E6'},
  actionText: {color: '#000000'},
  box: {width: 60, height: 40},
  sample: {fontSize: 20, color: '#000000'},
});
