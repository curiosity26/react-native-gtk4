'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {afterEach, beforeEach, describe, test} = require('node:test');

const {
  appNames,
  defaultAppId,
  editMetroConfig,
  editPackageJson,
  initLinux,
  renderTemplate,
  targetName,
} = require('../lib/cli/initLinux');
const {projectConfig, dependencyConfig} = require('../lib/cli/config');

const PKG = '@curiosity26/react-native-gtk4';

// metro.config.js as `@react-native-community/cli init` writes it for 0.87.
const RN_TEMPLATE_METRO = `const {getDefaultConfig, mergeConfig} = require('@react-native/metro-config');

/**
 * Metro configuration
 * https://reactnative.dev/docs/metro
 *
 * @type {import('@react-native/metro-config').MetroConfig}
 */
const config = {};

module.exports = mergeConfig(getDefaultConfig(__dirname), config);
`;

describe('editMetroConfig', () => {
  test("wraps the React Native template's export with withLinux", () => {
    const {text, status} = editMetroConfig(RN_TEMPLATE_METRO);
    assert.equal(status, 'edited');
    assert.match(
      text,
      /require\('@react-native\/metro-config'\);\nconst \{withLinux\} = require\('@curiosity26\/react-native-gtk4\/metro-config'\);\n/,
    );
    assert.match(text, /module\.exports = withLinux\(mergeConfig\(getDefaultConfig\(__dirname\), config\)\);\n$/);
  });

  test('is idempotent', () => {
    const once = editMetroConfig(RN_TEMPLATE_METRO).text;
    const twice = editMetroConfig(once);
    assert.equal(twice.status, 'unchanged');
    assert.equal(twice.text, once);
  });

  test('handles a multi-line export with brackets, strings and comments', () => {
    const src = [
      "const x = require('x');",
      'module.exports = merge(base, {',
      "  resolver: {sourceExts: ['js', 'ts;']}, // not the end;",
      '  /* nor ; this */',
      '});',
      '',
    ].join('\n');
    const {text, status} = editMetroConfig(src);
    assert.equal(status, 'edited');
    assert.match(text, /module\.exports = withLinux\(merge\(base, \{[\s\S]*\}\)\);\n$/);
    assert.ok(text.indexOf('withLinux} = require') > text.indexOf("require('x')"));
  });

  test('asks for a manual edit when the export is unusual', () => {
    const src = 'module.exports = a;\nif (x) module.exports = b;\nmodule.exports = c;\n';
    const r = editMetroConfig(src);
    assert.equal(r.status, 'manual');
    assert.equal(r.text, src);
    assert.ok(r.instructions.some(l => l.includes('withLinux(')));
  });

  test('a file without requires gets the require first', () => {
    const {text} = editMetroConfig('module.exports = {};\n');
    assert.equal(
      text,
      `const {withLinux} = require('${PKG}/metro-config');\nmodule.exports = withLinux({});\n`,
    );
  });
});

describe('editPackageJson', () => {
  const base = JSON.stringify(
    {name: 'MyApp', scripts: {android: 'react-native run-android'}, dependencies: {react: '19.2.3'}},
    null,
    2,
  ) + '\n';

  test('adds the dependency and the linux script', () => {
    const {text, changes} = editPackageJson(base, {version: '^1.2.3'});
    const pkg = JSON.parse(text);
    assert.equal(pkg.dependencies[PKG], '^1.2.3');
    assert.deepEqual(Object.keys(pkg.dependencies), [PKG, 'react']);
    assert.equal(pkg.scripts.linux, 'react-native run-linux');
    assert.equal(changes.length, 2);
    assert.ok(text.endsWith('}\n'));
    assert.match(text, /^ {2}"name"/m);
  });

  test('keeps an installed version (a tarball or a range)', () => {
    const withDep = editPackageJson(base, {version: '^1.0.0'}).text.replace('^1.0.0', 'file:../pkg.tgz');
    const {text, changes} = editPackageJson(withDep, {version: '^1.0.0'});
    assert.deepEqual(changes, []);
    assert.equal(text, withDep);
  });

  test('accepts the package in devDependencies', () => {
    const src = JSON.stringify({name: 'a', devDependencies: {[PKG]: '*'}, scripts: {}});
    const pkg = JSON.parse(editPackageJson(src, {version: '^1'}).text);
    assert.equal(pkg.dependencies, undefined);
  });

  test("keeps a custom linux script unless overwriting", () => {
    const src = JSON.stringify({name: 'a', dependencies: {[PKG]: '*'}, scripts: {linux: 'custom'}});
    assert.deepEqual(editPackageJson(src, {version: '^1'}).changes, []);
    const over = editPackageJson(src, {version: '^1', overwrite: true});
    assert.equal(JSON.parse(over.text).scripts.linux, 'react-native run-linux');
  });
});

describe('names', () => {
  test('target names and application ids', () => {
    assert.equal(targetName('MyApp'), 'MyApp');
    assert.equal(targetName('@scope/my-app'), 'my-app');
    assert.equal(targetName('1st app'), 'App1stapp');
    assert.equal(defaultAppId('MyApp'), 'com.myapp');
    assert.equal(defaultAppId('my-app'), 'com.myapp');
    assert.equal(defaultAppId('42'), 'com.app42');
  });

  test('templates substitute and escape for C++ strings', () => {
    assert.equal(
      renderTemplate('t = "{{title}}"; {{unknown}}', {title: 'Say "hi"'}),
      't = "Say \\"hi\\""; {{unknown}}',
    );
  });
});

describe('initLinux', () => {
  let root;
  const quiet = () => {};

  beforeEach(() => {
    root = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-init-'));
    fs.writeFileSync(path.join(root, 'app.json'), JSON.stringify({name: 'MyApp', displayName: 'My App'}));
    fs.writeFileSync(
      path.join(root, 'package.json'),
      JSON.stringify({name: 'MyApp', scripts: {}, dependencies: {'react-native': '0.87.1'}}, null, 2) + '\n',
    );
    fs.writeFileSync(path.join(root, 'metro.config.js'), RN_TEMPLATE_METRO);
  });

  afterEach(() => fs.rmSync(root, {recursive: true, force: true}));

  test('sets up linux/, package.json and metro.config.js', () => {
    const r = initLinux(root, {log: quiet});
    assert.deepEqual(r.names, {moduleName: 'MyApp', name: 'MyApp', title: 'My App', appId: 'com.myapp'});
    const main = fs.readFileSync(path.join(root, 'linux', 'main.cc'), 'utf8');
    assert.match(main, /options\.appId = "com\.myapp";/);
    assert.match(main, /options\.title = "My App";/);
    assert.match(main, /options\.moduleName = "MyApp";/);
    assert.doesNotMatch(main, /\{\{/);
    const cmake = fs.readFileSync(path.join(root, 'linux', 'CMakeLists.txt'), 'utf8');
    assert.match(cmake, /project\(MyApp LANGUAGES CXX\)/);
    assert.match(cmake, /add_executable\(MyApp main\.cc\)/);
    assert.equal(fs.readFileSync(path.join(root, 'linux', '.gitignore'), 'utf8'), 'build/\n');
    const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
    assert.ok(pkg.dependencies[PKG]);
    assert.equal(pkg.scripts.linux, 'react-native run-linux');
    assert.equal(r.metro.status, 'edited');
    assert.ok(projectConfig(root));
  });

  test('a second run changes nothing', () => {
    initLinux(root, {log: quiet});
    const snapshot = dir =>
      fs.readdirSync(dir, {recursive: true}).sort().map(f => {
        const p = path.join(dir, f);
        return [f, fs.statSync(p).isFile() ? fs.readFileSync(p, 'utf8') : null];
      });
    const before = snapshot(root);
    const r = initLinux(root, {log: quiet});
    assert.deepEqual(r.written, []);
    assert.equal(r.skipped.length, 3);
    assert.deepEqual(r.packageChanges, []);
    assert.equal(r.metro.status, 'unchanged');
    assert.deepEqual(snapshot(root), before);
  });

  test('keeps edited files unless --overwrite', () => {
    initLinux(root, {log: quiet});
    const main = path.join(root, 'linux', 'main.cc');
    fs.writeFileSync(main, '// mine\n');
    initLinux(root, {log: quiet});
    assert.equal(fs.readFileSync(main, 'utf8'), '// mine\n');
    initLinux(root, {log: quiet, overwrite: true, appId: 'org.example.MyApp'});
    assert.match(fs.readFileSync(main, 'utf8'), /options\.appId = "org\.example\.MyApp";/);
  });

  test('creates metro.config.js when there is none', () => {
    fs.rmSync(path.join(root, 'metro.config.js'));
    initLinux(root, {log: quiet});
    assert.match(fs.readFileSync(path.join(root, 'metro.config.js'), 'utf8'), /getDefaultConfig\(__dirname\)/);
  });
});

describe('platform config', () => {
  test('no linux/ project before init-linux; dependencies contribute nothing yet', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-cfg-'));
    try {
      assert.equal(projectConfig(dir), null);
      assert.equal(dependencyConfig(dir), null);
    } finally {
      fs.rmSync(dir, {recursive: true});
    }
  });

  test('react-native.config.js has the shape the CLI validates', () => {
    const config = require('../react-native.config');
    assert.deepEqual(Object.keys(config.platforms), ['linux']);
    assert.deepEqual(Object.keys(config.platforms.linux).sort(), ['dependencyConfig', 'projectConfig']);
    assert.deepEqual(config.commands.map(c => c.name), ['init-linux', 'run-linux']);
    for (const c of config.commands) {
      assert.equal(typeof c.func, 'function');
      for (const o of c.options) assert.match(o.name, /^--[a-z-]+( <[a-z]+>)?$/);
    }
  });
});

test('appNames falls back to package.json', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-names-'));
  try {
    fs.writeFileSync(path.join(dir, 'package.json'), '{"name":"cool-app"}');
    assert.deepEqual(appNames(dir), {moduleName: 'cool-app', name: 'cool-app', title: 'cool-app', appId: 'com.coolapp'});
  } finally {
    fs.rmSync(dir, {recursive: true});
  }
});
