/**
 * Linux override of
 * react-native/Libraries/Components/AccessibilityInfo/legacySendAccessibilityEvent.
 *
 * Only used by the pre-Fabric renderer. The iOS variant checks that its
 * native module exists; Android's throws without UIManager.sendAccessibilityEvent.
 */
export {default} from 'react-native-upstream/Libraries/Components/AccessibilityInfo/legacySendAccessibilityEvent.ios';
