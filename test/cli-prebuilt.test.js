'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {describe, test} = require('node:test');

const prebuilt = require('../lib/cli/prebuilt');

const NOBLE = {ID: 'ubuntu', VERSION_ID: '24.04', VERSION_CODENAME: 'noble', UBUNTU_CODENAME: 'noble'};
const MINT = {ID: 'linuxmint', ID_LIKE: 'ubuntu debian', VERSION_ID: '22.1', UBUNTU_CODENAME: 'noble'};

const manifest = {
  version: '0.1.0',
  hostSourceId: 'abc123',
  baseUrl: 'https://github.com/curiosity26/react-native-gtk4/releases/download/v0.1.0',
  hosts: {
    'react-native-gtk4-host-0.1.0-ubuntu24.04-aarch64-release.tar.gz': {sha256: 'f00'},
    'react-native-gtk4-host-0.1.0-ubuntu24.04-x86_64-debug.tar.gz': {sha256: 'ba7'},
  },
};

describe('prebuilt hosts', () => {
  test('are for Ubuntu 24.04 and what is based on it', () => {
    assert.equal(prebuilt.prebuiltSystem(NOBLE), 'ubuntu24.04');
    assert.equal(prebuilt.prebuiltSystem(MINT), 'ubuntu24.04');
    assert.equal(prebuilt.prebuiltSystem({ID: 'ubuntu', VERSION_CODENAME: 'plucky'}), null);
    assert.equal(prebuilt.prebuiltSystem({ID: 'fedora', VERSION_ID: '42'}), null);
    assert.equal(prebuilt.prebuiltSystem({ID: 'debian', VERSION_CODENAME: 'trixie'}), null);
  });

  test('findPrebuilt matches the package, the system, the architecture and the build', () => {
    const find = o => prebuilt.findPrebuilt({manifest, osRelease: NOBLE, sourceId: 'abc123', env: {}, ...o});
    assert.deepEqual(find({release: true, arch: 'aarch64'}), {
      url: `${manifest.baseUrl}/react-native-gtk4-host-0.1.0-ubuntu24.04-aarch64-release.tar.gz`,
      sha256: 'f00',
      name: 'react-native-gtk4-host-0.1.0-ubuntu24.04-aarch64-release.tar.gz',
    });
    assert.equal(find({release: false, arch: 'x86_64'}).sha256, 'ba7');
    assert.match(find({release: false, arch: 'aarch64'}).reason, /no prebuilt/);
    assert.match(find({release: true, arch: 'aarch64', sourceId: 'changed'}).reason, /isn't the one/);
    assert.match(find({release: true, arch: 'aarch64', osRelease: {ID: 'fedora'}}).reason, /Ubuntu 24\.04/);
    assert.match(find({release: true, arch: 'aarch64', env: {RNGTK_NO_PREBUILT: '1'}}).reason, /RNGTK_NO_PREBUILT/);
    assert.match(find({release: true, manifest: null}).reason, /no prebuilt hosts/);
  });

  test('relocate fills in where the host and the dependencies are', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-prebuilt-'));
    try {
      const sdk = path.join(dir, 'lib', 'cmake', 'ReactNativeGtk', 'ReactNativeGtkSdk.cmake');
      fs.mkdirSync(path.dirname(sdk), {recursive: true});
      fs.writeFileSync(sdk, `set(X [==[${prebuilt.PREFIX_TOKEN}/include;${prebuilt.DEPS_TOKEN}/react-native;${prebuilt.DEPS_TOKEN}/hermes-headers]==])\n`);
      prebuilt.relocate(dir, '/cache/deps');
      assert.equal(fs.readFileSync(sdk, 'utf8'), `set(X [==[${dir}/include;/cache/deps/react-native;/cache/deps/hermes-headers]==])\n`);
    } finally {
      fs.rmSync(dir, {recursive: true, force: true});
    }
  });
});
