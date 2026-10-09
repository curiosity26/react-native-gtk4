/**
 * File dialogs: open files, save a file, pick folders. GtkFileDialog shows
 * the desktop's file chooser (its portal under Flatpak, GTK's own chooser
 * otherwise), modal over the app's active window.
 *
 *   import {Dialogs} from '@curiosity26/react-native-gtk4';
 *
 *   const [path] = await Dialogs.openFile({
 *     title: 'Open an image',
 *     filters: [{name: 'Images', extensions: ['png', 'jpg']}],
 *   });
 *   const paths = await Dialogs.openFile({multiple: true});
 *   const target = await Dialogs.saveFile({defaultName: 'notes.txt'});
 *   const [folder] = await Dialogs.openFolder();
 *
 * Paths are local paths (a file:// or other URI for files without one).
 * Cancelling resolves to [] (null from saveFile).
 */
import {TurboModuleRegistry} from 'react-native';

const NativeDialogs = TurboModuleRegistry.get('LinuxDialogs');

function call(method, options) {
  if (NativeDialogs == null) {
    return Promise.reject(new Error('Dialogs are only available on Linux'));
  }
  return NativeDialogs[method](options ?? {});
}

const Dialogs = {
  /** One file, or several with `multiple: true`. */
  openFile(options) {
    return call('openFile', options);
  },
  /** The path to save to, or null. */
  saveFile(options) {
    return call('saveFile', options);
  },
  /** One folder, or several with `multiple: true`. */
  openFolder(options) {
    return call('openFolder', options);
  },
};

export default Dialogs;
