'use strict';

// scripts/release.js: every package on React Native's major.minor, each
// with its own patch, picked at release time from what changed since the
// package's last release tag.
const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
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
  write(root, {
    name: '@curiosity26/react-native-gtk4',
    version: versions.core,
    files: ['lib/', 'rn-version.properties', 'CHANGELOG.md', '!lib/tests/'],
    peerDependencies: {'react-native': rn},
  });
  fs.mkdirSync(path.join(root, 'lib'));
  fs.writeFileSync(path.join(root, 'lib', 'a.js'), '1\n');
  write(path.join(root, 'packages', 'svg'), {
    name: '@curiosity26/react-native-gtk4-svg',
    version: versions.svg,
    files: ['linux/', 'CHANGELOG.md'],
    peerDependencies: {'@curiosity26/react-native-gtk4': versions.peer, 'react-native-svg': '>=15.0.0'},
  });
  fs.writeFileSync(path.join(root, 'packages', 'svg', 'svg.cc'), '1\n');
  return root;
}

function repo(root) {
  const git = (...args) => execFileSync('git', args, {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
  git('init', '-q', '-b', 'main');
  git('config', 'user.email', 't@example.com');
  git('config', 'user.name', 't');
  git('add', '-A');
  git('commit', '-qm', 'initial');
  // A pull request: a branch off main changing a file, merged with a merge commit.
  const pr = (n, title, file) => {
    git('checkout', '-qb', `pr${n}`, 'main');
    fs.appendFileSync(path.join(root, file), `${n}\n`);
    git('commit', '-qam', `wip ${n}`);
    git('checkout', '-q', 'main');
    git('merge', '-q', '--no-ff', `pr${n}`, '-m', `Merge pull request #${n} from x/pr${n}\n\n${title}`);
  };
  return {git, pr};
}

const read = (root, ...p) => JSON.parse(fs.readFileSync(path.join(root, ...p, 'package.json'), 'utf8'));
const quiet = fn => {
  const log = console.log;
  console.log = () => {};
  try {
    return fn();
  } finally {
    console.log = log;
  }
};

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
    assert.equal(out.length, 2);
    assert.match(out[0], /react-native-gtk4 is 0\.87\.0: it should be 0\.83\.<patch>/);
    assert.match(out[1], /wants @curiosity26\/react-native-gtk4 >=0\.1\.0: it should be 0\.83\.x/);
  });

  test('a new React Native minor starts every package at .0', () => {
    const root = fixture('0.84.0', {core: '0.83.4', svg: '0.83.7', peer: '0.83.x'});
    quiet(() => release.moveToLine(root));
    assert.equal(read(root).version, '0.84.0');
    assert.equal(read(root, 'packages', 'svg').version, '0.84.0');
    assert.equal(read(root, 'packages', 'svg').peerDependencies['@curiosity26/react-native-gtk4'], '0.84.x');
    assert.deepEqual(release.problems(root), []);
  });

  test('main publishes as latest, a release line as vX.Y-stable', () => {
    assert.deepEqual(release.distTags('main', '0.87'), ['latest', 'v0.87-stable']);
    assert.deepEqual(release.distTags('release/0.83', '0.83'), ['v0.83-stable']);
    assert.equal(release.distTags('claude/some-branch', '0.87'), null);
    assert.throws(() => release.distTags('release/0.84', '0.83'), /on React Native 0\.83/);
  });

  test('tags: the package v<version>, a port <name>@<version>; what each ships', () => {
    const root = fixture('0.87.1', {core: '0.87.2', svg: '0.87.5', peer: '0.87.x'});
    const [core, svg] = release.packages(root);
    assert.equal(release.tagPrefix(core), 'v');
    assert.equal(release.tagPrefix(svg), 'react-native-gtk4-svg@');
    assert.deepEqual(release.pathspecs(core, root), ['package.json', 'lib', 'rn-version.properties']);
    assert.deepEqual(release.pathspecs(svg, root), ['packages/svg']);
  });

  test('the first release on a line is the version as it stands', () => {
    const root = fixture('0.87.1', {core: '0.87.0', svg: '0.87.0', peer: '0.87.x'});
    repo(root);
    const [core, svg] = release.packages(root);
    assert.deepEqual(release.next(core, '0.87', root), {version: '0.87.0', last: null, changes: []});
    assert.equal(release.next(svg, '0.87', root).version, '0.87.0');
  });

  test('two pull requests to one port before a release: one patch with both', () => {
    const root = fixture('0.87.1', {core: '0.87.0', svg: '0.87.0', peer: '0.87.x'});
    const {git, pr} = repo(root);
    git('tag', 'v0.87.0');
    git('tag', 'react-native-gtk4-svg@0.87.0');
    pr(1, 'svg: fix arcs', 'packages/svg/svg.cc');
    pr(2, 'svg: fix gradients', 'packages/svg/svg.cc');
    const [core, svg] = release.packages(root);
    assert.deepEqual(release.next(svg, '0.87', root), {
      version: '0.87.1',
      last: 'react-native-gtk4-svg@0.87.0',
      changes: ['svg: fix arcs (#1)', 'svg: fix gradients (#2)'],
    });
    assert.equal(release.next(core, '0.87', root).version, null); // the package didn't change
  });

  test('a release between two pull requests: the second gets the next patch', () => {
    const root = fixture('0.87.1', {core: '0.87.0', svg: '0.87.0', peer: '0.87.x'});
    const {git, pr} = repo(root);
    git('tag', 'react-native-gtk4-svg@0.87.0');
    pr(1, 'svg: fix arcs', 'packages/svg/svg.cc');
    // The release: its commit (version and changelog), then its tag.
    const svgDir = path.join(root, 'packages', 'svg');
    let [, svg] = release.packages(root);
    const n = release.next(svg, '0.87', root);
    svg.pkg.version = n.version;
    fs.writeFileSync(svg.file, JSON.stringify(svg.pkg, null, 2) + '\n');
    release.writeChangelog(svgDir, n.version, n.changes, '2026-10-11');
    git('commit', '-qam', 'Release: svg 0.87.1');
    git('tag', 'react-native-gtk4-svg@0.87.1');
    assert.match(fs.readFileSync(path.join(svgDir, 'CHANGELOG.md'), 'utf8'), /## 0\.87\.1 \(2026-10-11\)\n\n- svg: fix arcs \(#1\)\n\n## 0\.87\.0/);
    [, svg] = release.packages(root);
    assert.equal(release.next(svg, '0.87', root).version, null);
    pr(2, 'svg: fix gradients', 'packages/svg/svg.cc');
    assert.deepEqual(release.next(svg, '0.87', root).changes, ['svg: fix gradients (#2)']);
    assert.equal(release.next(svg, '0.87', root).version, '0.87.2');
  });

  test("a release commit that wasn't tagged (publishing stopped) goes out again", () => {
    const root = fixture('0.87.1', {core: '0.87.0', svg: '0.87.0', peer: '0.87.x'});
    const {git} = repo(root);
    git('tag', 'react-native-gtk4-svg@0.87.0');
    const [, svg] = release.packages(root);
    svg.pkg.version = '0.87.1';
    fs.writeFileSync(svg.file, JSON.stringify(svg.pkg, null, 2) + '\n');
    git('commit', '-qam', 'Release: svg 0.87.1');
    assert.equal(release.next(release.packages(root)[1], '0.87', root).version, '0.87.1');
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

  test("npm gets GitHub Packages' versions, oldest first, under their lines' tags", () => {
    const gh = {versions: ['0.83.0', '0.87.0', '0.83.1', '0.87.1'], tags: {latest: '0.87.1', 'v0.87-stable': '0.87.1', 'v0.83-stable': '0.83.1'}};
    // A new package: the copies set every tag but latest (the first one).
    assert.deepEqual(release.npmPlan(gh, {versions: [], tags: {}}), {
      copy: [
        {version: '0.83.0', tag: 'v0.83-stable'},
        {version: '0.83.1', tag: 'v0.83-stable'},
        {version: '0.87.0', tag: 'v0.87-stable'},
        {version: '0.87.1', tag: 'v0.87-stable'},
      ],
      tags: [{tag: 'latest', version: '0.87.1'}],
    });
    // Up to date: nothing.
    assert.deepEqual(release.npmPlan(gh, gh), {copy: [], tags: []});
    // An older version copied late moves its tag back, so it's set again.
    const npm = {versions: ['0.83.1', '0.87.0', '0.87.1'], tags: {latest: '0.87.1', 'v0.87-stable': '0.87.1', 'v0.83-stable': '0.83.1'}};
    assert.deepEqual(release.npmPlan(gh, npm), {
      copy: [{version: '0.83.0', tag: 'v0.83-stable'}],
      tags: [{tag: 'v0.83-stable', version: '0.83.1'}],
    });
  });

  test("a registry's listing: one version, or a 404 as none", () => {
    assert.deepEqual(release.listing('x', 'r', () => '{"versions": "0.87.0", "dist-tags": {"latest": "0.87.0"}}'), {
      versions: ['0.87.0'],
      tags: {latest: '0.87.0'},
    });
    const e404 = () => {
      throw Object.assign(new Error('npm'), {stderr: 'npm error code E404', stdout: ''});
    };
    assert.deepEqual(release.listing('x', 'r', e404), {versions: [], tags: {}});
  });
});
