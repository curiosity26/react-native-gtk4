// Context menus (the contextMenu prop, ContextMenu) and the app's menu bar
// (MenuBar). The last choice shows in the status line.
// rn-gtk-host --module GalleryMenus --self-test right-clicks, presses the
// Menu key and Shift+F10, picks items and checks the menus GTK shows.
import React, {useEffect, useState} from 'react';
import {Pressable, StyleSheet, Text, TextInput, View} from 'react-native';
import {ContextMenu, MenuBar} from '@curiosity26/react-native-gtk4';

export default function GalleryMenus() {
  const [last, setLast] = useState('-');
  const [count, setCount] = useState(0);
  const [hidden, setHidden] = useState(false);
  const [sort, setSort] = useState('name');
  const [sidebar, setSidebar] = useState(true);
  const chose = what => {
    setLast(what);
    setCount(c => c + 1);
  };

  useEffect(() => {
    MenuBar.setMenu([
      {
        title: 'File',
        items: [
          {title: 'New', shortcut: 'Ctrl+N', onSelect: () => chose('menubar New')},
          {title: 'Open…', shortcut: 'Ctrl+O', onSelect: () => chose('menubar Open')},
          {type: 'separator'},
          {title: 'Close', shortcut: 'Ctrl+W', disabled: true},
        ],
      },
      {
        title: 'View',
        items: [{title: 'Sidebar', checked: sidebar, shortcut: 'F9', onSelect: () => setSidebar(s => !s)}],
      },
    ]);
  }, [sidebar]);
  useEffect(() => () => MenuBar.clear(), []);

  const items = [
    // These report through ContextMenu's onSelect.
    {title: 'Open', shortcut: 'Ctrl+O'},
    {title: 'Rename…', shortcut: 'F2'},
    {type: 'separator'},
    {title: 'Show hidden files', checked: hidden, onSelect: () => setHidden(h => !h)},
    {
      title: 'Sort by',
      items: [
        {title: 'Name', type: 'radio', checked: sort === 'name', onSelect: () => setSort('name')},
        {title: 'Date', type: 'radio', checked: sort === 'date', onSelect: () => setSort('date')},
      ],
    },
    '-',
    {title: 'Delete', shortcut: 'Delete', disabled: true},
  ];

  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        #{count} {last} · hidden {String(hidden)} · sort {sort} · sidebar {String(sidebar)}
      </Text>
      <ContextMenu nativeID="menu-box" items={items} onSelect={item => chose(`via ${item.title}`)} style={styles.box}>
        <Text style={styles.label}>Right-click anywhere in here, or focus the button and press Menu</Text>
        <View nativeID="plain-inner" style={styles.inner}>
          <Text style={styles.label}>No menu of its own: the box's</Text>
        </View>
        <View
          nativeID="own-inner"
          style={styles.inner}
          contextMenu={[{title: 'Inner action', onSelect: () => chose('Inner action')}]}>
          <Text style={styles.label}>A menu of its own</Text>
        </View>
        <Pressable nativeID="focus-me" focusable onPress={() => {}} style={styles.button}>
          <Text style={styles.buttonText}>Focus me</Text>
        </Pressable>
        <TextInput nativeID="plain-input" style={styles.input} defaultValue="GTK's own menu here" />
        <TextInput
          nativeID="menu-input"
          style={styles.input}
          defaultValue="This one has its own"
          contextMenu={[{title: 'Insert date', onSelect: () => chose('Insert date')}]}
        />
      </ContextMenu>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#1C1C1E'},
  box: {padding: 16, gap: 10, borderRadius: 10, borderWidth: 1, borderColor: '#C0C0C8', backgroundColor: '#F6F6F8'},
  inner: {padding: 12, borderRadius: 8, backgroundColor: '#E4E4EA'},
  label: {fontSize: 14, color: '#1C1C1E'},
  button: {alignSelf: 'flex-start', paddingVertical: 8, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  buttonText: {color: '#FFFFFF'},
  input: {borderWidth: 1, borderColor: '#C0C0C8', borderRadius: 6, padding: 6, fontSize: 14},
});
