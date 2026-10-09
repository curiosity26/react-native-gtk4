// Alert.alert, Alert.prompt and the file dialogs (Dialogs from
// @curiosity26/react-native-gtk4). The last result shows in the status line.
// rn-gtk-host --module GalleryDialogs --self-test answers each dialog
// (buttons, Escape, typing, picking files in a folder it fills) and checks
// what JS got; the folder comes in as the `folder` prop.
import React, {useState} from 'react';
import {Alert, Pressable, StyleSheet, Text, View} from 'react-native';
import {Dialogs} from '@curiosity26/react-native-gtk4';

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryDialogs({folder = ''}) {
  const [result, setResult] = useState('-');
  const [count, setCount] = useState(0);
  const set = text => {
    setResult(text);
    setCount(c => c + 1);
  };
  const files = (what, promise) =>
    promise.then(
      paths => set(`${what}: ${JSON.stringify(paths)}`),
      e => set(`${what} failed: ${e.message}`),
    );
  return (
    <View style={styles.root}>
      <Text nativeID="result" style={styles.status}>
        #{count} {result}
      </Text>
      <View style={styles.row}>
        <Action
          id="alert-ok"
          label="Alert (one button)"
          onPress={() => Alert.alert('Saved', 'Your changes are saved.', [{text: 'OK', onPress: () => set('ok')}])}
        />
        <Action
          id="alert-three"
          label="Alert (three buttons)"
          onPress={() =>
            Alert.alert('Delete “notes.txt”?', 'It will be gone for good.', [
              {text: 'Keep', onPress: () => set('keep')},
              {text: 'Delete', style: 'destructive', onPress: () => set('delete')},
              {text: 'Cancel', style: 'cancel', onPress: () => set('cancel')},
            ])
          }
        />
        <Action
          id="alert-cancelable"
          label="Cancelable alert"
          onPress={() =>
            Alert.alert('Heads up', 'Escape dismisses this one.', [{text: 'Fine', onPress: () => set('fine')}], {
              cancelable: true,
              onDismiss: () => set('dismissed'),
            })
          }
        />
        <Action id="alert-default" label="Alert (no buttons)" onPress={() => Alert.alert('Just a title')} />
      </View>
      <View style={styles.row}>
        <Action
          id="prompt"
          label="Prompt"
          onPress={() => Alert.prompt('Your name', 'What should we call you?', text => set(`name: ${text}`), 'plain-text', 'Ada')}
        />
        <Action
          id="prompt-secure"
          label="Password prompt"
          onPress={() =>
            Alert.prompt('Password', null, [
              {text: 'Cancel', style: 'cancel', onPress: () => set('password cancelled')},
              {text: 'Unlock', onPress: text => set(`password: ${text}`)},
            ], 'secure-text')
          }
        />
        <Action
          id="prompt-login"
          label="Login prompt"
          onPress={() =>
            Alert.prompt(
              'Sign in',
              'to example.com',
              [{text: 'Cancel', style: 'cancel'}, {text: 'Sign in', onPress: v => set(`login: ${v.login} / ${v.password}`)}],
              'login-password',
              'ada',
            )
          }
        />
      </View>
      <View style={styles.row}>
        <Action
          id="open-file"
          label="Open a file"
          onPress={() =>
            files(
              'open',
              Dialogs.openFile({
                title: 'Open a text file',
                defaultPath: folder,
                filters: [
                  {name: 'Text files', extensions: ['txt']},
                  {name: 'Images', mimeTypes: ['image/*']},
                ],
              }),
            )
          }
        />
        <Action
          id="open-files"
          label="Open files"
          onPress={() => files('open several', Dialogs.openFile({multiple: true, defaultPath: folder}))}
        />
        <Action
          id="save-file"
          label="Save"
          onPress={() => files('save', Dialogs.saveFile({defaultPath: folder, defaultName: 'out.txt', buttonLabel: 'Export'}))}
        />
        <Action
          id="open-folder"
          label="Pick a folder"
          onPress={() => files('folder', Dialogs.openFolder({defaultPath: folder}))}
        />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#1C1C1E'},
  row: {flexDirection: 'row', gap: 8, flexWrap: 'wrap'},
  action: {paddingVertical: 8, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  actionText: {color: '#FFFFFF', fontSize: 14},
});
