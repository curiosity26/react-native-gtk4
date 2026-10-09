'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {afterEach, beforeEach, describe, test} = require('node:test');

const {
  autolinkingCmake,
  autolinkingSource,
  cIdentifier,
  dependencyConfig,
  linuxLibraries,
  writeAutolinking,
} = require('../lib/cli/autolinking');
const {initLinuxLibrary, libraryNames, rename} = require('../lib/cli/initLinuxLibrary');
const {warnIfNotRegistered} = require('../lib/cli/runLinux');

let dir;
beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-autolink-'));
});
afterEach(() => fs.rmSync(dir, {recursive: true, force: true}));

function library(name, {linux = true, config} = {}) {
  const root = path.join(dir, 'node_modules', name);
  fs.mkdirSync(root, {recursive: true});
  fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({name}));
  if (linux) {
    fs.mkdirSync(path.join(root, 'linux'));
    fs.writeFileSync(path.join(root, 'linux', 'CMakeLists.txt'), '');
  }
  return {root, linux: dependencyConfig(root, config)};
}

describe('dependencyConfig', () => {
  test('a dependency with linux/CMakeLists.txt: target and function from its name', () => {
    const {root, linux} = library('@acme/react-native-foo');
    assert.deepEqual(linux, {
      sourceDir: path.join(root, 'linux'),
      packageName: '@acme/react-native-foo',
      cmakeTarget: 'acme_react_native_foo',
      packageFunction: 'acme_react_native_foo_package',
    });
  });

  test('its react-native.config.js can name them', () => {
    const {linux} = library('react-native-bar', {config: {cmakeTarget: 'bar', packageFunction: 'bar_package'}});
    assert.equal(linux.cmakeTarget, 'bar');
    assert.equal(linux.packageFunction, 'bar_package');
  });

  test('without linux/: nothing', () => {
    assert.equal(library('plain-js', {linux: false}).linux, null);
  });

  test('C identifiers', () => {
    assert.equal(cIdentifier('react-native-gtk.calendar'), 'react_native_gtk_calendar');
    assert.equal(cIdentifier('3d-thing'), 'lib_3d_thing');
  });
});

describe('generated files', () => {
  test('the CLI config dependencies with Linux code, by name', () => {
    const deps = {
      b: {platforms: {linux: library('react-native-b').linux}},
      a: {platforms: {linux: library('react-native-a').linux, ios: {}}},
      js: {platforms: {linux: null}},
    };
    assert.deepEqual(linuxLibraries(deps).map(l => l.packageName), ['react-native-a', 'react-native-b']);
  });

  test('autolinking.cmake adds each linux/ folder and lists its target', () => {
    const lib = library('react-native-a').linux;
    const text = autolinkingCmake([lib]);
    assert.match(text, /if\(NOT TARGET react_native_a\)/);
    assert.ok(text.includes(`add_subdirectory("${lib.sourceDir}" "\${CMAKE_BINARY_DIR}/autolinked/react_native_a")`));
    assert.match(text, /list\(APPEND RNGTK_AUTOLINKED_TARGETS react_native_a\)/);
  });

  test('autolinking.cc returns each package', () => {
    const text = autolinkingSource([library('react-native-a').linux]);
    assert.match(text, /std::shared_ptr<const rngtk::Package> react_native_a_package\(\);/);
    assert.match(text, /PackageList autolinkedPackages\(\) \{\n  return \{\n      ::react_native_a_package\(\),\n  \};/);
    assert.match(autolinkingSource([]), /return \{\n  \};/);
  });

  test('written only when they change (builds stay incremental)', () => {
    const out = path.join(dir, 'autolinking');
    writeAutolinking(out, []);
    const file = path.join(out, 'autolinking.cc');
    const before = fs.statSync(file).mtimeMs;
    fs.utimesSync(file, new Date(0), new Date(0));
    writeAutolinking(out, []);
    assert.equal(fs.statSync(file).mtimeMs, 0);
    assert.notEqual(before, 0);
  });
});

describe('init-linux-library', () => {
  test('names from package.json or --name', () => {
    fs.writeFileSync(path.join(dir, 'package.json'), '{"name":"react-native-gtk-calendar"}');
    assert.deepEqual(libraryNames(dir), {
      pascal: 'GtkCalendar',
      snake: 'gtk_calendar',
      target: 'gtk_calendar',
      packageFunction: 'gtk_calendar_package',
      component: 'GtkCalendarView',
      packageName: 'react-native-gtk-calendar',
    });
    assert.equal(libraryNames(dir, {name: 'SuperWidget'}).snake, 'super_widget');
    assert.equal(rename('RNGtkExampleView ExampleModule example_package', libraryNames(dir)),
                 'GtkCalendarView GtkCalendarModule gtk_calendar_package');
  });

  test('writes linux/, the JS sides and react-native.config.js; autolinking finds it', () => {
    fs.writeFileSync(path.join(dir, 'package.json'), '{"name":"react-native-gtk-calendar"}');
    const {written, config} = initLinuxLibrary(dir, {log: () => {}});
    const rel = written.map(f => path.relative(dir, f)).sort();
    assert.deepEqual(rel, [
      'linux/CMakeLists.txt',
      'linux/src/GtkCalendarModule.cc',
      'linux/src/GtkCalendarModule.h',
      'linux/src/GtkCalendarPackage.cc',
      'linux/src/GtkCalendarView.cc',
      'linux/src/GtkCalendarView.h',
      'src/GtkCalendarViewNativeComponent.ts',
      'src/NativeGtkCalendar.ts',
      'src/index.ts',
    ]);
    assert.equal(config, 'created');
    const cmake = fs.readFileSync(path.join(dir, 'linux', 'CMakeLists.txt'), 'utf8');
    assert.match(cmake, /add_library\(gtk_calendar STATIC/);
    assert.match(fs.readFileSync(path.join(dir, 'linux/src/GtkCalendarPackage.cc'), 'utf8'),
                 /std::shared_ptr<const rngtk::Package> gtk_calendar_package\(\)/);
    assert.match(fs.readFileSync(path.join(dir, 'src/GtkCalendarViewNativeComponent.ts'), 'utf8'),
                 /codegenNativeComponent<NativeProps>\('GtkCalendarView'\)/);
    // What the CLI would read for an app depending on it.
    const userConfig = require(path.join(dir, 'react-native.config.js')).dependency.platforms.linux;
    const linux = dependencyConfig(dir, userConfig);
    assert.equal(linux.cmakeTarget, 'gtk_calendar');
    assert.equal(linux.packageFunction, 'gtk_calendar_package');
  });

  test('keeps existing files and an existing index; asks to edit an existing config', () => {
    fs.writeFileSync(path.join(dir, 'package.json'), '{"name":"cal"}');
    fs.mkdirSync(path.join(dir, 'src'));
    fs.writeFileSync(path.join(dir, 'src', 'index.js'), '// mine');
    fs.writeFileSync(path.join(dir, 'react-native.config.js'), 'module.exports = {};');
    const lines = [];
    const {written, config} = initLinuxLibrary(dir, {log: l => lines.push(l)});
    assert.ok(!written.some(f => f.endsWith('index.ts')));
    assert.equal(config, 'manual');
    assert.ok(lines.some(l => l.includes("linux: {cmakeTarget: 'cal', packageFunction: 'cal_package'}")));
    const again = initLinuxLibrary(dir, {log: () => {}});
    assert.deepEqual(again.written, []);
  });
});

test("run-linux warns when an older app's main.cc doesn't register the libraries", t => {
  const warn = t.mock.method(console, 'warn', () => {});
  fs.mkdirSync(path.join(dir, 'linux'));
  fs.writeFileSync(path.join(dir, 'linux', 'main.cc'), 'int main() {}');
  assert.equal(warnIfNotRegistered(dir), true);
  assert.match(warn.mock.calls[0].arguments[0], /options\.packages = rngtk::autolinkedPackages\(\);/);
  const template = fs.readFileSync(path.join(__dirname, '..', 'template', 'linux', 'main.cc'), 'utf8');
  fs.writeFileSync(path.join(dir, 'linux', 'main.cc'), template);
  assert.equal(warnIfNotRegistered(dir), false);
});
