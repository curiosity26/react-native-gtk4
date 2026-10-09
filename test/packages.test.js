'use strict';

// The library ports in packages/: each autolinks (a linux/ folder its CLI
// config names) and is a package of its own.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {describe, test} = require('node:test');

const {dependencyConfig} = require('../lib/cli/autolinking');

const PACKAGES = path.join(__dirname, '..', 'packages');

describe('library ports', () => {
  for (const dir of fs.readdirSync(PACKAGES)) {
    const root = path.join(PACKAGES, dir);
    test(dir, () => {
      const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
      assert.equal(pkg.name, `@curiosity26/react-native-gtk4-${dir}`);
      assert.equal(pkg.version, require('../package.json').version);
      assert.ok(pkg.files.includes('linux/'));
      const linux = require(path.join(root, 'react-native.config.js')).dependency.platforms.linux;
      const config = dependencyConfig(root, linux);
      assert.ok(config, 'has linux/CMakeLists.txt');
      const cmake = fs.readFileSync(path.join(root, 'linux', 'CMakeLists.txt'), 'utf8');
      assert.match(cmake, new RegExp(`add_library\\(${config.cmakeTarget} STATIC`));
      const sources = fs.readdirSync(path.join(root, 'linux', 'src')).map(f => fs.readFileSync(path.join(root, 'linux', 'src', f), 'utf8'));
      assert.ok(
        sources.some(s => s.includes(`std::shared_ptr<const rngtk::Package> ${config.packageFunction}()`)),
        `defines ${config.packageFunction}()`,
      );
    });
  }
});
