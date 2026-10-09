// Notifications (from @curiosity26/react-native-gtk4). The self-test runs
// on a private D-Bus with a stand-in notification server, which records
// what it gets and clicks the notification and its buttons:
// dbus-run-session -- rn-gtk-host --module GalleryNotifications --self-test
import React, {useEffect, useState} from 'react';
import {Pressable, StyleSheet, Text, View} from 'react-native';
import {Notifications} from '@curiosity26/react-native-gtk4';

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryNotifications() {
  const [events, setEvents] = useState([]);
  const log = line => setEvents(e => [...e, line].slice(-6));
  useEffect(() => {
    const sub = Notifications.addListener('press', e => log(`press ${e.id} ${e.action}`));
    return () => sub.remove();
  }, []);
  return (
    <View style={styles.root}>
      <Text style={styles.status}>events: {events.join(' | ')}</Text>
      <View style={styles.row}>
        <Action
          id="notify"
          label="Notify"
          onPress={() =>
            Notifications.show({
              id: 'download',
              title: 'Download finished',
              body: 'notes.pdf (2 MB)',
              icon: 'folder-download-symbolic',
              buttons: [
                {id: 'open', title: 'Open'},
                {id: 'show', title: 'Show in Files'},
              ],
              onPress: action => log(`onPress ${action}`),
            })
          }
        />
        <Action id="close" label="Withdraw it" onPress={() => Notifications.close('download')} />
        <Action
          id="urgent"
          label="Urgent"
          onPress={() => Notifications.show({title: 'Battery low', body: '5% left', priority: 'urgent'})}
        />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#1C1C1E'},
  row: {flexDirection: 'row', gap: 8},
  action: {paddingVertical: 8, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  actionText: {color: '#FFFFFF', fontSize: 14},
});
