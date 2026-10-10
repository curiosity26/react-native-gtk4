/**
 * Linux override of react-native/Libraries/NativeComponent/BaseViewConfig.
 *
 * Follows Android: the GTK host runs React Native's shared C++ view props,
 * which accept Android's view config (event names, accessibility props).
 * Android's already has focusable and the focus, blur, keyDown and keyUp
 * events; this adds the host's keyboard and desktop mouse props, spelled
 * as in react-native-windows and react-native-macos.
 *
 * @flow strict-local
 * @format
 */

import type {PartialViewConfigWithoutName} from 'react-native-upstream/Libraries/NativeComponent/PlatformBaseViewConfig';

import AndroidConfig from 'react-native-upstream/Libraries/NativeComponent/BaseViewConfig.android';

const PlatformBaseViewConfigLinux: PartialViewConfigWithoutName = {
  ...AndroidConfig,
  bubblingEventTypes: {
    ...AndroidConfig.bubblingEventTypes,
    // A middle or right click (W3C auxclick).
    topAuxClick: {
      phasedRegistrationNames: {
        captured: 'onAuxClickCapture',
        bubbled: 'onAuxClick',
      },
    },
  },
  directEventTypes: {
    ...AndroidConfig.directEventTypes,
    // The mouse entering and leaving the view (not bubbling).
    topMouseEnter: {registrationName: 'onMouseEnter'},
    topMouseLeave: {registrationName: 'onMouseLeave'},
    // A context menu item was chosen: {id} (View.linux.js runs its
    // onSelect).
    topContextMenuSelect: {registrationName: 'onContextMenuSelect'},
    // Drag and drop (react-native-macos): a drag of something the view's
    // draggedTypes take came over it, left it, or was dropped on it.
    topDragEnter: {registrationName: 'onDragEnter'},
    topDragLeave: {registrationName: 'onDragLeave'},
    topDrop: {registrationName: 'onDrop'},
  },
  validAttributes: {
    ...AndroidConfig.validAttributes,
    // Keys the view handles itself: [{key, code, altKey, ctrlKey,
    // metaKey, shiftKey}] (GTK then doesn't act on them).
    keyDownEvents: true,
    keyUpEvents: true,
    // react-native-macos: draw the keyboard focus ring (default true).
    enableFocusRing: true,
    // Take keyboard focus once mounted.
    autoFocus: true,
    // A GTK tooltip.
    tooltip: true,
    // iOS's, which the GTK host maps to AT-SPI too (Android's config
    // lacks them).
    accessibilityElementsHidden: true,
    accessibilityViewIsModal: true,
    // Listeners, so the host sends only the mouse events views want.
    onMouseEnter: true,
    onMouseLeave: true,
    onAuxClick: true,
    onAuxClickCapture: true,
    // Menu items for right-click, the Menu key and Shift+F10.
    contextMenu: true,
    // What drops the view takes: 'fileUrl', 'string', 'image'.
    draggedTypes: true,
    // An Image that can be dragged out.
    draggable: true,
    // Dragging it moves the window (<TitleBar>).
    windowDragRegion: true,
  },
};

export default PlatformBaseViewConfigLinux;
