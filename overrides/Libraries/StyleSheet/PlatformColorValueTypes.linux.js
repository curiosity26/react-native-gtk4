/**
 * Linux override of react-native/Libraries/StyleSheet/PlatformColorValueTypes.
 *
 * Follows Android: PlatformColor(...names) becomes {resource_paths: names},
 * the form the C++ color parser shared with Android reads. The GTK host
 * maps the names to theme colors.
 */
export * from 'react-native-upstream/Libraries/StyleSheet/PlatformColorValueTypes.android';
