/**
 * @react-native-community/cli configuration: the `linux` platform and the
 * init-linux / run-linux commands (lib/cli).
 */
'use strict';

const {projectConfig, dependencyConfig} = require('./lib/cli/config');

module.exports = {
  platforms: {
    linux: {
      npmPackageName: '@curiosity26/react-native-gtk4',
      projectConfig,
      dependencyConfig,
    },
  },
  commands: require('./lib/cli/commands'),
};
