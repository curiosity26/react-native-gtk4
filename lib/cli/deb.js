/**
 * package-linux --format deb: the installed tree under /usr as a Debian
 * package, built with dpkg-deb. Its Depends come from dpkg-shlibdeps (the
 * system libraries the app and its private host libraries link), so the
 * package is for the distribution release it was built on.
 *
 *   /usr/bin/<app>
 *   /usr/lib/<app>/              the host libraries (private)
 *   /usr/share/<app>/            bundle and assets
 *   /usr/share/applications/, metainfo/, icons/hicolor/
 *   /usr/share/doc/<package>/copyright, changelog.Debian.gz
 */
'use strict';

const crypto = require('crypto');
const {spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

/** A Debian package name from the app's (lowercase, [a-z0-9+.-]). */
function debPackageName(info) {
  const configured = info.deb?.package;
  if (configured) return configured;
  let s = String(info.name).toLowerCase().replace(/[^a-z0-9+.-]/g, '-').replace(/^[^a-z0-9]+/, '');
  if (s.length < 2) s = `${s}app`;
  return s;
}

/** A Debian version: upstream version, plus a revision (default 1). */
function debVersion(info) {
  const upstream = String(info.version).replace(/[^A-Za-z0-9.+~-]/g, '~');
  return `${upstream}-${info.deb?.revision || 1}`;
}

function dpkgArch() {
  const r = spawnSync('dpkg', ['--print-architecture'], {encoding: 'utf8'});
  if (r.status === 0) return r.stdout.trim();
  return {arm64: 'arm64', x64: 'amd64'}[process.arch] || process.arch;
}

/**
 * The maintainer, "Name <email>": linux.maintainer, $DEBFULLNAME and
 * $DEBEMAIL, package.json's author, or git's user.
 */
function maintainer(info, pkg = {}, env = process.env) {
  if (info.maintainer) return info.maintainer;
  if (env.DEBEMAIL) return `${env.DEBFULLNAME || info.developer?.name || 'Maintainer'} <${env.DEBEMAIL}>`;
  const author = pkg.author;
  if (typeof author === 'string' && /<[^>]+@[^>]+>/.test(author)) return author.replace(/\s*\([^)]*\)\s*$/, '');
  if (author && author.email) return `${author.name || info.developer?.name || 'Maintainer'} <${author.email}>`;
  const git = key => spawnSync('git', ['config', key], {encoding: 'utf8'}).stdout?.trim();
  const email = git('user.email');
  if (email) return `${git('user.name') || 'Maintainer'} <${email}>`;
  return null;
}

/** The Description field: the summary, then the description folded. */
function debDescription(info) {
  const body = String(info.description || '')
    .trim()
    .split(/\n\s*\n/)
    .map(p => p.split('\n').map(l => l.trim()).join(' ').trim())
    .filter(Boolean);
  const lines = [];
  body.forEach((p, i) => {
    if (i) lines.push(' .');
    // Folded to ~76 columns, each continuation line starting with a space.
    let line = '';
    for (const word of p.split(/\s+/)) {
      if (line && line.length + word.length + 1 > 75) {
        lines.push(` ${line}`);
        line = word;
      } else {
        line = line ? `${line} ${word}` : word;
      }
    }
    if (line) lines.push(` ${line}`);
  });
  if (!lines.length) lines.push(` ${info.summary}`);
  return [info.summary, ...lines].join('\n');
}

function controlFile(info, {package: name, version, arch, depends, installedSize, maintainer: m}) {
  const fields = [
    ['Package', name],
    ['Version', version],
    ['Architecture', arch],
    ['Maintainer', m],
    ['Installed-Size', String(installedSize)],
    ['Depends', depends],
    ['Section', info.deb?.section || 'misc'],
    ['Priority', 'optional'],
    ['Homepage', info.homepage],
    ['Description', debDescription(info)],
  ];
  return fields.filter(([, v]) => v).map(([k, v]) => `${k}: ${v}`).join('\n') + '\n';
}

/** debian/copyright in the machine-readable format, with the license texts. */
function copyrightFile(info, {licenseTexts}) {
  const out = [
    'Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/',
    `Upstream-Name: ${info.displayName}`,
  ];
  if (info.homepage) out.push(`Source: ${info.homepage}`);
  out.push('', 'Files: *', `Copyright: ${info.developer?.name || info.displayName}`, `License: ${info.license}`);
  const indent = text =>
    text
      .trimEnd()
      .split('\n')
      .map(l => (l.trim() ? ` ${l}` : ' .'))
      .join('\n');
  if (licenseTexts.app) out.push(indent(licenseTexts.app));
  out.push(
    '',
    `Files: usr/lib/${info.name}/*`,
    'Copyright: Meta Platforms, Inc. and affiliates (React Native, Hermes); Alex Boyce (React Native for GTK4)',
    'License: MIT',
  );
  if (licenseTexts.host) out.push(indent(licenseTexts.host));
  return out.join('\n') + '\n';
}

function changelogFile(info, {package: name, version, maintainer: m, date}) {
  const when = new Date(`${date}T00:00:00Z`).toUTCString().replace('GMT', '+0000');
  return `${name} (${version}) unstable; urgency=medium\n\n  * Release ${info.version}.\n\n -- ${m}  ${when}\n`;
}

/**
 * Debian's permissions, whatever the umask was: directories 0755, files
 * 0644, or 0755 when executable.
 */
function normalizeModes(root) {
  const walk = dir => {
    for (const e of fs.readdirSync(dir, {withFileTypes: true})) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) {
        fs.chmodSync(p, 0o755);
        walk(p);
      } else if (e.isFile()) {
        fs.chmodSync(p, fs.statSync(p).mode & 0o111 ? 0o755 : 0o644);
      }
    }
  };
  fs.chmodSync(root, 0o755);
  walk(root);
}

/**
 * The AppArmor profile an app using WebKitGTK needs on Ubuntu 24.04 and
 * later, where unprivileged user namespaces (WebKit's sandbox) need a
 * profile that allows them: like Ubuntu's own for Epiphany and Devhelp, it
 * allows everything and adds `userns`.
 */
function apparmorProfile(name, exe) {
  return `# The app runs web views (WebKitGTK), whose sandbox needs unprivileged
# user namespaces. This profile allows everything and adds that, as
# Ubuntu's own for Epiphany. (Generated by react-native package-linux.)

abi <abi/4.0>,
include <tunables/global>

profile ${name} ${exe} flags=(unconfined) {
  userns,

  # Site-specific additions and overrides. See local/README for details.
  include if exists <local/${name}>
}
`;
}

// postinst and postrm for it (what dh_apparmor writes, shortened).
function apparmorScripts(name) {
  const profile = `/etc/apparmor.d/${name}`;
  return {
    postinst: `#!/bin/sh
set -e
if [ "$1" = configure ] && command -v apparmor_parser >/dev/null 2>&1 && aa-enabled --quiet 2>/dev/null; then
  apparmor_parser -r -T -W ${profile} || true
fi
`,
    postrm: `#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
  if command -v apparmor_parser >/dev/null 2>&1 && [ -d /sys/kernel/security/apparmor ]; then
    apparmor_parser -R -T -W ${profile} 2>/dev/null || true
  fi
fi
`,
  };
}

/** Sums of the files under `root` (DEBIAN/md5sums), relative and sorted. */
function md5sums(root) {
  const lines = [];
  const walk = rel => {
    for (const e of fs.readdirSync(path.join(root, rel), {withFileTypes: true}).sort((a, b) => a.name.localeCompare(b.name))) {
      const child = rel ? `${rel}/${e.name}` : e.name;
      if (child === 'DEBIAN') continue;
      if (e.isDirectory()) walk(child);
      else if (e.isFile()) {
        const sum = crypto.createHash('md5').update(fs.readFileSync(path.join(root, child))).digest('hex');
        lines.push(`${sum}  ${child}`);
      }
    }
  };
  walk('');
  return lines.join('\n') + '\n';
}

function duKiB(root) {
  let bytes = 0;
  const walk = dir => {
    for (const e of fs.readdirSync(dir, {withFileTypes: true})) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else bytes += fs.lstatSync(p).size;
    }
  };
  walk(root);
  return Math.ceil(bytes / 1024);
}

/**
 * dpkg-shlibdeps on the app and its private libraries: the Depends line.
 * It needs a debian/control, so it runs in a scratch source tree.
 */
function shlibDepends(root, info, {run, scratch}) {
  const src = path.join(scratch, 'shlibdeps');
  fs.rmSync(src, {recursive: true, force: true});
  fs.mkdirSync(path.join(src, 'debian'), {recursive: true});
  fs.writeFileSync(path.join(src, 'debian', 'control'), `Source: ${debPackageName(info)}\n\nPackage: ${debPackageName(info)}\nArchitecture: any\n`);
  const libDir = path.join(root, 'usr', 'lib', info.name);
  const elfs = [path.join(root, 'usr', 'bin', info.name), ...fs.readdirSync(libDir).filter(f => f.endsWith('.so')).map(f => path.join(libDir, f))];
  const r = run('dpkg-shlibdeps', ['-O', `-l${libDir}`, '--ignore-missing-info', ...elfs.map(e => `-e${e}`)], {cwd: src, quiet: true});
  const m = r.stdout.match(/^shlibs:Depends=(.*)$/m);
  return m ? m[1].trim() : '';
}

/**
 * Moves the staged tree (prefix /usr) into a Debian package and builds it.
 * `stage` is the package root (it has usr/). Returns the .deb's path.
 */
function buildDeb(stage, info, {output, run, pkg, scratch, date, licenseTexts}) {
  const name = debPackageName(info);
  const version = debVersion(info);
  const arch = dpkgArch();
  const m = maintainer(info, pkg);
  if (!m) throw new Error('No maintainer for the .deb: set linux.maintainer ("Name <email>") in app.json, or DEBEMAIL');

  // share/licenses isn't Debian's place for them: they go into copyright.
  fs.rmSync(path.join(stage, 'usr', 'share', 'licenses'), {recursive: true, force: true});
  const doc = path.join(stage, 'usr', 'share', 'doc', name);
  fs.mkdirSync(doc, {recursive: true});
  fs.writeFileSync(path.join(doc, 'copyright'), copyrightFile(info, {licenseTexts}));
  fs.writeFileSync(
    path.join(doc, 'changelog.Debian.gz'),
    zlib.gzipSync(changelogFile(info, {package: name, version, maintainer: m, date}), {level: 9}),
  );
  normalizeModes(stage);

  const depends = shlibDepends(stage, info, {run, scratch});
  const control = path.join(stage, 'DEBIAN');
  fs.mkdirSync(control, {recursive: true});
  // Web views (libwebkitgtk-6.0): the AppArmor profile their sandbox needs.
  const webkit = /\blibwebkitgtk-6\.0/.test(depends);
  if (webkit) {
    const dir = path.join(stage, 'etc', 'apparmor.d');
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, name), apparmorProfile(name, `/usr/bin/${info.name}`));
    normalizeModes(stage);
    fs.writeFileSync(path.join(control, 'conffiles'), `/etc/apparmor.d/${name}\n`);
    const scripts = apparmorScripts(name);
    for (const [script, text] of Object.entries(scripts)) {
      fs.writeFileSync(path.join(control, script), text, {mode: 0o755});
      fs.chmodSync(path.join(control, script), 0o755);
    }
    fs.chmodSync(path.join(control, 'conffiles'), 0o644);
  }
  fs.writeFileSync(path.join(control, 'md5sums'), md5sums(stage));
  fs.writeFileSync(path.join(control, 'control'), controlFile(info, {package: name, version, arch, depends, installedSize: duKiB(path.join(stage, 'usr')), maintainer: m}));
  fs.chmodSync(path.join(control, 'md5sums'), 0o644);
  fs.chmodSync(path.join(control, 'control'), 0o644);
  const deb = path.join(output, `${name}_${version}_${arch}.deb`);
  run('dpkg-deb', ['--build', '--root-owner-group', '-Zxz', stage, deb], {quiet: true});
  return {deb, package: name, version, arch, depends, apparmor: webkit};
}

module.exports = {
  debPackageName,
  debVersion,
  maintainer,
  debDescription,
  controlFile,
  copyrightFile,
  changelogFile,
  md5sums,
  normalizeModes,
  apparmorProfile,
  apparmorScripts,
  buildDeb,
};
