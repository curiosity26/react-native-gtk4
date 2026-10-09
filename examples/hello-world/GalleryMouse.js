// Desktop mouse props: onMouseEnter / onMouseLeave, tooltip, onAuxClick.
// rn-gtk-host --module GalleryMouse --self-test moves and clicks the mouse
// through the input path and checks what reaches JS.
import React, {useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';

export default function GalleryMouse() {
  const [outer, setOuter] = useState('out');
  const [inner, setInner] = useState('out');
  const [enters, setEnters] = useState(0);
  const [aux, setAux] = useState('-');
  const [parentAux, setParentAux] = useState(0);
  const [presses, setPresses] = useState(0);
  const [bare, setBare] = useState('out');
  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        outer {outer} · inner {inner} · enters {enters}
      </Text>
      <Text style={styles.status}>
        aux {aux} · parent aux {parentAux} · presses {presses}
      </Text>
      <View
        nativeID="outer"
        style={styles.outer}
        onMouseEnter={e => {
          setOuter(`in at ${Math.round(e.nativeEvent.offsetX)}`);
          setEnters(n => n + 1);
        }}
        onMouseLeave={() => setOuter('out')}>
        <View
          nativeID="inner"
          style={styles.inner}
          onMouseEnter={() => setInner('in')}
          onMouseLeave={() => setInner('out')}
        />
      </View>
      <View nativeID="tip" tooltip="Hello from a tooltip" style={styles.box}>
        <Text nativeID="tip-text">hover for a tooltip</Text>
      </View>
      {/* A plain parent: flattened, it still hears the bubbling auxclick. */}
      <View onAuxClick={() => setParentAux(n => n + 1)}>
        <Pressable
          nativeID="aux"
          style={styles.box}
          onPress={() => setPresses(n => n + 1)}
          onAuxClick={e => setAux(`button ${e.nativeEvent.button}`)}>
          <Text>middle or right click</Text>
        </Pressable>
        <View onAuxClick={() => setParentAux(n => n + 100)}>
          <Text nativeID="aux-text">only a flattened View listens here</Text>
        </View>
      </View>
      {/* Only onMouseEnter / tooltip, no style: still a real view. */}
      <View onMouseEnter={() => setBare('in')} onMouseLeave={() => setBare('out')}>
        <Text nativeID="bare">bare onMouseEnter: {bare}</Text>
      </View>
      <View tooltip="A bare tooltip">
        <Text nativeID="bare-tip">bare tooltip</Text>
      </View>
      <View nativeID="away" style={styles.box} />
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 20, gap: 12, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#000000'},
  outer: {width: 300, height: 120, backgroundColor: '#DDEEFF', padding: 30},
  inner: {width: 100, height: 60, backgroundColor: '#3584E4'},
  box: {
    width: 200,
    height: 50,
    backgroundColor: '#EEEEEE',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
