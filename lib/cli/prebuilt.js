/**
 * Prebuilt host libraries: the release workflow builds the host for each
 * architecture (Debug for run-linux, Release for packages) on Ubuntu 24.04
 * and attaches them to the GitHub release; the published package lists
 * them, with their checksums, in prebuilt-hosts.json. run-linux and
 * package-linux use a matching one instead of building the host (and
 * Hermes) from source:
 *
 *   - the package is unmodified (its host source id is the one built)
 *   - the system is the one they were built on (Ubuntu 24.04, or a
 *     distribution based on it, like Linux Mint 22)
 *   - every library it links is there (ldd)
 *
 * Otherwise, and with RNGTK_NO_PREBUILT=1, the host builds from source.
 *
 * A prebuilt tarball is the host's install directory with the absolute
 * paths of its SDK file (ReactNativeGtkSdk.cmake) replaced by
 * @RNGTK_PREFIX@ and @RNGTK_DEPS_DIR@, which unpacking fills in.
 */
'use strict';

const crypto = require('crypto');
const {spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');
const paths = require('./paths');
const {parseOsRelease} = require('./prereqs');

const MANIFEST = 'prebuilt-hosts.json';
const SDK_FILE = path.join('lib', 'cmake', 'ReactNativeGtk', 'ReactNativeGtkSdk.cmake');
const PREFIX_TOKEN = '@RNGTK_PREFIX@';
const DEPS_TOKEN = '@RNGTK_DEPS_DIR@';

/** The system prebuilt hosts are built for, or null for any other. */
function prebuiltSystem(osRelease) {
  const ids = [osRelease.ID, ...(osRelease.ID_LIKE || '').split(/\s+/)].filter(Boolean);
  const codename = osRelease.UBUNTU_CODENAME || (osRelease.ID === 'ubuntu' ? osRelease.VERSION_CODENAME : null);
  if (ids.includes('ubuntu') && codename === 'noble') return 'ubuntu24.04';
  return null;
}

function machineArch(arch = process.arch) {
  return {arm64: 'aarch64', x64: 'x86_64'}[arch] || arch;
}

/** The asset name for a host build. */
function assetName({version, system, arch, release}) {
  return `react-native-gtk4-host-${version}-${system}-${arch}-${release ? 'release' : 'debug'}.tar.gz`;
}

/** The package's prebuilt-hosts.json, or null (in the repository, say). */
function readManifest(packageDir = paths.PACKAGE_DIR) {
  try {
    return JSON.parse(fs.readFileSync(path.join(packageDir, MANIFEST), 'utf8'));
  } catch {
    return null;
  }
}

/**
 * The prebuilt host for this system, or {reason} saying why there's none.
 */
function findPrebuilt({release, manifest = readManifest(), osRelease, arch = machineArch(), sourceId = paths.hostSourceId(), env = process.env}) {
  if (env.RNGTK_NO_PREBUILT === '1') return {reason: 'RNGTK_NO_PREBUILT=1'};
  if (!manifest) return {reason: 'this package has no prebuilt hosts'};
  if (manifest.hostSourceId !== sourceId) return {reason: "this package's linux/ isn't the one the prebuilt hosts were built from"};
  if (!osRelease) {
    try {
      osRelease = parseOsRelease(fs.readFileSync('/etc/os-release', 'utf8'));
    } catch {
      osRelease = {};
    }
  }
  const system = prebuiltSystem(osRelease);
  if (!system) return {reason: 'prebuilt hosts are for Ubuntu 24.04 and its derivatives'};
  const name = assetName({version: manifest.version, system, arch, release});
  const host = manifest.hosts?.[name];
  if (!host) return {reason: `no prebuilt ${name}`};
  return {url: `${manifest.baseUrl}/${name}`, sha256: host.sha256, name};
}

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

/** Libraries `ldd` can't find for the host's libraries ([] when none). */
function missingLibraries(libDir) {
  const missing = new Set();
  for (const f of fs.readdirSync(libDir).filter(f => f.endsWith('.so'))) {
    const r = spawnSync('ldd', [path.join(libDir, f)], {encoding: 'utf8', env: {...process.env, LD_LIBRARY_PATH: libDir}});
    for (const m of (r.stdout || '').matchAll(/^\s*(\S+) => not found/gm)) missing.add(m[1]);
  }
  return [...missing];
}

/** Fills the SDK file's tokens in for where the host and deps are. */
function relocate(install, deps) {
  const file = path.join(install, SDK_FILE);
  if (!fs.existsSync(file)) return;
  const text = fs.readFileSync(file, 'utf8');
  fs.writeFileSync(file, text.split(PREFIX_TOKEN).join(install).split(DEPS_TOKEN).join(deps));
}

/**
 * Downloads, checks and unpacks a prebuilt host into `host`/install.
 * Returns the CMake config dir, or null (with why on the console) when
 * the host should be built from source instead.
 */
function installPrebuilt(host, deps, {release, run, log = console.log, ...find}) {
  const found = findPrebuilt({release, ...find});
  if (!found.url) {
    log(`(No prebuilt host: ${found.reason}. Building it from source.)`);
    return null;
  }
  const install = path.join(host, 'install');
  const download = path.join(host, found.name);
  fs.mkdirSync(host, {recursive: true});
  log(`Downloading the prebuilt host ${found.url}`);
  const r = spawnSync('curl', ['-fsSL', '--retry', '3', '-o', download, found.url], {stdio: 'inherit'});
  if (r.status !== 0 || !fs.existsSync(download)) {
    log('(The download failed. Building the host from source.)');
    return null;
  }
  if (sha256(download) !== found.sha256) {
    fs.rmSync(download, {force: true});
    log('(The prebuilt host\'s checksum is wrong. Building it from source.)');
    return null;
  }
  fs.rmSync(install, {recursive: true, force: true});
  fs.mkdirSync(install, {recursive: true});
  run('tar', ['-xzf', download, '-C', install]);
  fs.rmSync(download, {force: true});
  relocate(install, deps);
  const missing = missingLibraries(path.join(install, 'lib'));
  if (missing.length) {
    fs.rmSync(install, {recursive: true, force: true});
    log(`(The prebuilt host needs ${missing.join(', ')}, which this system doesn't have. Building it from source.)`);
    return null;
  }
  fs.writeFileSync(path.join(install, '.prebuilt'), `${found.url}\n`);
  fs.writeFileSync(path.join(install, '.complete'), `${new Date().toISOString()}\n`);
  return path.join(install, 'lib', 'cmake', 'ReactNativeGtk');
}

/**
 * Turns an installed host into a prebuilt tarball (the release workflow):
 * the SDK file's absolute paths become tokens. Returns its sha256.
 */
function packPrebuilt(install, deps, out, {run, prefix = install}) {
  const file = path.join(install, SDK_FILE);
  const text = fs.readFileSync(file, 'utf8');
  // (The prefix it was installed with: `install`, unless it was moved.)
  fs.writeFileSync(file, text.split(path.resolve(prefix)).join(PREFIX_TOKEN).split(path.resolve(deps)).join(DEPS_TOKEN));
  for (const f of ['.complete', '.prebuilt']) fs.rmSync(path.join(install, f), {force: true});
  run('tar', ['--sort=name', '--owner=0', '--group=0', '--numeric-owner', '-czf', out, '-C', install, '.']);
  fs.writeFileSync(file, text);
  return sha256(out);
}

module.exports = {
  MANIFEST,
  PREFIX_TOKEN,
  DEPS_TOKEN,
  prebuiltSystem,
  assetName,
  readManifest,
  findPrebuilt,
  missingLibraries,
  relocate,
  installPrebuilt,
  packPrebuilt,
};
