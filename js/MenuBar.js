/**
 * The app's menu bar: menus of items (as in ContextMenu) shown at the top
 * of the app's windows (GtkApplicationWindow's menu bar). Shortcuts work
 * while the menus are closed.
 *
 *   MenuBar.setMenu([
 *     {title: 'File', items: [
 *       {title: 'New', shortcut: 'Ctrl+N', onSelect: newDocument},
 *       {title: 'Open…', shortcut: 'Ctrl+O', onSelect: open},
 *       {type: 'separator'},
 *       {title: 'Quit', shortcut: 'Ctrl+Q', onSelect: quit},
 *     ]},
 *     {title: 'View', items: [
 *       {title: 'Sidebar', checked: sidebar, onSelect: toggleSidebar},
 *     ]},
 *   ]);
 *
 * Call it again to change the menus (a checkbox's `checked`, a disabled
 * item); MenuBar.clear() removes the bar.
 */
import {TurboModuleRegistry} from 'react-native';

import {toNativeMenu} from './menuItems';

const NativeMenuBar = TurboModuleRegistry.get('LinuxMenuBar');

let current = null;

const MenuBar = {
  setMenu(menus) {
    if (NativeMenuBar == null) {
      return;
    }
    current = toNativeMenu(menus);
    NativeMenuBar.setMenu(current.items, id => current?.select(id));
  },
  clear() {
    MenuBar.setMenu([]);
  },
};

export default MenuBar;
