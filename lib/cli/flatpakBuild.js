/**
 * package-linux --format flatpak: writes the manifest and its sources into
 * <output>/flatpak/, then builds a .flatpak bundle with flatpak-builder
 * (when it's installed; --manifest-only stops after the manifest).
 *
 *   <output>/flatpak/<appId>.json          the manifest
 *   <output>/flatpak/codegen-npm-sources.json, app-npm-sources.json
 *   <output>/flatpak/sources/              the app's and the host package's
 *                                          tarballs (not with --local)
 *   <output>/<app>-<version>-<arch>.flatpak
 *
 * flatpak-builder's state (downloads, cached modules, ccache) lives in
 * <cache>/flatpak/ for every app, so the host module (React Native, Hermes
 * and the host library: the long part) builds once per package version.
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const paths = require('./paths');
const {RunError, run, step, which, ensureDepsSources} = require('./runLinux');
const {
  flatpakCacheDir,
  thirdPartyVersions,
  depsArchives,
  checksummedSources,
  HOST_PACKAGE_FILES,
  HOST_PACKAGE_SKIP,
  syncTree,
  tarball,
  sourceFiles,
  flatpakManifest,
  nodeGenerator,
  npmSources,
  sha256File,
} = require('./flatpak');

function machineArch(arch = process.arch) {
  return {arm64: 'aarch64', x64: 'x86_64'}[arch] || arch;
}

/** The app's source tree: the repository for its own examples. */
function sourceRoot(projectRoot) {
  const rel = path.relative(paths.PACKAGE_DIR, projectRoot);
  if (rel && !rel.startsWith('..') && !path.isAbsolute(rel)) {
    return {root: paths.PACKAGE_DIR, appDir: rel.split(path.sep).join('/')};
  }
  return {root: projectRoot, appDir: '.'};
}

/** Paths a --local dir source leaves out (relative to its root). */
function localSkips(root, appDir) {
  const skips = ['.git', 'build', 'node_modules', 'third-party/deps', 'spike/build', 'spike/out'];
  const app = appDir === '.' ? '' : `${appDir}/`;
  skips.push(`${app}node_modules`, `${app}linux/build`, `${app}build`);
  return [...new Set(skips)].filter(s => fs.existsSync(path.join(root, s)));
}

function writeManifestFiles(projectRoot, opts) {
  const {info} = opts;
  const out = path.join(opts.output, 'flatpak');
  const sourcesDir = path.join(out, 'sources');
  fs.mkdirSync(sourcesDir, {recursive: true});
  const cache = flatpakCacheDir();

  // React Native, Hermes and the third-party libraries, as fetch-rn-deps.py
  // pins them (read from the deps it fetched for run-linux).
  const deps = paths.depsDir();
  const toml = path.join(deps, 'react-native', 'packages', 'react-native', 'gradle', 'libs.versions.toml');
  // (Only the sources are needed here: the manifest builds everything.)
  if (!fs.existsSync(toml)) ensureDepsSources(deps);
  const props = paths.parseProperties(fs.readFileSync(path.join(paths.PACKAGE_DIR, 'rn-version.properties'), 'utf8'));
  step('Checksumming React Native\'s sources for the manifest');
  const hostDeps = checksummedSources(
    depsArchives({
      reactNative: props.reactNative,
      hermes: props.hermes,
      versions: thirdPartyVersions(fs.readFileSync(toml, 'utf8')),
    }),
    path.join(cache, 'archives'),
    {run},
  );

  // The package's host sources: a copy that changes only when they do.
  const hostCopy = path.join(cache, 'host-package', paths.hostSourceId());
  const hostFiles = syncTree(paths.PACKAGE_DIR, HOST_PACKAGE_FILES, hostCopy, {skip: HOST_PACKAGE_SKIP});
  const {root, appDir} = sourceRoot(projectRoot);
  let hostPackage;
  let app;
  if (opts.local) {
    hostPackage = {type: 'dir', path: hostCopy};
    app = {type: 'dir', path: root, skip: localSkips(root, appDir)};
  } else {
    const hostTar = tarball(hostCopy, hostFiles, path.join(sourcesDir, `react-native-gtk4-host-${paths.hostSourceId()}.tar.gz`), {run});
    const appTar = tarball(root, sourceFiles(root), path.join(sourcesDir, `${info.name}-${info.version}.tar.gz`), {run});
    // Paths are relative to the manifest.
    hostPackage = {type: 'archive', path: path.relative(out, hostTar), sha256: sha256File(hostTar), 'strip-components': 0};
    app = {type: 'archive', path: path.relative(out, appTar), sha256: sha256File(appTar), 'strip-components': 0};
    for (const f of fs.readdirSync(sourcesDir)) {
      const p = path.join(sourcesDir, f);
      if (![hostTar, appTar].includes(p) && /\.tar\.gz$/.test(f)) fs.rmSync(p);
    }
  }

  step('Generating offline npm sources (flatpak-node-generator)');
  const generator = nodeGenerator();
  if (!generator) {
    throw new RunError(
      'flatpak-node-generator is missing. Install it from flatpak-builder-tools:\n' +
        '  pipx install "git+https://github.com/flatpak/flatpak-builder-tools.git#subdirectory=node"\n' +
        'or set FLATPAK_NODE_GENERATOR to it',
    );
  }
  const appLock = path.join(projectRoot, 'package-lock.json');
  if (!fs.existsSync(appLock)) throw new RunError('No package-lock.json: run npm install first (Flatpak builds install from it offline)');
  const hostNpm = npmSources(path.join(paths.PACKAGE_DIR, 'scripts', 'codegen-deps', 'package-lock.json'), path.join(out, 'codegen-npm-sources.json'), {run, generator, filesDir: path.join(sourcesDir, 'npm')});
  const appNpmFile = npmSources(appLock, path.join(out, 'app-npm-sources.json'), {run, generator, filesDir: path.join(sourcesDir, 'npm')});
  // The app's npm cache sits next to the app (its module's build directory).
  const manifest = flatpakManifest(info, {hostDeps, hostPackage, hostNpm, app, appNpm: appNpmFile, appDir}, {version: info.version});
  const manifestFile = path.join(out, `${info.appId}.json`);
  fs.writeFileSync(manifestFile, JSON.stringify(manifest, null, 2) + '\n');
  return {manifestFile, out};
}

/** flatpak-builder, then flatpak build-bundle. */
function buildFlatpak(projectRoot, opts) {
  const {info} = opts;
  const {manifestFile} = writeManifestFiles(projectRoot, opts);
  console.log(`Manifest: ${manifestFile}`);
  const bundle = path.join(opts.output, `${info.name}-${info.version}-${machineArch()}.flatpak`);
  if (opts.manifestOnly) return {info, format: 'flatpak', manifest: manifestFile, artifact: manifestFile};
  for (const tool of ['flatpak', 'flatpak-builder']) {
    if (!which(tool)) {
      throw new RunError(`${tool} is missing: sudo apt install flatpak flatpak-builder (or dnf), or pass --manifest-only`);
    }
  }
  const cache = flatpakCacheDir();
  const repo = path.join(cache, 'repo');
  const buildDir = path.join(opts.output, 'flatpak', 'build-dir');
  step('Building the Flatpak (flatpak-builder; the first build compiles React Native, Hermes and the host)');
  run('flatpak-builder', [
    '--user',
    '--install-deps-from=flathub',
    '--force-clean',
    '--disable-updates',
    `--state-dir=${path.join(cache, 'state')}`,
    `--repo=${repo}`,
    ...(opts.local ? ['--ccache'] : []),
    ...(opts.logging ? ['--verbose'] : []),
    buildDir,
    manifestFile,
  ]);
  step(`Bundling ${path.basename(bundle)}`);
  run('flatpak', ['build-bundle', '--runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo', repo, bundle, info.appId]);
  fs.rmSync(buildDir, {recursive: true, force: true});
  if (opts.smoke) {
    step(`Installing ${path.basename(bundle)} (--user) and launching it with --smoke`);
    run('flatpak', ['install', '--user', '-y', '--noninteractive', '--bundle', '--reinstall', bundle]);
    run('flatpak', ['run', info.appId, '--smoke'], {cwd: os.tmpdir()});
  }
  return {info, format: 'flatpak', manifest: manifestFile, artifact: bundle};
}

module.exports = {buildFlatpak, writeManifestFiles, sourceRoot, localSkips};
