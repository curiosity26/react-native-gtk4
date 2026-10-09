// Component gallery: View styling, Text and pressables on one 940x680
// screen (scrolling comes later). rn-gtk-host --module Gallery --self-test
// checks pixels of the views with a nativeID.
import React, {useState} from 'react';
import {
  Button,
  Platform,
  Pressable,
  StyleSheet,
  Text,
  TouchableHighlight,
  TouchableOpacity,
  View,
} from 'react-native';

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

function Pressables() {
  const [presses, setPresses] = useState(0);
  const [hovered, setHovered] = useState(false);
  const [opacityPresses, setOpacityPresses] = useState(0);
  const [highlightPresses, setHighlightPresses] = useState(0);
  const [buttonPresses, setButtonPresses] = useState(0);
  return (
    <View style={styles.row}>
      <Tile label="Pressable + hover">
        <Pressable
          nativeID="pressable"
          onPress={() => setPresses(n => n + 1)}
          onHoverIn={() => setHovered(true)}
          onHoverOut={() => setHovered(false)}
          style={({pressed}) => [
            styles.button,
            {
              backgroundColor: pressed
                ? '#0040A0'
                : hovered
                  ? '#3395FF'
                  : '#007AFF',
              cursor: 'pointer',
            },
          ]}>
          <Text nativeID="pressable-text" style={styles.buttonText}>
            pressed {presses}
          </Text>
        </Pressable>
      </Tile>
      <Tile label="TouchableOpacity">
        <TouchableOpacity
          nativeID="opacity"
          onPress={() => setOpacityPresses(n => n + 1)}
          style={[styles.button, {backgroundColor: '#34C759'}]}>
          <Text style={styles.buttonText}>opacity {opacityPresses}</Text>
        </TouchableOpacity>
      </Tile>
      <Tile label="TouchableHighlight">
        <TouchableHighlight
          nativeID="highlight"
          underlayColor="#AF52DE"
          onPress={() => setHighlightPresses(n => n + 1)}
          style={[styles.button, {backgroundColor: '#FF9500'}]}>
          <Text style={styles.buttonText}>highlight {highlightPresses}</Text>
        </TouchableHighlight>
      </Tile>
      <Tile label="Button: default, color, disabled">
        <View style={styles.buttons}>
          <Button
            nativeID="button"
            title={`button ${buttonPresses}`}
            onPress={() => setButtonPresses(n => n + 1)}
          />
          <View style={styles.pair6}>
            <Button nativeID="button-color" title="color" color="#3584E4" />
            <Button
              nativeID="button-disabled"
              title="disabled"
              disabled
              onPress={() => setButtonPresses(n => n + 100)}
            />
          </View>
        </View>
      </Tile>
      <Tile label="pointerEvents none">
        <View style={[styles.box, {backgroundColor: '#DDD'}]}>
          <View pointerEvents="none" style={StyleSheet.absoluteFill}>
            <Text style={styles.small}>
              Clicks go through to the press counter's sibling.
            </Text>
          </View>
        </View>
      </Tile>
    </View>
  );
}

export default function Gallery() {
  return (
    <View style={styles.root}>
      <View style={styles.row}>
        <Tile label="per-side borders">
          <View nativeID="borders" style={[styles.box, styles.borders]} />
        </Tile>
        <Tile label="per-corner radii">
          <View nativeID="radii" style={[styles.box, styles.radii]} />
        </Tile>
        <Tile label="elliptical 50%">
          <View nativeID="ellipse" style={styles.ellipse} />
        </Tile>
        <Tile label="dashed / dotted">
          <View style={styles.pair}>
            <View nativeID="dashed" style={[styles.half, styles.dashed]} />
            <View nativeID="dotted" style={[styles.half, styles.dotted]} />
          </View>
        </Tile>
        <Tile label="outline">
          <View nativeID="outline" style={[styles.box, styles.outline]} />
        </Tile>
      </View>

      <View style={styles.row}>
        <Tile label="overflow hidden, rounded">
          <View nativeID="clip" style={[styles.box, styles.clip]}>
            <View style={styles.clipChild} />
          </View>
        </Tile>
        <Tile label="boxShadow">
          <View nativeID="shadow" style={[styles.box, styles.shadow]} />
        </Tile>
        <Tile label="inset shadow">
          <View nativeID="inset" style={[styles.box, styles.inset]} />
        </Tile>
        <Tile label="legacy shadow*">
          <View nativeID="legacy-shadow" style={[styles.box, styles.legacy]} />
        </Tile>
        <Tile label="opacity 0.5">
          <View nativeID="opacity-box" style={[styles.box, styles.half50]} />
        </Tile>
      </View>

      <View style={styles.row}>
        <Tile label="translate 40,10">
          <View nativeID="translated" style={[styles.small40, styles.translated]} />
        </Tile>
        <Tile label="rotate 45° scale 1.2">
          <View nativeID="rotated" style={[styles.small40, styles.rotated]} />
        </Tile>
        <Tile label="rotateY 60° (3D)">
          <View style={[styles.box, styles.rotated3d]}>
            <Text style={styles.small}>perspective</Text>
          </View>
        </Tile>
        <Tile label="filter grayscale / blur">
          <View style={styles.pair}>
            <View nativeID="grayscale" style={[styles.half, styles.grayscale]} />
            <View style={[styles.half, styles.blur]} />
          </View>
        </Tile>
        <Tile label="gradients">
          <View style={styles.pair}>
            <View nativeID="linear" style={[styles.half, styles.linear]} />
            <View nativeID="radial" style={[styles.half, styles.radial]} />
          </View>
        </Tile>
      </View>

      <View style={styles.row}>
        <Tile label="nested Text spans">
          <Text nativeID="nested" style={styles.nested}>
            Black <Text style={{color: '#FF0000'}}>red</Text>{' '}
            <Text style={{color: '#0000FF', fontWeight: 'bold'}}>blue</Text>{' '}
            <Text style={{backgroundColor: '#FFEB3B'}}>marked</Text>
          </Text>
        </Tile>
        <Tile label="decoration / spacing">
          <Text style={styles.decorated}>
            <Text style={{textDecorationLine: 'underline'}}>under</Text>{' '}
            <Text
              style={{
                textDecorationLine: 'line-through',
                textDecorationColor: '#FF3B30',
              }}>
              struck
            </Text>{' '}
            <Text style={{letterSpacing: 4}}>spaced</Text>{' '}
            <Text style={{fontStyle: 'italic', textTransform: 'uppercase'}}>
              italic
            </Text>
          </Text>
        </Tile>
        <Tile label="numberOfLines 1">
          <Text nativeID="ellipsis" numberOfLines={1} style={styles.ellipsis}>
            This line is much too long to fit in its box
          </Text>
        </Tile>
        <Tile label="justify, lineHeight 22, selectable">
          <Text nativeID="selectable" selectable style={styles.justify}>
            Justified text spreads words to fill each line except the last.
          </Text>
        </Tile>
        <Tile label="fonts">
          <Text style={styles.fonts}>
            <Text style={{fontFamily: 'serif'}}>Serif </Text>
            <Text style={{fontFamily: 'monospace'}}>Mono </Text>
            <Text style={{fontWeight: '300'}}>Light </Text>
            <Text>Ünïcødé ✓ 漢字</Text>
          </Text>
        </Tile>
      </View>

      <Pressables />
      <Text style={styles.footer}>
        Running on {Platform.OS} ({Platform.constants.windowSystem})
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#F5F5F7', padding: 6},
  row: {flexDirection: 'row', height: 130},
  tile: {width: 185, height: 130, padding: 6},
  stage: {flex: 1, alignItems: 'center', justifyContent: 'center'},
  label: {fontSize: 11, color: '#6E6E73', textAlign: 'center', marginTop: 4},
  box: {width: 110, height: 80, backgroundColor: '#FFFFFF'},
  pair: {flexDirection: 'row', gap: 12},
  pair6: {flexDirection: 'row', gap: 6},
  buttons: {alignItems: 'center', gap: 6},
  half: {width: 64, height: 64},
  small: {fontSize: 11, color: '#333', padding: 4},
  small40: {width: 40, height: 40, backgroundColor: '#FF2D55'},
  borders: {
    borderTopWidth: 4,
    borderRightWidth: 8,
    borderBottomWidth: 12,
    borderLeftWidth: 16,
    borderTopColor: '#FF0000',
    borderRightColor: '#00C000',
    borderBottomColor: '#0000FF',
    borderLeftColor: '#FF9500',
  },
  radii: {
    backgroundColor: '#5856D6',
    borderTopLeftRadius: 0,
    borderTopRightRadius: 30,
    borderBottomRightRadius: 45,
    borderBottomLeftRadius: 10,
  },
  ellipse: {width: 150, height: 76, borderRadius: '50%', backgroundColor: '#FF9500'},
  dashed: {borderWidth: 3, borderStyle: 'dashed', borderColor: '#007AFF', borderRadius: 10},
  dotted: {borderWidth: 4, borderStyle: 'dotted', borderColor: '#FF2D55'},
  outline: {
    borderRadius: 12,
    outlineWidth: 3,
    outlineOffset: 4,
    outlineColor: '#AF52DE',
  },
  clip: {borderRadius: 40, overflow: 'hidden', backgroundColor: '#000'},
  clipChild: {width: 200, height: 200, backgroundColor: '#FF3B30'},
  shadow: {borderRadius: 8, boxShadow: '0px 8px 16px 4px rgba(0, 0, 0, 0.5)'},
  inset: {boxShadow: 'inset 0px 0px 16px 4px #007AFF'},
  legacy: {
    shadowColor: '#000',
    shadowOffset: {width: 6, height: 6},
    shadowOpacity: 0.6,
    shadowRadius: 6,
  },
  half50: {backgroundColor: '#000', opacity: 0.5},
  translated: {transform: [{translateX: 40}, {translateY: 10}]},
  rotated: {transform: [{rotate: '45deg'}, {scale: 1.2}]},
  rotated3d: {
    backgroundColor: '#5AC8FA',
    transform: [{perspective: 300}, {rotateY: '60deg'}],
  },
  grayscale: {backgroundColor: '#FF0000', filter: 'grayscale(1)'},
  blur: {backgroundColor: '#34C759', borderRadius: 32, filter: 'blur(4px)'},
  linear: {experimental_backgroundImage: 'linear-gradient(90deg, #FF0000, #0000FF)'},
  radial: {
    experimental_backgroundImage: 'radial-gradient(circle, #FFFFFF, #000000)',
    borderRadius: 32,
  },
  nested: {fontSize: 16, color: '#000000', width: 165},
  decorated: {fontSize: 14, width: 165},
  ellipsis: {fontSize: 14, width: 150},
  justify: {fontSize: 13, lineHeight: 22, width: 165, textAlign: 'justify'},
  fonts: {fontSize: 14, width: 165},
  button: {
    paddingVertical: 10,
    paddingHorizontal: 14,
    borderRadius: 8,
    minWidth: 120,
    alignItems: 'center',
  },
  buttonText: {color: '#FFFFFF', fontSize: 15, fontWeight: '600'},
  footer: {fontSize: 12, color: '#6E6E73', marginTop: 4},
});
