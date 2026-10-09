/**
 * Where things live: this package, the React Native version it targets and
 * the shared build cache.
 *
 *   $RNGTK_CACHE_DIR (default $XDG_CACHE_HOME/react-native-gtk4, or
 *   ~/.cache/react-native-gtk4)
 *     <rn-version>/deps          React Native sources, third-party C++
 *                                libraries, Hermes (scripts/fetch-rn-deps.py,
 *                                scripts/build-hermes.sh); RNGTK_DEPS_DIR
 *                                overrides it
 *     <rn-version>/host/<id>/    the host library (build/ and install/), one
 *                                per content of this package's linux/
 */
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');

const PACKAGE_DIR = path.resolve(__dirname, '..', '..');
const PACKAGE_NAME = '@curiosity26/react-native-gtk4';

function packageVersion() {
  return JSON.parse(fs.readFileSync(path.join(PACKAGE_DIR, 'package.json'), 'utf8')).version;
}

/** Parses rn-version.properties (key=value lines). */
function parseProperties(text) {
  const out = {};
  for (const line of text.split('\n')) {
    const m = line.match(/^\s*([^#=\s][^=]*?)\s*=\s*(.*?)\s*$/);
    if (m) out[m[1]] = m[2].replace(/^"(.*)"$/, '$1');
  }
  return out;
}

function reactNativeVersion() {
  const text = fs.readFileSync(path.join(PACKAGE_DIR, 'rn-version.properties'), 'utf8');
  return parseProperties(text).reactNative;
}

function cacheRoot(env = process.env) {
  if (env.RNGTK_CACHE_DIR) return path.resolve(env.RNGTK_CACHE_DIR);
  const base = env.XDG_CACHE_HOME || path.join(os.homedir(), '.cache');
  return path.join(base, 'react-native-gtk4');
}

function depsDir(env = process.env) {
  if (env.RNGTK_DEPS_DIR) return path.resolve(env.RNGTK_DEPS_DIR);
  return path.join(cacheRoot(env), reactNativeVersion(), 'deps');
}

/**
 * A short hash of the files the host build reads from this package (linux/
 * and rn-version.properties), so a changed package gets its own host build.
 */
function hostSourceId(packageDir = PACKAGE_DIR) {
  const hash = crypto.createHash('sha256');
  const files = [];
  const walk = dir => {
    for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
      const p = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name !== 'build') walk(p);
      } else if (entry.isFile()) {
        files.push(p);
      }
    }
  };
  walk(path.join(packageDir, 'linux'));
  files.push(path.join(packageDir, 'rn-version.properties'));
  for (const f of files.sort()) {
    hash.update(path.relative(packageDir, f));
    hash.update('\0');
    hash.update(fs.readFileSync(f));
    hash.update('\0');
  }
  return hash.digest('hex').slice(0, 12);
}

function hostDir(env = process.env, packageDir = PACKAGE_DIR) {
  return path.join(cacheRoot(env), reactNativeVersion(), 'host', hostSourceId(packageDir));
}

module.exports = {
  PACKAGE_DIR,
  PACKAGE_NAME,
  packageVersion,
  parseProperties,
  reactNativeVersion,
  cacheRoot,
  depsDir,
  hostSourceId,
  hostDir,
};
