/**
 * @react-native-community/cli configuration: the `linux` platform and the
 * init-linux / run-linux commands (lib/cli).
 */
'use strict';

const {projectConfig, dependencyConfig} = require('./lib/cli/config');

module.exports = {
  platforms: {
    // No npmPackageName: with one, the CLI's Metro config resolves
    // 'react-native' to that package on this platform (react-native-windows
    // is a fork of React Native's JS). Linux uses React Native's own JS,
    // plus metro-config's overrides.
    linux: {
      projectConfig,
      dependencyConfig,
    },
  },
  commands: require('./lib/cli/commands'),
};
