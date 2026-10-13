/**
 * Prebuilt host libraries: the release workflow builds the host for each
 * architecture (Debug for run-linux, Release for packages) on each system
 * prebuiltSystem names and attaches them to the GitHub release; the published package lists
 * them, with their checksums, in prebuilt-hosts.json, which is attached
 * to the release too: a package installed from a git tag (no manifest of
 * its own) fetches it from there, so it needs no GitHub Packages token.
 * run-linux and package-linux use a matching one instead of building the
 * host (and Hermes) from source:
 *
 *   - the package is unmodified (its host source id is the one built)
 *   - the system is one they were built on (prebuiltSystem)
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
// The Fedora releases with prebuilt hosts: the two Fedora supports.
const FEDORA = ['44', '45'];
const RELEASES = 'https://github.com/curiosity26/react-native-gtk4/releases/download';
const SDK_FILE = path.join('lib', 'cmake', 'ReactNativeGtk', 'ReactNativeGtkSdk.cmake');
const PREFIX_TOKEN = '@RNGTK_PREFIX@';
const DEPS_TOKEN = '@RNGTK_DEPS_DIR@';

/**
 * The system prebuilt hosts are built for, or null for any other: Ubuntu
 * 24.04 (and Linux Mint 22 and other derivatives), Debian 13 (and LMDE 7),
 * Fedora 44 and 45, and Enterprise Linux 10 (RHEL, CentOS Stream, AlmaLinux,
 * Rocky Linux).
 */
function prebuiltSystem(osRelease) {
  const ids = [osRelease.ID, ...(osRelease.ID_LIKE || '').split(/\s+/)].filter(Boolean);
  const codename = osRelease.UBUNTU_CODENAME || (osRelease.ID === 'ubuntu' ? osRelease.VERSION_CODENAME : null);
  const major = String(osRelease.VERSION_ID || '').split('.')[0];
  if (ids.includes('ubuntu')) return codename === 'noble' ? 'ubuntu24.04' : null;
  const debian = osRelease.DEBIAN_CODENAME || (osRelease.ID === 'debian' ? osRelease.VERSION_CODENAME : null);
  if (debian === 'trixie' || (osRelease.ID === 'debian' && major === '13')) return 'debian13';
  if (osRelease.ID === 'fedora') return FEDORA.includes(major) ? `fedora${major}` : null;
  if (ids.includes('rhel') && major === '10') return 'el10';
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

/** Where the GitHub release of `version` has its prebuilt-hosts.json. */
function releaseManifestUrl(version = paths.packageVersion()) {
  return `${RELEASES}/v${version}/${MANIFEST}`;
}

/**
 * The manifest attached to this version's GitHub release (for a package
 * installed from git, which has none), or null when there's none.
 */
function fetchManifest(dir, {version, log = console.log} = {}) {
  const url = releaseManifestUrl(version);
  const file = path.join(dir, MANIFEST);
  fs.mkdirSync(dir, {recursive: true});
  const r = spawnSync('curl', ['-fsSL', '--retry', '3', '-o', file, url], {stdio: ['ignore', 'ignore', 'pipe']});
  try {
    if (r.status === 0) return JSON.parse(fs.readFileSync(file, 'utf8'));
    log(`(No ${url}.)`);
    return null;
  } catch {
    return null;
  } finally {
    fs.rmSync(file, {force: true});
  }
}

function systemOsRelease() {
  try {
    return parseOsRelease(fs.readFileSync('/etc/os-release', 'utf8'));
  } catch {
    return {};
  }
}

/**
 * The prebuilt host for this system, or {reason} saying why there's none.
 */
function findPrebuilt({release, manifest = readManifest(), osRelease = systemOsRelease(), arch = machineArch(), sourceId = paths.hostSourceId(), env = process.env}) {
  if (env.RNGTK_NO_PREBUILT === '1') return {reason: 'RNGTK_NO_PREBUILT=1'};
  if (!manifest) return {reason: 'this package has no prebuilt hosts'};
  if (manifest.hostSourceId !== sourceId) return {reason: "this package's linux/ isn't the one the prebuilt hosts were built from"};
  const system = prebuiltSystem(osRelease);
  if (!system) return {reason: 'prebuilt hosts are for Ubuntu 24.04, Debian 13, Fedora 44 and 45, and Enterprise Linux 10'};
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
function installPrebuilt(host, deps, {release, run, log = console.log, fetch = fetchManifest, ...find}) {
  const env = find.env || process.env;
  const osRelease = find.osRelease || systemOsRelease();
  let manifest = 'manifest' in find ? find.manifest : readManifest();
  // Installed from git: the release's manifest, when one could be used.
  if (!manifest && env.RNGTK_NO_PREBUILT !== '1' && prebuiltSystem(osRelease)) {
    manifest = fetch(host, {log});
  }
  const found = findPrebuilt({release, ...find, env, osRelease, manifest});
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
  releaseManifestUrl,
  fetchManifest,
  findPrebuilt,
  missingLibraries,
  relocate,
  installPrebuilt,
  packPrebuilt,
};
