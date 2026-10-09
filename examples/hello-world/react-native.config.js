// The linux platform and its commands (run-linux, package-linux) from this
// repository's package, which an app outside the repo gets from its
// @curiosity26/react-native-gtk4 dependency instead.
const pkg = require('../../react-native.config.js');

module.exports = {
  platforms: {linux: pkg.platforms.linux},
  commands: pkg.commands,
};
