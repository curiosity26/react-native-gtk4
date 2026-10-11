'use strict';

// scripts/release.js: every package on React Native's major.minor, each
// with its own patch.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {describe, test} = require('node:test');

const release = require('../scripts/release');

function fixture(rn, versions) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-release-'));
  fs.writeFileSync(path.join(root, 'rn-version.properties'), `reactNative=${rn}\nhermes=x\n`);
  const write = (dir, pkg) => {
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify(pkg, null, 2) + '\n');
    fs.writeFileSync(path.join(dir, 'CHANGELOG.md'), `# Changelog\n\n## ${pkg.version} (unreleased)\n\n- First.\n`);
  };
  write(root, {name: '@curiosity26/react-native-gtk4', version: versions.core, peerDependencies: {'react-native': rn}});
  write(path.join(root, 'packages', 'svg'), {
    name: '@curiosity26/react-native-gtk4-svg',
    version: versions.svg,
    files: ['linux/', 'CHANGELOG.md'],
    peerDependencies: {'@curiosity26/react-native-gtk4': versions.peer, 'react-native-svg': '>=15.0.0'},
  });
  return root;
}

const read = (root, ...p) => JSON.parse(fs.readFileSync(path.join(root, ...p, 'package.json'), 'utf8'));

describe('release versions', () => {
  test('this repository follows the rule', () => {
    assert.deepEqual(release.problems(), []);
  });

  test('are React Native major.minor with a patch of their own', () => {
    const root = fixture('0.83.10', {core: '0.83.2', svg: '0.83.5', peer: '0.83.x'});
    assert.equal(release.line(root), '0.83');
    assert.deepEqual(release.problems(root), []);
  });

  test('flag another line or a loose peer range', () => {
    const root = fixture('0.83.10', {core: '0.87.0', svg: '0.83.0', peer: '>=0.1.0'});
    const out = release.problems(root);
    assert.equal(out.length, 2); // core's version, svg's peer
    assert.match(out[0], /react-native-gtk4 is 0\.87\.0: it should be 0\.83\.<patch>/);
    assert.match(out[1], /wants @curiosity26\/react-native-gtk4 >=0\.1\.0: it should be 0\.83\.x/);
  });

  test('a new React Native minor starts every package at .0', () => {
    const root = fixture('0.84.0', {core: '0.83.4', svg: '0.83.7', peer: '0.83.x'});
    const log = console.log;
    console.log = () => {};
    try {
      release.moveToLine(root);
    } finally {
      console.log = log;
    }
    assert.equal(read(root).version, '0.84.0');
    assert.equal(read(root, 'packages', 'svg').version, '0.84.0');
    assert.equal(read(root, 'packages', 'svg').peerDependencies['@curiosity26/react-native-gtk4'], '0.84.x');
    assert.match(fs.readFileSync(path.join(root, 'packages', 'svg', 'CHANGELOG.md'), 'utf8'), /## 0\.84\.0 \(unreleased\)[^]*## 0\.83\.7/);
    assert.deepEqual(release.problems(root), []);
  });

  test('a patch bump moves one package alone', () => {
    const root = fixture('0.87.1', {core: '0.87.0', svg: '0.87.0', peer: '0.87.x'});
    const log = console.log;
    console.log = () => {};
    try {
      release.bump(['svg'], root);
      release.moveToLine(root); // same line: keeps the patches
    } finally {
      console.log = log;
    }
    assert.equal(read(root).version, '0.87.0');
    assert.equal(read(root, 'packages', 'svg').version, '0.87.1');
    assert.deepEqual(release.problems(root), []);
  });

  test('main publishes as latest, a release line as vX.Y-stable', () => {
    assert.deepEqual(release.distTags('main', '0.87'), ['latest', 'v0.87-stable']);
    assert.deepEqual(release.distTags('release/0.83', '0.83'), ['v0.83-stable']);
    assert.equal(release.distTags('claude/some-branch', '0.87'), null);
    assert.throws(() => release.distTags('release/0.84', '0.83'), /on React Native 0\.83/);
  });

  test('tags: the package v<version>, a port <name>@<version>', () => {
    const [core, svg] = release.packages(fixture('0.87.1', {core: '0.87.2', svg: '0.87.5', peer: '0.87.x'}));
    assert.equal(release.gitTag(core), 'v0.87.2');
    assert.equal(release.gitTag(svg), 'react-native-gtk4-svg@0.87.5');
  });

  test('a version the registry 404s is unpublished; other errors stop', () => {
    const err = stderr => () => {
      throw Object.assign(new Error('npm'), {stderr, stdout: ''});
    };
    assert.equal(release.published('x', '1.0.0', () => '"1.0.0"\n'), true);
    assert.equal(release.published('x', '1.0.0', () => ''), false);
    assert.equal(release.published('x', '1.0.0', err('npm error code E404')), false);
    assert.throws(() => release.published('x', '1.0.0', err('npm error code E401')), /E401/);
  });
});
