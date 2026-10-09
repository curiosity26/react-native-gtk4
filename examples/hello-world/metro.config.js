// The 'linux' platform comes from this repo's package. An app outside the
// repo would depend on @curiosity26/react-native-gtk4 and require
// '@curiosity26/react-native-gtk4/metro-config' instead.
const path = require('path');
const {getDefaultConfig} = require('../../metro-config');

const config = getDefaultConfig(__dirname);
// The package isn't in node_modules here: its name resolves to its source
// (js/, which getDefaultConfig watches).
const resolveRequest = config.resolver.resolveRequest;
config.resolver.resolveRequest = (context, moduleName, platform) =>
  moduleName === '@curiosity26/react-native-gtk4'
    ? {type: 'sourceFile', filePath: path.resolve(__dirname, '../../js/index.js')}
    : resolveRequest(context, moduleName, platform);

module.exports = config;
