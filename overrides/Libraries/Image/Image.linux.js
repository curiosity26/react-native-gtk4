/**
 * Linux override of react-native/Libraries/Image/Image.
 *
 * Follows Android: the C++ core's image component and ImageLoader module
 * are shared with Android, and the GTK host implements that interface.
 */
export {default} from 'react-native/Libraries/Image/Image.android';
