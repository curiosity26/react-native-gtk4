/**
 * Linux override of react-native/Libraries/Alert/RCTAlertManager.
 *
 * Alert has no Linux native module yet. Follow Android's dialog-manager
 * shape, which is what a GTK dialog module will implement.
 */
export * from 'react-native-upstream/Libraries/Alert/RCTAlertManager.android';
