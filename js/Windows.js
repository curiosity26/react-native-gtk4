/**
 * More windows: each shows a component registered with AppRegistry, as a
 * surface of its own in the app's one JS runtime (state shared through
 * modules, stores, context providers you render in each).
 *
 *   import {Windows, useWindow} from '@curiosity26/react-native-gtk4';
 *
 *   AppRegistry.registerComponent('Inspector', () => Inspector);
 *
 *   const inspector = Windows.open({
 *     component: 'Inspector',
 *     initialProps: {documentId: 42},
 *     title: 'Inspector',
 *     width: 360, height: 600,
 *     minWidth: 280, minHeight: 300,
 *     resizable: true,
 *   });
 *   inspector.addListener('closed', () => ...);
 *   inspector.setTitle('Inspector — notes.txt');
 *   inspector.setSize(400, 640);
 *   inspector.focus();
 *   inspector.close();
 *
 *   Windows.main.setTitle('My App — notes.txt');
 *
 *   function Inspector() {
 *     const window = useWindow();          // this component's window
 *     const {width} = useWindowDimensions(); // ...and its size
 *   }
 *
 * Events: 'focus', 'blur', 'resize' ({width, height}), 'close-requested'
 * (the close button; with interceptClose: true the window stays open until
 * you close() it), 'closed'. The main window hides on close while other
 * windows are open; the app quits when the last one closes, unless
 * Windows.setQuitOnLastWindowClosed(false).
 */
import {useContext} from 'react';
import {RootTagContext} from 'react-native';

import {NativeWindows, mainWindowId, onWindowEvent} from './windowEvents';

const listeners = new Map(); // id -> type -> Set(listener)
let subscribed = false;

function subscribe() {
  if (subscribed) {
    return;
  }
  subscribed = true;
  onWindowEvent(event => {
    const byType = listeners.get(event.id);
    const set = byType?.get(event.type);
    if (set != null) {
      for (const listener of [...set]) {
        listener(event);
      }
    }
    if (event.type === 'closed' && event.id !== mainWindowId()) {
      listeners.delete(event.id);
    }
  });
}

class WindowHandle {
  id: number;
  constructor(id: number) {
    this.id = id;
  }
  /** The window's surface root tag (the same number). */
  get rootTag(): number {
    return this.id;
  }
  close() {
    NativeWindows?.close(this.id);
  }
  setTitle(title: string) {
    NativeWindows?.setTitle(this.id, String(title));
  }
  /** The content's size, in points. */
  setSize(width: number, height: number) {
    NativeWindows?.setSize(this.id, width, height);
  }
  setMinimumSize(width: number, height: number) {
    NativeWindows?.setMinSize(this.id, width, height);
  }
  /** Raises it (and shows the main window again after it was closed). */
  focus() {
    NativeWindows?.focus(this.id);
  }
  /** The close button only sends 'close-requested'; close() closes. */
  setInterceptClose(intercept: boolean) {
    NativeWindows?.setInterceptClose(this.id, intercept);
  }
  addListener(type: string, listener: Function): {remove: () => void} {
    subscribe();
    if (!listeners.has(this.id)) {
      listeners.set(this.id, new Map());
    }
    const byType = listeners.get(this.id);
    if (!byType.has(type)) {
      byType.set(type, new Set());
    }
    byType.get(type).add(listener);
    return {remove: () => byType.get(type)?.delete(listener)};
  }
}

const handles = new Map();
function handle(id: number): WindowHandle {
  if (!handles.has(id)) {
    handles.set(id, new WindowHandle(id));
  }
  return handles.get(id);
}

const Windows = {
  /**
   * Opens a window showing `component` (an AppRegistry name). Options:
   * initialProps, title, width, height, minWidth, minHeight, resizable,
   * interceptClose.
   */
  open(options: {component: string, ...}): WindowHandle {
    if (NativeWindows == null) {
      throw new Error('Windows are only available on Linux');
    }
    if (typeof options?.component !== 'string') {
      throw new Error('Windows.open: `component` must be a registered component name');
    }
    subscribe();
    return handle(NativeWindows.open(options));
  },
  /** The main window (the app's first). */
  get main(): WindowHandle {
    return handle(mainWindowId());
  },
  /** A window by id (its root tag). */
  get(id: number): WindowHandle {
    return handle(id);
  },
  /** The open windows: [{id, title, width, height, main}], main first. */
  getAll(): Array<{id: number, title: string, width: number, height: number, main: boolean}> {
    return NativeWindows?.getWindows() ?? [];
  },
  /** Default true: the app quits once its last window closes. */
  setQuitOnLastWindowClosed(quit: boolean) {
    NativeWindows?.setQuitOnLastWindowClosed(quit);
  },
};

/** The window the calling component is in. */
export function useWindow(): WindowHandle {
  const rootTag = useContext(RootTagContext);
  return handle(Number(rootTag));
}

export default Windows;
