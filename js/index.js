/**
 * @curiosity26/react-native-gtk4: the Linux APIs React Native has none for.
 * (React Native's own components and APIs come from 'react-native'.)
 */
import ContextMenu from './ContextMenu';
import MenuBar from './MenuBar';

export {default as Dialogs} from './Dialogs';
export {default as Notifications} from './Notifications';
export {default as TitleBar, WindowControls} from './TitleBar';
export {default as Windows, useWindow} from './Windows';
export {ContextMenu, MenuBar};

// Menus together: Menu.setMenuBar(...), <Menu.ContextMenu>.
export const Menu = {
  ContextMenu,
  setMenuBar: MenuBar.setMenu,
  clearMenuBar: MenuBar.clear,
};
