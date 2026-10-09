// Selectable text: drag, double- and triple-click, copying, and presses.
// rn-gtk-host --module GallerySelection --self-test drags and clicks
// through the input path and checks the selection, the clipboard and that
// presses still work where they should.
import React, {useState} from 'react';
import {Pressable, ScrollView, StyleSheet, Text, View} from 'react-native';

export default function GallerySelection() {
  const [plainPresses, setPlainPresses] = useState(0);
  const [wrappedPresses, setWrappedPresses] = useState(0);
  return (
    <View style={styles.root}>
      <Text nativeID="para" selectable style={styles.para}>
        {'Alpha beta gamma.\nSecond paragraph here.'}
      </Text>
      <Text nativeID="red" selectable selectionColor="#FF0000" style={styles.para}>
        Highlighted in red
      </Text>
      <Text nativeID="plain" style={styles.para} onPress={() => setPlainPresses(n => n + 1)}>
        Not selectable, pressable
      </Text>
      <Text style={styles.status}>plain presses {plainPresses}</Text>
      <Pressable
        nativeID="wrapper"
        style={styles.wrapper}
        onPress={() => setWrappedPresses(n => n + 1)}>
        <Text nativeID="wrapped" selectable style={styles.para}>
          Selectable inside a Pressable
        </Text>
      </Pressable>
      <Text style={styles.status}>wrapped presses {wrappedPresses}</Text>
      <ScrollView nativeID="scroller" style={styles.scroller}>
        {Array.from({length: 20}, (_, i) => (
          <Text key={i} selectable style={styles.para}>
            Scrollable selectable line {i + 1}
          </Text>
        ))}
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 20, gap: 10, backgroundColor: '#FFFFFF'},
  para: {fontFamily: 'monospace', fontSize: 18, color: '#000000', alignSelf: 'flex-start'},
  status: {fontSize: 14, color: '#333333'},
  wrapper: {padding: 10, backgroundColor: '#EEEEEE', alignSelf: 'flex-start'},
  scroller: {height: 120, borderWidth: 1, borderColor: '#CCCCCC'},
});
