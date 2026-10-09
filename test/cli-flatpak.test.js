'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {describe, test} = require('node:test');

const {
  depsArchives,
  finishArgs,
  flatpakManifest,
  npmCacheContent,
  shellQuote,
  syncTree,
  thirdPartyVersions,
} = require('../lib/cli/flatpak');
const {sourceRoot, localSkips} = require('../lib/cli/flatpakBuild');
const paths = require('../lib/cli/paths');

const TOML = `
boost="1_83_0"
doubleconversion="1.1.6"
fastFloat="8.0.0"
fmt="12.1.0"
folly="2024.11.18.00"
glog="0.3.5"
gflags="2.2.0"
nlohmannjson="3.11.2"
`;

const info = {name: 'MyApp', appId: 'com.example.MyApp', version: '1.0.0', network: false, flatpak: null};

describe('flatpak manifest', () => {
  test("the archives land where fetch-rn-deps.py --offline looks", () => {
    const archives = depsArchives({reactNative: '0.87.1', hermes: 'hermes-v1', versions: thirdPartyVersions(TOML)});
    const by = Object.fromEntries(archives.map(a => [a.name, a]));
    assert.equal(by['react-native'].url, 'https://github.com/facebook/react-native/archive/refs/tags/v0.87.1.tar.gz');
    assert.equal(by['react-native'].dest, 'deps/react-native');
    assert.equal(by.hermes.dest, 'deps/hermes');
    assert.equal(by.boost.type, 'file');
    assert.equal(by.boost.url, 'https://github.com/boostorg/boost/releases/download/boost-1.83.0/boost-1.83.0.tar.gz');
    assert.equal(by.folly.dest, 'deps/downloads/folly-2024.11.18.00');
    assert.equal(by.nlohmann_json.dest, 'deps/downloads/json-3.11.2');
    assert.equal(by.fmt.url, 'https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz');
    assert.equal(archives.length, 10);
  });

  test('finish-args: a window and the GPU; the network only when asked for', () => {
    assert.deepEqual(finishArgs(info), ['--socket=wayland', '--socket=fallback-x11', '--share=ipc', '--device=dri']);
    assert.ok(finishArgs({...info, network: true}).includes('--share=network'));
    assert.ok(finishArgs({...info, flatpak: {finishArgs: ['--filesystem=xdg-music:ro']}}).includes('--filesystem=xdg-music:ro'));
  });

  test('two modules: the host into /app/rngtk (cleaned up), the app into /app', () => {
    const m = flatpakManifest(
      info,
      {hostDeps: [{type: 'archive', url: 'u', sha256: 's', dest: 'deps/react-native'}], hostPackage: {type: 'dir', path: '/p'}, hostNpm: 'codegen.json', app: {type: 'dir', path: '/a'}, appNpm: 'app.json', appDir: 'examples/my app'},
      {version: '1.2.3'},
    );
    assert.equal(m.id, 'com.example.MyApp');
    assert.equal(m.runtime, 'org.gnome.Platform');
    assert.equal(m.sdk, 'org.gnome.Sdk');
    assert.equal(m.command, 'MyApp');
    assert.deepEqual(m.cleanup, ['/rngtk']);
    assert.deepEqual(m.modules.map(x => x.name), ['react-native-gtk4', 'MyApp']);
    const [host, app] = m.modules;
    assert.match(host['build-commands'][0], /fetch-rn-deps\.py --deps deps --offline/);
    assert.ok(host['build-commands'].some(c => c.includes('-DCMAKE_INSTALL_PREFIX=/app/rngtk')));
    assert.deepEqual(host.sources.at(-2), {type: 'dir', path: '/p', dest: 'rngtk'});
    assert.equal(host.sources.at(-1), 'codegen.json');
    assert.equal(app['build-options'].env.npm_config_offline, 'true');
    assert.equal(app['build-options'].env.npm_config_cache, '/run/build/MyApp/flatpak-node/npm-cache');
    assert.match(app['build-commands'][1], /^cd 'examples\/my app' && npx react-native package-linux --no-checks --host \/app\/rngtk\/lib\/cmake\/ReactNativeGtk --prefix \/app --version 1\.2\.3$/);
    assert.deepEqual(m['sdk-extensions'], ['org.freedesktop.Sdk.Extension.node24', 'org.freedesktop.Sdk.Extension.llvm22']);
    const old = flatpakManifest({...info, flatpak: {runtimeVersion: '50', llvm: 'llvm21'}}, {hostDeps: [], hostPackage: {}, app: {}});
    assert.equal(old['runtime-version'], '50');
    assert.match(old.modules[0]['build-options']['append-path'], /llvm21/);
  });

  test('shellQuote', () => {
    assert.equal(shellQuote('examples/hello-world'), 'examples/hello-world');
    assert.equal(shellQuote("it's"), `'it'\\''s'`);
  });
});

describe('flatpak sources', () => {
  test('an app inside this repository is built from the repository', () => {
    assert.deepEqual(sourceRoot(path.join(paths.PACKAGE_DIR, 'examples', 'hello-world')), {root: paths.PACKAGE_DIR, appDir: 'examples/hello-world'});
    assert.deepEqual(sourceRoot('/somewhere/MyApp'), {root: '/somewhere/MyApp', appDir: '.'});
    const skips = localSkips(paths.PACKAGE_DIR, 'examples/hello-world');
    assert.ok(skips.includes('.git'));
    assert.ok(!skips.includes('linux'));
  });

  test('syncTree copies only what changed and drops what is gone', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-sync-'));
    try {
      const src = path.join(dir, 'src');
      const dest = path.join(dir, 'dest');
      fs.mkdirSync(path.join(src, 'linux', 'tests'), {recursive: true});
      fs.writeFileSync(path.join(src, 'linux', 'a.cc'), 'a');
      fs.writeFileSync(path.join(src, 'linux', 'tests', 't.cc'), 't');
      fs.writeFileSync(path.join(src, 'top.txt'), 'top');
      assert.deepEqual(syncTree(src, ['linux', 'top.txt'], dest, {skip: ['linux/tests']}), ['linux/a.cc', 'top.txt']);
      const before = fs.statSync(path.join(dest, 'linux', 'a.cc')).mtimeMs;
      fs.rmSync(path.join(src, 'top.txt'));
      assert.deepEqual(syncTree(src, ['linux'], dest, {skip: ['linux/tests']}), ['linux/a.cc']);
      assert.equal(fs.statSync(path.join(dest, 'linux', 'a.cc')).mtimeMs, before);
      assert.ok(!fs.existsSync(path.join(dest, 'top.txt')));
      assert.ok(!fs.existsSync(path.join(dest, 'linux', 'tests')));
    } finally {
      fs.rmSync(dir, {recursive: true, force: true});
    }
  });

  test("npmCacheContent finds a tarball in npm's cache by its sha512", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-cacache-'));
    try {
      const sha = 'ab'.repeat(64);
      const file = path.join(dir, 'content-v2', 'sha512', 'ab', 'ab', 'ab'.repeat(62));
      fs.mkdirSync(path.dirname(file), {recursive: true});
      fs.writeFileSync(file, 'tgz');
      assert.equal(npmCacheContent(sha, dir), file);
      assert.equal(npmCacheContent('cd'.repeat(64), dir), null);
    } finally {
      fs.rmSync(dir, {recursive: true, force: true});
    }
  });
});
