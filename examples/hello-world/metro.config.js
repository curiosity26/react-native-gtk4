// The 'linux' platform comes from this repo's package. An app outside the
// repo would depend on @curiosity26/react-native-gtk4 and require
// '@curiosity26/react-native-gtk4/metro-config' instead.
const {getDefaultConfig} = require('../../metro-config');

module.exports = getDefaultConfig(__dirname);
