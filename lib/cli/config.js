/**
 * The `linux` platform for @react-native-community/cli's config: the app's
 * linux/ project, and its dependencies' linux/ folders (autolinking.js).
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

/** A dependency's Linux native code (autolinked by run-linux), or null. */
const {dependencyConfig} = require('./autolinking');

module.exports = {projectConfig, dependencyConfig};
