/**
 * Linux override of
 * react-native/src/private/devsupport/rndevtools/ReactDevToolsSettingsManager.
 *
 * Follows Android, which calls the optional ReactDevToolsSettingsManager
 * native module and does nothing without it. (iOS uses its Settings module,
 * which Linux does not have.)
 */
export * from 'react-native/src/private/devsupport/rndevtools/ReactDevToolsSettingsManager.android';
