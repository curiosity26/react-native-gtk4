/**
 * package-linux --format flatpak: a flatpak-builder manifest that builds
 * everything from source inside the GNOME SDK, with no network during the
 * build (as Flathub requires), and a .flatpak bundle built from it.
 *
 * Two modules:
 *   react-native-gtk4   React Native's C++ sources, its third-party
 *                       libraries and Hermes (archives at the versions
 *                       React Native pins), React Native's codegen (npm,
 *                       offline) and the host library, installed into
 *                       /app/rngtk (removed from the finished app)
 *   <app>               the app's sources and its node_modules (offline
 *                       npm sources from flatpak-node-generator); runs
 *                       package-linux inside the sandbox against
 *                       /app/rngtk and installs into /app
 *
 * The sources are archives with checksums (a Flathub-style manifest), or
 * with --local the project's directories themselves, and flatpak-builder's
 * state (downloads, module cache, ccache) is shared between runs in the
 * package's cache, so a rebuild after an app change only rebuilds the app.
 */
'use strict';

const crypto = require('crypto');
const {spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');
const paths = require('./paths');

// The runtime and SDK extensions (Flathub, October 2026): GNOME 51 is on
// freedesktop.org's 26.08 SDK, which the extensions' branches follow.
// app.json's linux.flatpak can override each.
const DEFAULTS = {
  runtimeVersion: '51',
  sdkBranch: '26.08',
  node: 'node24',
  llvm: 'llvm22',
};

// The flatpak-builder state directory, shared by every app's builds.
function flatpakCacheDir(env = process.env) {
  return path.join(paths.cacheRoot(env), 'flatpak');
}

function sha256File(file) {
  const hash = crypto.createHash('sha256');
  const fd = fs.openSync(file, 'r');
  const buf = Buffer.alloc(1 << 20);
  let n;
  while ((n = fs.readSync(fd, buf, 0, buf.length, null)) > 0) hash.update(buf.subarray(0, n));
  fs.closeSync(fd);
  return hash.digest('hex');
}

/** React Native's pinned third-party versions (gradle/libs.versions.toml). */
function thirdPartyVersions(toml) {
  const v = name => {
    const m = toml.match(new RegExp(`^${name}\\s*=\\s*"([^"]+)"`, 'm'));
    if (!m) throw new Error(`libs.versions.toml has no ${name}`);
    return m[1];
  };
  return {
    boost: v('boost'),
    doubleConversion: v('doubleconversion'),
    fastFloat: v('fastFloat'),
    fmt: v('fmt'),
    folly: v('folly'),
    glog: v('glog'),
    gflags: v('gflags'),
    nlohmannJson: v('nlohmannjson'),
  };
}

/**
 * What scripts/fetch-rn-deps.py fetches, as archives at the paths it looks
 * for them (dest is relative to the module's build directory; `deps` is its
 * --deps). GitHub's tag archives' top directory is the one the script's
 * checkouts have, and flatpak-builder strips it.
 */
function depsArchives({reactNative, hermes, versions: t}) {
  const gh = (repo, tag) => `https://github.com/${repo}/archive/refs/tags/${tag}.tar.gz`;
  const dotted = t.boost.replace(/_/g, '.');
  return [
    {name: 'react-native', url: gh('facebook/react-native', `v${reactNative}`), dest: 'deps/react-native'},
    {name: 'hermes', url: gh('facebook/hermes', hermes), dest: 'deps/hermes'},
    {
      name: 'boost',
      type: 'file',
      url: `https://github.com/boostorg/boost/releases/download/boost-${dotted}/boost-${dotted}.tar.gz`,
      dest: 'deps/downloads',
    },
    {name: 'double-conversion', url: gh('google/double-conversion', `v${t.doubleConversion}`), dest: `deps/downloads/double-conversion-${t.doubleConversion}`},
    {name: 'fast_float', url: gh('fastfloat/fast_float', `v${t.fastFloat}`), dest: `deps/downloads/fast_float-${t.fastFloat}`},
    {name: 'fmt', url: gh('fmtlib/fmt', t.fmt), dest: `deps/downloads/fmt-${t.fmt}`},
    {name: 'glog', url: gh('google/glog', `v${t.glog}`), dest: `deps/downloads/glog-${t.glog}`},
    {name: 'folly', url: gh('facebook/folly', `v${t.folly}`), dest: `deps/downloads/folly-${t.folly}`},
    {name: 'nlohmann_json', url: gh('nlohmann/json', `v${t.nlohmannJson}`), dest: `deps/downloads/json-${t.nlohmannJson}`},
    {name: 'gflags', url: gh('gflags/gflags', `v${t.gflags}`), dest: `deps/downloads/gflags-${t.gflags}`},
  ];
}

/**
 * Downloads each archive once into `dir` (the host has network; the build
 * doesn't) and returns flatpak-builder sources with their sha256.
 */
function checksummedSources(archives, dir, {run}) {
  fs.mkdirSync(dir, {recursive: true});
  return archives.map(a => {
    const file = path.join(dir, `${a.name}-${path.basename(new URL(a.url).pathname)}`);
    if (!fs.existsSync(file)) {
      console.log(`downloading ${a.url}`);
      run('curl', ['-fsSL', '--retry', '3', '-o', `${file}.part`, a.url]);
      fs.renameSync(`${file}.part`, file);
    }
    const source = {type: a.type || 'archive', url: a.url, sha256: sha256File(file), dest: a.dest};
    if (a.type === 'file') source['dest-filename'] = path.basename(new URL(a.url).pathname);
    return source;
  });
}

/** The files of the package the host module builds from. */
const HOST_PACKAGE_FILES = [
  'linux',
  'rn-version.properties',
  'LICENSE',
  'scripts/fetch-rn-deps.py',
  'scripts/build-hermes.sh',
  'scripts/codegen-deps',
];
// As the npm package leaves them out (package.json's files).
const HOST_PACKAGE_SKIP = ['linux/build', 'linux/tests', 'linux/src/main.cc'];

/**
 * Copies the files the host build reads into `dest` (rewritten only when
 * they change, so flatpak-builder's cache of the host module holds).
 */
function syncTree(srcRoot, entries, dest, {skip = []} = {}) {
  const wanted = new Set();
  const copy = (rel) => {
    const src = path.join(srcRoot, rel);
    const stat = fs.statSync(src);
    if (stat.isDirectory()) {
      for (const e of fs.readdirSync(src)) {
        const child = path.join(rel, e);
        if (!skip.includes(child)) copy(child);
      }
      return;
    }
    wanted.add(rel);
    const out = path.join(dest, rel);
    const data = fs.readFileSync(src);
    let old = null;
    try {
      old = fs.readFileSync(out);
    } catch {}
    if (!old || !old.equals(data)) {
      fs.mkdirSync(path.dirname(out), {recursive: true});
      fs.writeFileSync(out, data, {mode: stat.mode});
    }
  };
  entries.forEach(copy);
  // Drop files that are gone from the source.
  if (fs.existsSync(dest)) {
    for (const f of fs.readdirSync(dest, {recursive: true})) {
      const p = path.join(dest, f);
      if (fs.statSync(p).isFile() && !wanted.has(f)) fs.rmSync(p);
    }
  }
  return [...wanted].sort();
}

/** A reproducible tarball of `files` (relative to `root`). */
function tarball(root, files, out, {run}) {
  const list = `${out}.files`;
  fs.writeFileSync(list, files.join('\n') + '\n');
  run('tar', [
    '--sort=name', '--mtime=@0', '--owner=0', '--group=0', '--numeric-owner',
    '-czf', out, '-C', root, '-T', list,
  ]);
  fs.rmSync(list);
  return out;
}

/**
 * The app's source files: git's tracked and untracked-but-not-ignored ones
 * when it's a git checkout, else everything but node_modules, build trees
 * and .git.
 */
function sourceFiles(root) {
  const r = spawnSync('git', ['ls-files', '-co', '--exclude-standard', '-z'], {cwd: root, encoding: 'utf8'});
  if (r.status === 0) {
    return r.stdout.split('\0').filter(f => f && fs.existsSync(path.join(root, f)) && fs.statSync(path.join(root, f)).isFile());
  }
  const out = [];
  const walk = rel => {
    for (const e of fs.readdirSync(path.join(root, rel), {withFileTypes: true})) {
      const child = rel ? `${rel}/${e.name}` : e.name;
      if (['node_modules', '.git', 'build'].includes(e.name)) continue;
      if (e.isDirectory()) walk(child);
      else if (e.isFile()) out.push(child);
    }
  };
  walk('');
  return out.sort();
}

/** finish-args: a window, GPU, and the network if the app asks for it. */
function finishArgs(info) {
  const args = ['--socket=wayland', '--socket=fallback-x11', '--share=ipc', '--device=dri'];
  if (info.network) args.push('--share=network');
  // Files, notifications and URLs go through portals: no filesystem,
  // notification or session-bus permissions.
  return args.concat(info.flatpak?.finishArgs || []);
}

// Node and clang from the SDK extensions; npm from the cache the npm
// sources fill (flatpak-node-generator's layout).
function buildOptions(moduleName, f) {
  const node = `/run/build/${moduleName}/flatpak-node`;
  return {
    'append-path': `/usr/lib/sdk/${f.node}/bin:/usr/lib/sdk/${f.llvm}/bin`,
    'prepend-ld-library-path': `/usr/lib/sdk/${f.llvm}/lib`,
    env: {
      CC: 'clang',
      CXX: 'clang++',
      npm_config_cache: `${node}/npm-cache`,
      npm_config_offline: 'true',
      npm_config_nodedir: `/usr/lib/sdk/${f.node}`,
      XDG_CACHE_HOME: `${node}/cache`,
    },
  };
}

/**
 * The manifest. `sources` has hostDeps (the archives), hostPackage,
 * hostNpm (the codegen's npm sources file), app, appNpm, and appDir (the
 * app's directory inside the app source; '.' when it's the project).
 */
function flatpakManifest(info, sources, options = {}) {
  const f = {...DEFAULTS, ...(info.flatpak || {})};
  const appDir = sources.appDir || '.';
  const version = options.version || info.version;
  return {
    id: info.appId,
    runtime: 'org.gnome.Platform',
    'runtime-version': f.runtimeVersion,
    sdk: 'org.gnome.Sdk',
    'sdk-extensions': [`org.freedesktop.Sdk.Extension.${f.node}`, `org.freedesktop.Sdk.Extension.${f.llvm}`],
    command: info.name,
    'finish-args': finishArgs(info),
    // The host library's headers, CMake config and its own copies of the
    // libraries (the app has its own in lib/<app>/).
    cleanup: ['/rngtk'],
    modules: [
      {
        name: 'react-native-gtk4',
        buildsystem: 'simple',
        'build-options': buildOptions('react-native-gtk4', f),
        'build-commands': [
          'python3 rngtk/scripts/fetch-rn-deps.py --deps deps --offline',
          'rngtk/scripts/build-hermes.sh deps',
          `cmake -S rngtk/linux -B build -G Ninja ${paths.hostBuildTypeArgs(true).join(' ')}` +
            ' -DRNGTK_DEPS_DIR="$PWD/deps" -DCMAKE_INSTALL_PREFIX=/app/rngtk -DRNGTK_BUILD_HARNESS=OFF',
          'cmake --build build --target rngtk_host',
          'cmake --install build',
        ],
        sources: [
          ...sources.hostDeps,
          {...sources.hostPackage, dest: 'rngtk'},
          // npm's cache for the codegen's packages (scripts/codegen-deps).
          sources.hostNpm,
        ],
      },
      {
        name: info.name,
        buildsystem: 'simple',
        'build-options': buildOptions(info.name, f),
        'build-commands': [
          `cd ${shellQuote(appDir)} && npm ci --offline --no-audit --no-fund --ignore-scripts`,
          `cd ${shellQuote(appDir)} && npx react-native package-linux --no-checks` +
            ` --host /app/rngtk/lib/cmake/ReactNativeGtk --prefix /app --version ${shellQuote(version)}`,
        ],
        sources: [sources.app, sources.appNpm],
      },
    ],
  };
}

function shellQuote(s) {
  return /^[A-Za-z0-9_./:=+-]+$/.test(s) ? s : `'${String(s).replace(/'/g, `'\\''`)}'`;
}

/**
 * Runs flatpak-node-generator on a package-lock.json. It's a Python tool
 * from flatpak-builder-tools; $FLATPAK_NODE_GENERATOR names it if it isn't
 * on PATH.
 */
function nodeGenerator(env = process.env) {
  const candidates = [
    env.FLATPAK_NODE_GENERATOR,
    'flatpak-node-generator',
    path.join(require('os').homedir(), '.local', 'opt', 'flatpak-node-generator', 'bin', 'flatpak-node-generator'),
  ].filter(Boolean);
  for (const c of candidates) {
    const r = spawnSync(c, ['--help'], {stdio: 'ignore'});
    if (r.status === 0) return c;
  }
  return null;
}

/**
 * npm sources for a lock file, generated once per lock content (into
 * `out`). Packages on GitHub Packages (npm.pkg.github.com, which needs a
 * token flatpak-builder can't send) come from npm's own cache instead.
 */
function npmSources(lockFile, out, {run, generator, filesDir}) {
  const lock = fs.readFileSync(lockFile);
  const stamp = `${out}.sha256`;
  const digest = crypto.createHash('sha256').update(lock).digest('hex');
  if (!(fs.existsSync(out) && fs.existsSync(stamp) && fs.readFileSync(stamp, 'utf8') === digest)) {
    run(generator, ['npm', lockFile, '-o', out, '--no-requests-cache']);
    fs.writeFileSync(stamp, digest);
  }
  const sources = JSON.parse(fs.readFileSync(out, 'utf8'));
  let changed = false;
  for (const s of sources) {
    if (s.type !== 'file' || !s.url || !s.url.startsWith('https://npm.pkg.github.com/')) continue;
    const cached = npmCacheContent(s.sha512);
    if (!cached) {
      throw new Error(
        `${s.url} is on GitHub Packages, which flatpak-builder can't download from; ` +
          'run npm install (so npm caches it) and package again',
      );
    }
    fs.mkdirSync(filesDir, {recursive: true});
    const name = path.basename(new URL(s.url).pathname);
    fs.copyFileSync(cached, path.join(filesDir, name));
    delete s.url;
    s.path = path.relative(path.dirname(out), path.join(filesDir, name));
    changed = true;
  }
  if (changed) fs.writeFileSync(out, JSON.stringify(sources, null, 2) + '\n');
  return path.basename(out);
}

/** A tarball in npm's cache (~/.npm/_cacache) by its sha512 (hex). */
function npmCacheContent(sha512, cacheDir = path.join(require('os').homedir(), '.npm', '_cacache')) {
  if (!sha512) return null;
  const file = path.join(cacheDir, 'content-v2', 'sha512', sha512.slice(0, 2), sha512.slice(2, 4), sha512.slice(4));
  return fs.existsSync(file) ? file : null;
}

module.exports = {
  DEFAULTS,
  flatpakCacheDir,
  sha256File,
  thirdPartyVersions,
  depsArchives,
  checksummedSources,
  HOST_PACKAGE_FILES,
  HOST_PACKAGE_SKIP,
  syncTree,
  tarball,
  sourceFiles,
  finishArgs,
  flatpakManifest,
  nodeGenerator,
  npmSources,
  npmCacheContent,
  shellQuote,
};
