/**
 * The `linux` platform for @react-native-community/cli's config.
 * Autolinking native modules for Linux is Phase 3: dependencies don't
 * contribute anything to the Linux build yet.
 */
'use strict';

const fs = require('fs');
const path = require('path');

/** The app's linux/ project, or null when init-linux hasn't run. */
function projectConfig(projectRoot, userConfig = {}) {
  const sourceDir = path.resolve(projectRoot, userConfig.sourceDir || 'linux');
  const cmakeLists = path.join(sourceDir, 'CMakeLists.txt');
  if (!fs.existsSync(cmakeLists)) return null;
  return {sourceDir, cmakeLists};
}

/** A dependency's Linux native code: none until autolinking (Phase 3). */
function dependencyConfig(_root, _userConfig = {}) {
  return null;
}

module.exports = {projectConfig, dependencyConfig};
