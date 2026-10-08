// ScrollView, FlatList and SectionList on one 940x680 screen.
// rn-gtk-host --module GalleryLists --self-test scrolls them through the
// same input path as the mouse wheel and checks the results.
import React, {useCallback, useRef, useState} from 'react';
import {
  FlatList,
  Pressable,
  ScrollView,
  SectionList,
  StyleSheet,
  Text,
  View,
} from 'react-native';

const ROWS = Array.from({length: 30}, (_, i) => i);
const BOXES = Array.from({length: 20}, (_, i) => i);
const ITEMS = Array.from({length: 10000}, (_, i) => ({key: String(i)}));
const SECTIONS = ['A', 'B', 'C', 'D', 'E'].map(title => ({
  title,
  data: Array.from({length: 10}, (_, i) => `${title}${i}`),
}));
const ROW_HEIGHT = 30;
const COLORS = ['#FF3B30', '#FF9500', '#FFCC00', '#34C759', '#007AFF', '#AF52DE'];

function Panel({title, children}) {
  return (
    <View style={styles.panel}>
      <Text style={styles.title}>{title}</Text>
      {children}
    </View>
  );
}

export default function GalleryLists() {
  const scrollRef = useRef(null);
  const listRef = useRef(null);
  const [offset, setOffset] = useState(0);
  const [presses, setPresses] = useState(0);
  const [endReached, setEndReached] = useState(0);
  const [listOffset, setListOffset] = useState(0);

  // Stable callbacks: FlatList is a PureComponent, so the status line's
  // re-render on each scroll doesn't re-render the list.
  const getItemLayout = useCallback(
    (_, index) => ({length: ROW_HEIGHT, offset: ROW_HEIGHT * index, index}),
    [],
  );
  const onEndReached = useCallback(() => setEndReached(n => n + 1), []);
  const onListScroll = useCallback(
    e => setListOffset(Math.round(e.nativeEvent.contentOffset.y)),
    [],
  );
  const renderItem = useCallback(
    ({item}) => (
      <View style={styles.item}>
        <Text style={styles.itemText}>item {item.key}</Text>
      </View>
    ),
    [],
  );

  return (
    <View style={styles.root}>
      <View style={styles.column}>
        <Panel title="ScrollView (vertical)">
          <ScrollView
            nativeID="vscroll"
            ref={scrollRef}
            style={styles.vscroll}
            scrollEventThrottle={16}
            onScroll={e => setOffset(Math.round(e.nativeEvent.contentOffset.y))}>
            <Pressable
              nativeID="scroll-press"
              onPress={() => setPresses(n => n + 1)}
              style={({pressed}) => [
                styles.row,
                {backgroundColor: pressed ? '#0040A0' : '#007AFF'},
              ]}>
              <Text style={styles.rowTextLight}>scroll press {presses}</Text>
            </Pressable>
            {ROWS.map(i => (
              <View key={i} style={[styles.row, i % 2 ? styles.odd : null]}>
                <Text style={styles.rowText}>row {i}</Text>
              </View>
            ))}
          </ScrollView>
          <Text nativeID="scroll-offset" style={styles.status}>
            offset {offset}
          </Text>
          <Pressable
            nativeID="scroll-to-200"
            onPress={() => scrollRef.current?.scrollTo({y: 200, animated: false})}
            style={styles.smallButton}>
            <Text style={styles.rowTextLight}>scrollTo 200</Text>
          </Pressable>
        </Panel>
        <Panel title="ScrollView (horizontal)">
          <ScrollView
            nativeID="hscroll"
            horizontal
            style={styles.hscroll}
            showsHorizontalScrollIndicator={false}>
            {BOXES.map(i => (
              <View
                key={i}
                style={[styles.box, {backgroundColor: COLORS[i % COLORS.length]}]}
              />
            ))}
          </ScrollView>
        </Panel>
      </View>

      <View style={styles.column}>
        <Panel title="FlatList, 10,000 rows">
          <FlatList
            nativeID="flatlist"
            ref={listRef}
            style={styles.list}
            data={ITEMS}
            renderItem={renderItem}
            getItemLayout={getItemLayout}
            onEndReached={onEndReached}
            onScroll={onListScroll}
            scrollEventThrottle={16}
          />
          <Text nativeID="list-status" style={styles.status}>
            list offset {listOffset} · end reached {endReached}
          </Text>
          <View style={styles.buttons}>
            <Pressable
              nativeID="list-index"
              onPress={() =>
                listRef.current?.scrollToIndex({index: 5000, animated: false})
              }
              style={styles.smallButton}>
              <Text style={styles.rowTextLight}>to 5000</Text>
            </Pressable>
            <Pressable
              nativeID="list-end"
              onPress={() => listRef.current?.scrollToEnd({animated: false})}
              style={styles.smallButton}>
              <Text style={styles.rowTextLight}>to end</Text>
            </Pressable>
          </View>
        </Panel>
      </View>

      <View style={styles.column}>
        <Panel title="SectionList, sticky headers">
          <SectionList
            nativeID="sections"
            style={styles.list}
            sections={SECTIONS}
            stickySectionHeadersEnabled
            keyExtractor={item => item}
            renderSectionHeader={({section}) => (
              <View style={styles.header}>
                <Text style={styles.headerText}>section {section.title}</Text>
              </View>
            )}
            renderItem={({item}) => (
              <View style={styles.item}>
                <Text style={styles.itemText}>{item}</Text>
              </View>
            )}
          />
        </Panel>
        <Panel title="inverted FlatList">
          <FlatList
            inverted
            style={styles.short}
            data={ITEMS.slice(0, 50)}
            renderItem={renderItem}
          />
        </Panel>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, flexDirection: 'row', backgroundColor: '#F5F5F7', padding: 8},
  column: {width: 306, marginRight: 4},
  panel: {marginBottom: 8},
  title: {fontSize: 12, color: '#6E6E73', marginBottom: 4},
  vscroll: {height: 300, backgroundColor: '#FFFFFF', borderRadius: 8},
  hscroll: {height: 90, backgroundColor: '#FFFFFF'},
  list: {height: 450, backgroundColor: '#FFFFFF'},
  short: {height: 130, backgroundColor: '#FFFFFF'},
  row: {height: 40, justifyContent: 'center', paddingHorizontal: 12},
  odd: {backgroundColor: '#F0F0F5'},
  rowText: {fontSize: 14, color: '#1C1C1E'},
  rowTextLight: {fontSize: 13, color: '#FFFFFF', fontWeight: '600'},
  status: {fontSize: 12, color: '#1C1C1E', marginTop: 4},
  box: {width: 70, height: 70, margin: 10, borderRadius: 10},
  item: {
    height: ROW_HEIGHT,
    justifyContent: 'center',
    paddingHorizontal: 10,
    borderBottomWidth: 1,
    borderBottomColor: '#E5E5EA',
  },
  itemText: {fontSize: 13, color: '#1C1C1E'},
  header: {height: 28, justifyContent: 'center', paddingHorizontal: 10, backgroundColor: '#5856D6'},
  headerText: {fontSize: 13, color: '#FFFFFF', fontWeight: 'bold'},
  buttons: {flexDirection: 'row', gap: 8, marginTop: 4},
  smallButton: {
    marginTop: 4,
    paddingVertical: 6,
    paddingHorizontal: 10,
    borderRadius: 6,
    backgroundColor: '#007AFF',
    alignSelf: 'flex-start',
  },
});
