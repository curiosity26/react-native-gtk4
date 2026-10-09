/**
 * Types for React Native on Linux.
 *
 * React Native declares PlatformOSType and its Platform types as type
 * aliases, which TypeScript cannot extend from outside, so `Platform.OS`
 * stays typed without 'linux' (react-native-windows and react-native-macos
 * are listed upstream). Until that changes, narrow by hand:
 *
 *   import {Platform} from 'react-native';
 *   import type {LinuxPlatform} from '@curiosity26/react-native-gtk4';
 *
 *   if ((Platform.OS as string) === 'linux') {
 *     const {windowSystem} = (Platform as unknown as LinuxPlatform).constants;
 *   }
 */

/** Platform.constants on Linux (overrides/Libraries/Utilities/NativePlatformConstantsLinux.js). */
export type PlatformConstantsLinux = {
  isTesting: boolean;
  isDisableAnimations?: boolean | undefined;
  reactNativeVersion: {
    major: number;
    minor: number;
    patch: number;
    prerelease: string | null | undefined;
  };
  /** VERSION_ID from /etc/os-release, or the kernel release if there is none. */
  Version: string;
  /** PRETTY_NAME from /etc/os-release. */
  Release: string;
  /** ID from /etc/os-release, e.g. "ubuntu", "fedora", "linuxmint". */
  osId: string;
  /** VERSION_ID from /etc/os-release, or '' if there is none. */
  osVersion: string;
  /** uname -r */
  kernelRelease: string;
  /** The GTK version the host runs with, e.g. "4.14.5". */
  gtkVersion: string;
  windowSystem: 'wayland' | 'x11' | 'unknown';
  /** XDG_CURRENT_DESKTOP, e.g. "ubuntu:GNOME" or "X-Cinnamon"; '' if unset. */
  desktop: string;
};

export type LinuxPlatform = {
  OS: 'linux';
  readonly Version: string;
  readonly constants: PlatformConstantsLinux;
  readonly isTV: false;
  readonly isVision: false;
  readonly isTesting: boolean;
  readonly isDisableAnimations: boolean;
  select<T>(spec: {linux?: T; native?: T; default?: T; [os: string]: T | undefined}): T;
};

/**
 * An entry of keyDownEvents / keyUpEvents: a key the view handles itself,
 * so GTK doesn't act on it (Tab won't move focus, a TextInput won't type
 * it). Matched by `key` (react-native-macos) or `code`
 * (react-native-windows); a modifier left out matches either way.
 */
export type HandledKeyEvent = {
  key?: string;
  code?: string;
  altKey?: boolean;
  ctrlKey?: boolean;
  metaKey?: boolean;
  shiftKey?: boolean;
};

/** The View props the Linux host adds (docs/components.md). */
export type ViewPropsLinux = {
  keyDownEvents?: ReadonlyArray<HandledKeyEvent>;
  keyUpEvents?: ReadonlyArray<HandledKeyEvent>;
  /** Draw the keyboard focus ring (default true), as react-native-macos. */
  enableFocusRing?: boolean;
  /** Take keyboard focus once mounted. */
  autoFocus?: boolean;
  /** A GTK tooltip. */
  tooltip?: string;
  onMouseEnter?: (event: {nativeEvent: {clientX: number; clientY: number; offsetX: number; offsetY: number}}) => void;
  onMouseLeave?: (event: {nativeEvent: {clientX: number; clientY: number; offsetX: number; offsetY: number}}) => void;
  /** A middle (button 1) or right (button 2) click. */
  onAuxClick?: (event: {nativeEvent: {button: number}}) => void;
  /** A menu for right-click, the Menu key and Shift+F10 (TextInput too). */
  contextMenu?: MenuItem[];
  /** An item of contextMenu was chosen (after its onSelect). */
  onContextMenuSelect?: (event: {nativeEvent: {id: string}}) => void;
  /** Drag and drop (react-native-macos): what drops the view takes. */
  draggedTypes?: DraggedType[];
  onDragEnter?: (event: {nativeEvent: DragEvent}) => void;
  onDragLeave?: (event: {nativeEvent: DragEvent}) => void;
  onDrop?: (event: {nativeEvent: DragEvent}) => void;
  /** An Image that can be dragged out (its picture, and its file or URL). */
  draggable?: boolean;
};

/** 'fileUrl': files and links (a URI list); 'string': text; 'image': image data. */
export type DraggedType = 'fileUrl' | 'string' | 'image';

/** react-native-macos' DataTransfer, plus Linux's text and urls. */
export type DragEvent = {
  clientX: number;
  clientY: number;
  dataTransfer: {
    /** On drop: local files (and dropped image data, saved as PNG). */
    files: Array<{name: string; type: string; uri: string; size?: number; width?: number; height?: number}>;
    items: Array<{kind: 'file' | 'string'; type: string}>;
    /** MIME types. */
    types: string[];
    /** On drop, Linux: the text, for 'string'. */
    text?: string;
    /** On drop, Linux: links that aren't local files, for 'fileUrl'. */
    urls?: string[];
  };
};

/** A file type filter for Dialogs: matches any of its extensions, MIME types or patterns. */
export type FileFilter = {
  name: string;
  /** Without the dot: ['png', 'jpg']. */
  extensions?: string[];
  /** ['image/*', 'text/plain'] */
  mimeTypes?: string[];
  /** Glob patterns: ['*.tar.gz']. */
  patterns?: string[];
};

export type FileDialogOptions = {
  title?: string;
  /** The accept button's label ("Open", "Save" by default). */
  buttonLabel?: string;
  /** A folder to start in, or a file to select (saveFile: to save as). */
  defaultPath?: string;
  /** The first is selected. */
  filters?: FileFilter[];
};

/**
 * File dialogs on GtkFileDialog: the desktop's file chooser (its portal),
 * modal over the app's active window. Paths are local paths (a URI for a
 * file without one).
 */
export declare const Dialogs: {
  /** The files picked; [] if cancelled. */
  openFile(options?: FileDialogOptions & {multiple?: boolean}): Promise<string[]>;
  /** The path to save to; null if cancelled. */
  saveFile(options?: FileDialogOptions & {defaultName?: string}): Promise<string | null>;
  /** The folders picked; [] if cancelled. */
  openFolder(options?: FileDialogOptions & {multiple?: boolean}): Promise<string[]>;
};

/**
 * A menu item: for a View's `contextMenu`, ContextMenu and MenuBar.
 * `checked` alone makes a checkbox; radio items between two separators are
 * one group. `shortcut`: 'Ctrl+Shift+S', 'Alt+F4', 'Delete', or GTK's
 * '<Control>s' (a label in context menus; a working accelerator in the
 * menu bar).
 */
export type MenuItem =
  | {
      title: string;
      onSelect?: (item: MenuItem) => void;
      disabled?: boolean;
      checked?: boolean;
      type?: 'item' | 'checkbox' | 'radio';
      shortcut?: string;
      /** A submenu. */
      items?: MenuItem[];
    }
  | {type: 'separator'}
  | '-';

/** A View with a context menu; onSelect runs for every item chosen. */
export declare function ContextMenu(
  props: import('react-native').ViewProps & {items: MenuItem[]; onSelect?: (item: MenuItem) => void},
): import('react').ReactElement;

/** The app's menu bar: menus, each {title, items}. */
export declare const MenuBar: {
  setMenu(menus: Array<{title: string; items: MenuItem[]}>): void;
  clear(): void;
};

export declare const Menu: {
  ContextMenu: typeof ContextMenu;
  setMenuBar: typeof MenuBar.setMenu;
  clearMenuBar: typeof MenuBar.clear;
};

export type WindowEvent =
  | 'focus'
  | 'blur'
  | 'resize'
  | 'close-requested'
  | 'closed'
  | 'open';

/** One of the app's windows (Windows.open, Windows.main, useWindow). */
export interface WindowHandle {
  /** The window's surface root tag. */
  readonly id: number;
  readonly rootTag: number;
  close(): void;
  setTitle(title: string): void;
  /** The content's size, in points. */
  setSize(width: number, height: number): void;
  setMinimumSize(width: number, height: number): void;
  /** Raises it (and shows the main window again after it was closed). */
  focus(): void;
  /** The close button only sends 'close-requested'; close() closes. */
  setInterceptClose(intercept: boolean): void;
  addListener(
    type: WindowEvent,
    listener: (event: {id: number; type: WindowEvent; width?: number; height?: number}) => void,
  ): {remove(): void};
}

export type WindowOptions = {
  /** A component registered with AppRegistry. */
  component: string;
  initialProps?: object;
  title?: string;
  width?: number;
  height?: number;
  minWidth?: number;
  minHeight?: number;
  resizable?: boolean;
  /** The close button only asks ('close-requested'). */
  interceptClose?: boolean;
};

/**
 * More windows, each a surface of a registered component in the app's one
 * JS runtime; and the main window's title, size and focus.
 */
export declare const Windows: {
  open(options: WindowOptions): WindowHandle;
  readonly main: WindowHandle;
  get(id: number): WindowHandle;
  getAll(): Array<{id: number; title: string; width: number; height: number; main: boolean}>;
  /** Default true: the app quits once its last window closes. */
  setQuitOnLastWindowClosed(quit: boolean): void;
};

/** The window the calling component is in. */
export declare function useWindow(): WindowHandle;

export type NotificationOptions = {
  /** Showing again with the same id replaces it. */
  id?: string;
  title: string;
  body?: string;
  /** An icon name ('mail-unread-symbolic'), or a file path or URI. */
  icon?: string;
  priority?: 'low' | 'normal' | 'high' | 'urgent';
  buttons?: Array<{id: string; title: string}>;
  /** The notification ('default') or a button (its id) was clicked. */
  onPress?: (action: string) => void;
};

/** Desktop notifications (GNotification). */
export declare const Notifications: {
  /** Returns the notification's id. */
  show(options: NotificationOptions): string;
  close(id: string): void;
  addListener(
    type: 'press',
    listener: (event: {id: string; action: string}) => void,
  ): {remove(): void};
  requestPermission(): Promise<'granted'>;
};
