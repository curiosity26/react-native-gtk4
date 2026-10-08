/**
 * Checks for the tools and libraries `react-native run-linux` needs, with
 * install hints for the user's distribution.
 */
'use strict';

const {spawnSync} = require('child_process');
const fs = require('fs');

// Package names per package manager, keyed by the check that needs them.
const PACKAGES = {
  apt: {
    clang: 'clang',
    cmake: 'cmake',
    ninja: 'ninja-build',
    'pkg-config': 'pkg-config',
    git: 'git',
    python3: 'python3',
    gtk4: 'libgtk-4-dev',
    'libsoup-3.0': 'libsoup-3.0-dev',
    openssl: 'libssl-dev',
    'icu-uc': 'libicu-dev',
    readline: 'libreadline-dev',
  },
  dnf: {
    clang: 'clang',
    cmake: 'cmake',
    ninja: 'ninja-build',
    'pkg-config': 'pkgconf-pkg-config',
    git: 'git',
    python3: 'python3',
    gtk4: 'gtk4-devel',
    'libsoup-3.0': 'libsoup3-devel',
    openssl: 'openssl-devel',
    'icu-uc': 'libicu-devel',
    readline: 'readline-devel',
  },
};

const MIN = {
  clang: '16.0.0',
  cmake: '3.22.0',
  gtk4: '4.14.0',
  'libsoup-3.0': '3.0.0',
  node: '22.13.0',
};

/** Parses KEY=value lines of /etc/os-release. */
function parseOsRelease(text) {
  const out = {};
  for (const line of text.split('\n')) {
    const m = line.match(/^([A-Z_]+)=(.*)$/);
    if (m) out[m[1]] = m[2].replace(/^"(.*)"$/, '$1');
  }
  return out;
}

/**
 * The distribution family for install hints: {id, name, manager} where
 * manager is 'apt', 'dnf' or null.
 */
function detectDistro(osRelease) {
  const ids = [osRelease.ID, ...(osRelease.ID_LIKE || '').split(/\s+/)]
    .filter(Boolean)
    .map(s => s.toLowerCase());
  const name = osRelease.PRETTY_NAME || osRelease.NAME || 'Linux';
  if (ids.some(id => ['debian', 'ubuntu', 'linuxmint'].includes(id))) {
    return {id: osRelease.ID, name, manager: 'apt'};
  }
  if (ids.some(id => ['fedora', 'rhel', 'centos'].includes(id))) {
    return {id: osRelease.ID, name, manager: 'dnf'};
  }
  return {id: osRelease.ID || 'linux', name, manager: null};
}

/** The first x.y[.z] version in `text`, as "x.y.z", or null. */
function parseVersion(text) {
  const m = String(text || '').match(/(\d+)\.(\d+)(?:\.(\d+))?/);
  return m ? `${m[1]}.${m[2]}.${m[3] || 0}` : null;
}

function compareVersions(a, b) {
  const pa = a.split('.').map(Number);
  const pb = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) - (pb[i] || 0);
  }
  return 0;
}

function atLeast(version, min) {
  return version != null && compareVersions(version, min) >= 0;
}

/** The install command for missing packages, or null. */
function installHint(manager, keys) {
  const names = [...new Set(keys.map(k => PACKAGES[manager]?.[k]).filter(Boolean))];
  if (!names.length) return null;
  if (manager === 'apt') return `sudo apt install -y ${names.join(' ')}`;
  if (manager === 'dnf') return `sudo dnf install -y ${names.join(' ')}`;
  return null;
}

function defaultRunner(cmd, args) {
  const r = spawnSync(cmd, args, {encoding: 'utf8'});
  if (r.error) return {ok: false, stdout: '', stderr: String(r.error.message)};
  return {ok: r.status === 0, stdout: r.stdout || '', stderr: r.stderr || ''};
}

/**
 * Runs every check. `run(cmd, args)` returns {ok, stdout, stderr}; tests
 * pass a fake. Returns {distro, results: [{key, label, ok, version, need,
 * note}], missing: [keys], hint}.
 */
function checkPrerequisites({
  run = defaultRunner,
  nodeVersion = process.versions.node,
  osRelease = readOsRelease(),
} = {}) {
  const distro = detectDistro(osRelease);
  const results = [];

  const tool = (key, label, args, min) => {
    const r = run(key === 'ninja' ? 'ninja' : key, args);
    const version = r.ok ? parseVersion(r.stdout + r.stderr) : null;
    const ok = r.ok && (!min || atLeast(version, min));
    results.push({key, label, ok, version, need: min ? `>= ${min}` : null});
  };
  tool('clang', 'clang', ['--version'], MIN.clang);
  tool('cmake', 'CMake', ['--version'], MIN.cmake);
  tool('ninja', 'Ninja', ['--version']);
  tool('pkg-config', 'pkg-config', ['--version']);
  tool('git', 'git', ['--version']);
  tool('python3', 'Python 3', ['--version']);

  const hasPkgConfig = results.find(r => r.key === 'pkg-config').ok;
  const lib = (key, label, min) => {
    const r = hasPkgConfig ? run('pkg-config', ['--modversion', key]) : {ok: false};
    const version = r.ok ? parseVersion(r.stdout) : null;
    const ok = r.ok && (!min || atLeast(version, min));
    results.push({key, label, ok, version, need: min ? `>= ${min}` : null});
  };
  lib('gtk4', 'GTK 4', MIN.gtk4);
  lib('libsoup-3.0', 'libsoup 3', MIN['libsoup-3.0']);
  lib('openssl', 'OpenSSL');
  lib('icu-uc', 'ICU');
  lib('readline', 'readline');

  const nodeOk = atLeast(parseVersion(nodeVersion), MIN.node);
  results.push({
    key: 'node',
    label: 'Node.js',
    ok: nodeOk,
    version: parseVersion(nodeVersion),
    need: `>= ${MIN.node}`,
    note: nodeOk
      ? null
      : 'Install Node 22 or 24 LTS from https://nodejs.org (or nvm); ' +
        "some distributions' packaged nodejs is older.",
  });

  const gtk = results.find(r => r.key === 'gtk4');
  if (gtk.version && !gtk.ok) {
    gtk.note =
      'GTK 4.14 or newer ships with Ubuntu 24.04, Linux Mint 22, Debian 13 ' +
      'and Fedora 40 and later.';
  }

  const missing = results.filter(r => !r.ok).map(r => r.key);
  const hint = distro.manager
    ? installHint(distro.manager, missing.filter(k => k !== 'node'))
    : null;
  return {distro, results, missing, hint};
}

function readOsRelease() {
  for (const file of ['/etc/os-release', '/usr/lib/os-release']) {
    try {
      return parseOsRelease(fs.readFileSync(file, 'utf8'));
    } catch {}
  }
  return {};
}

/** Human-readable report lines. */
function formatReport({distro, results, missing, hint}) {
  const lines = [];
  for (const r of results) {
    const status = r.ok ? 'ok  ' : 'MISS';
    const version = r.version ? ` ${r.version}` : '';
    const need = !r.ok && r.need ? ` (need ${r.need})` : '';
    lines.push(`  ${status} ${r.label}${version}${need}`);
    if (r.note) lines.push(`       ${r.note}`);
  }
  if (missing.length) {
    if (hint) {
      lines.push('', `On ${distro.name}, install the missing packages with:`, `  ${hint}`);
    } else if (missing.some(k => k !== 'node')) {
      lines.push(
        '',
        `Install clang, CMake, Ninja, pkg-config and the GTK 4, libsoup 3, ` +
          `OpenSSL, ICU and readline development packages for ${distro.name}.`,
      );
    }
  }
  return lines;
}

module.exports = {
  PACKAGES,
  MIN,
  parseOsRelease,
  detectDistro,
  parseVersion,
  compareVersions,
  installHint,
  checkPrerequisites,
  formatReport,
};
