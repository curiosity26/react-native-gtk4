// Drag and drop (react-native-macos' props): drop zones for files, text and
// images, and things to drag out: selected text and a draggable Image.
// rn-gtk-host --module GalleryDragDrop --self-test drags over and drops
// on the zones (as GTK's drop target does) and checks what a drag out of
// the text and the image carries.
import React, {useState} from 'react';
import {Image, StyleSheet, Text, View} from 'react-native';

function describe(e) {
  const t = e.nativeEvent.dataTransfer;
  const files = t.files.map(f => `${f.name}:${f.type}:${f.size}`).join(',');
  return `files [${files}] urls [${(t.urls ?? []).join(',')}] text ${JSON.stringify(t.text ?? null)} types [${t.types.join(',')}]`;
}

function Zone({id, types, label}) {
  const [over, setOver] = useState(false);
  const [last, setLast] = useState('-');
  const [log, setLog] = useState([]);
  const note = what => setLog(l => [...l, what].slice(-4));
  return (
    <View
      nativeID={id}
      draggedTypes={types}
      onDragEnter={e => {
        setOver(true);
        note(`enter ${e.nativeEvent.dataTransfer.types.join('+')}`);
      }}
      onDragLeave={() => {
        setOver(false);
        note('leave');
      }}
      onDrop={e => {
        setOver(false);
        note('drop');
        setLast(describe(e));
      }}
      style={[styles.zone, over && styles.zoneOver]}>
      <Text style={styles.label}>
        {label} ({types.join(', ')})
      </Text>
      <Text style={styles.small}>{log.join(' · ')}</Text>
      <Text style={styles.small}>{last}</Text>
    </View>
  );
}

export default function GalleryDragDrop({image}) {
  return (
    <View style={styles.root}>
      <View style={styles.row}>
        <Zone id="zone-files" types={['fileUrl']} label="Files and links" />
        <Zone id="zone-text" types={['string']} label="Text" />
        <Zone id="zone-any" types={['fileUrl', 'string', 'image']} label="Anything" />
      </View>
      <View nativeID="no-zone" style={styles.zone}>
        <Text style={styles.label}>Not a drop zone</Text>
      </View>
      <Text nativeID="drag-text" selectable style={styles.body}>
        Select some of this text, then drag the selection out of the window.
      </Text>
      <View style={styles.row}>
        <Image
          nativeID="drag-image"
          draggable
          source={image ? {uri: image} : require('./assets/tile.png')}
          style={styles.image}
        />
        <Image nativeID="still-image" source={require('./assets/tile.png')} style={styles.image} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 12, backgroundColor: '#FFFFFF'},
  row: {flexDirection: 'row', gap: 12},
  zone: {flex: 1, minHeight: 110, padding: 10, borderRadius: 10, borderWidth: 2, borderStyle: 'dashed', borderColor: '#B0B0B8', gap: 4},
  zoneOver: {borderColor: '#3584E4', backgroundColor: '#E8F0FC'},
  label: {fontSize: 14, fontWeight: '600', color: '#1C1C1E'},
  small: {fontSize: 12, color: '#3A3A3C'},
  body: {fontSize: 15, color: '#1C1C1E'},
  image: {width: 96, height: 96, borderRadius: 8},
});
