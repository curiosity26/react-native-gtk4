// Image: bundled assets, http, data URIs, every resizeMode, tint, rounded
// corners, load events and errors. rn-gtk-host --module GalleryImages
// --self-test serves the http images itself and passes their base URL as
// the `imageServer` initial prop.
import React, {useState} from 'react';
import {Image, ImageBackground, StyleSheet, Text, View} from 'react-native';

const halves = require('./assets/halves.png'); // 100x50: 25px red, then blue
const tile = require('./assets/tile.png'); // 30x30: red top-left quarter

// 2x2 green (#00C800) PNG.
const GREEN_DATA_URI =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAABHNCSVQICAgIfAhkiAAAABRJREFUCJljZDjB8J+BgYGBiQEKABqoAcsCwRiPAAAAAElFTkSuQmCC';

function Tile({label, children}) {
  return (
    <View style={styles.tile}>
      <View style={styles.stage}>{children}</View>
      <Text style={styles.label} numberOfLines={1}>
        {label}
      </Text>
    </View>
  );
}

export default function GalleryImages({imageServer}) {
  const [loaded, setLoaded] = useState('');
  const [httpLoaded, setHttpLoaded] = useState('');
  const [error, setError] = useState('');
  const [events, setEvents] = useState([]);
  const server = imageServer ?? 'http://127.0.0.1:1';
  const modes = ['stretch', 'contain', 'cover', 'center', 'repeat'];
  return (
    <View style={styles.root}>
      <View style={styles.row}>
        {modes.map(mode => (
          <Tile key={mode} label={`resizeMode ${mode}`}>
            <Image
              nativeID={`mode-${mode}`}
              source={mode === 'repeat' ? tile : halves}
              resizeMode={mode}
              style={styles.frame}
            />
          </Tile>
        ))}
      </View>
      <View style={styles.row}>
        <Tile label="tintColor">
          <Image nativeID="tint" source={halves} tintColor="#34C759" style={styles.frame} />
        </Tile>
        <Tile label="borderRadius 40">
          <Image
            nativeID="rounded"
            source={halves}
            resizeMode="cover"
            style={[styles.frame, {borderRadius: 40}]}
          />
        </Tile>
        <Tile label="data: URI">
          <Image nativeID="data" source={{uri: GREEN_DATA_URI}} style={styles.frame} />
        </Tile>
        <Tile label="http + onLoad">
          <Image
            nativeID="http"
            source={{uri: `${server}/green.png`}}
            style={styles.frame}
            onLoadStart={() => setEvents(e => [...e, 'start'])}
            onLoad={e => {
              const {width, height} = e.nativeEvent.source;
              setHttpLoaded(`${width}x${height}`);
              setEvents(ev => [...ev, 'load']);
            }}
            onLoadEnd={() => setEvents(e => [...e, 'end'])}
          />
        </Tile>
        <Tile label="404 + onError">
          <Image
            nativeID="missing"
            source={{uri: `${server}/missing.png`}}
            defaultSource={tile}
            style={styles.frame}
            onError={e => setError(e.nativeEvent.error)}
          />
        </Tile>
      </View>
      <View style={styles.row}>
        <Tile label="ImageBackground">
          <ImageBackground source={halves} style={styles.frame} resizeMode="stretch">
            <Text style={styles.overlay}>on top</Text>
          </ImageBackground>
        </Tile>
        <Tile label="onLoad (asset)">
          <Image
            source={halves}
            style={styles.frame}
            onLoad={e => {
              const {width, height} = e.nativeEvent.source;
              setLoaded(`${width}x${height}`);
            }}
          />
        </Tile>
      </View>
      <Text nativeID="image-status" style={styles.status}>
        asset loaded {loaded || '-'} · http loaded {httpLoaded || '-'} · events{' '}
        {events.join(',') || '-'} · error {error || '-'}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#F5F5F7', padding: 8},
  row: {flexDirection: 'row', height: 150},
  tile: {width: 185, height: 150, padding: 6},
  stage: {flex: 1, alignItems: 'center', justifyContent: 'center'},
  label: {fontSize: 11, color: '#6E6E73', textAlign: 'center', marginTop: 4},
  frame: {width: 160, height: 100, backgroundColor: '#FFFFFF'},
  overlay: {color: '#FFFFFF', fontWeight: 'bold', margin: 8},
  status: {fontSize: 12, color: '#1C1C1E', marginTop: 8},
});
