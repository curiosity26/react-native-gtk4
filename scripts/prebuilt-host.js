#!/usr/bin/env node
/**
 * The release workflow's prebuilt hosts (lib/cli/prebuilt.js).
 *
 *   scripts/prebuilt-host.js build DIST [--release]
 *       Builds React Native's dependencies, Hermes and the host (Debug, or
 *       Release) from source in a scratch cache and packs it into DIST/.
 *   scripts/prebuilt-host.js manifest DIST --base-url URL
 *       Writes prebuilt-hosts.json (the package's list of them, with their
 *       checksums) from the tarballs in DIST/.
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const paths = require('../lib/cli/paths');
const prebuilt = require('../lib/cli/prebuilt');
const {parseOsRelease} = require('../lib/cli/prereqs');
const {ensureDeps, ensureHost, run} = require('../lib/cli/runLinux');

function system() {
  const s = prebuilt.prebuiltSystem(parseOsRelease(fs.readFileSync('/etc/os-release', 'utf8')));
  if (!s) throw new Error('prebuilt hosts are built on Ubuntu 24.04');
  return s;
}

function build(dist, release) {
  fs.mkdirSync(dist, {recursive: true});
  const cache = process.env.RNGTK_CACHE_DIR || fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-prebuilt-'));
  const env = {...process.env, RNGTK_CACHE_DIR: cache};
  const deps = paths.depsDir(env);
  ensureDeps(deps);
  const host = paths.hostDir(env, paths.PACKAGE_DIR, {release});
  ensureHost(host, deps, {quiet: false, release, allowPrebuilt: false});
  const arch = {arm64: 'aarch64', x64: 'x86_64'}[process.arch] || process.arch;
  const name = prebuilt.assetName({version: paths.packageVersion(), system: system(), arch, release});
  const sha = prebuilt.packPrebuilt(path.join(host, 'install'), deps, path.join(dist, name), {run});
  console.log(`${sha}  ${name}`);
}

function manifest(dist, baseUrl) {
  const hosts = {};
  for (const f of fs.readdirSync(dist).filter(f => /^react-native-gtk4-host-.*\.tar\.gz$/.test(f)).sort()) {
    const sha256 = require('crypto').createHash('sha256').update(fs.readFileSync(path.join(dist, f))).digest('hex');
    hosts[f] = {sha256, size: fs.statSync(path.join(dist, f)).size};
  }
  const out = {
    version: paths.packageVersion(),
    reactNative: paths.reactNativeVersion(),
    hostSourceId: paths.hostSourceId(),
    baseUrl: baseUrl.replace(/\/$/, ''),
    hosts,
  };
  fs.writeFileSync(path.join(paths.PACKAGE_DIR, prebuilt.MANIFEST), JSON.stringify(out, null, 2) + '\n');
  console.log(JSON.stringify(out, null, 2));
}

const [cmd, dist, ...rest] = process.argv.slice(2);
if (cmd === 'build' && dist) {
  build(path.resolve(dist), rest.includes('--release'));
} else if (cmd === 'manifest' && dist && rest[0] === '--base-url' && rest[1]) {
  manifest(path.resolve(dist), rest[1]);
} else {
  console.error('usage: prebuilt-host.js build DIST [--release] | manifest DIST --base-url URL');
  process.exit(2);
}
