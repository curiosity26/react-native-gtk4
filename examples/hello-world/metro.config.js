const {getDefaultConfig} = require('@react-native/metro-config');

// Phase 0 bundles for the 'android' platform: the C++ core is shared, so
// the Fabric renderer works unchanged. A 'linux' platform (with .linux.js
// files and Platform.OS === 'linux') comes with the out-of-tree platform
// config in Phase 1.
module.exports = getDefaultConfig(__dirname);
