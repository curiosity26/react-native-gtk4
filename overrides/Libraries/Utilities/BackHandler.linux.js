/**
 * Linux override of react-native/Libraries/Utilities/BackHandler.
 *
 * Desktops have no hardware back button: behave like iOS, where listeners
 * are accepted and never called.
 */
export {default} from 'react-native-upstream/Libraries/Utilities/BackHandler.ios';
