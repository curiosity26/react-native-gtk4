/**
 * `react-native package-linux`: the app, built for Release and installed
 * into a package.
 *
 * 1. Reads the app's identity (app.json's `linux` block, appInfo.js).
 * 2. Builds linux/ for Release with the JS bundled, as run-linux --release.
 * 3. Writes the .desktop file, MetaInfo, icons and licenses into
 *    <build>/packaging, and installs the app (cmake --install) into a
 *    staging prefix:
 *      bin/<app>  lib/<app>/  share/<app>/  share/applications/ ...
 * 4. Makes the format's package from it:
 *      dir       the prefix itself, <output>/<app>-<version>-<arch>/, with
 *                an install.sh that copies it into a prefix (~/.local);
 *                or, with --prefix, installed straight into that prefix
 *      deb       a Debian package (deb.js): the tree under /usr, Depends
 *                from dpkg-shlibdeps, checked with lintian
 *      rpm       an RPM (rpm.js) from a generated spec, checked with rpmlint
 *      flatpak   a flatpak-builder manifest and a .flatpak bundle
 *                (flatpak.js, flatpakBuild.js): the manifest's build runs
 *                package-linux --prefix /app inside the GNOME SDK
 */
'use strict';

const {spawnSync} = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const paths = require('./paths');
const {checkPrerequisites, formatReport} = require('./prereqs');
const {appInfo, validateAppInfo, writePackagingFiles, releaseDate} = require('./appInfo');
const {buildFlatpak} = require('./flatpakBuild');
const {buildDeb, maintainer} = require('./deb');
const {buildRpm} = require('./rpm');
const {
  RunError,
  run,
  step,
  elapsed,
  ensureHost,
  buildApp,
  bundle,
  which,
} = require('./runLinux');

const FORMATS = ['dir', 'flatpak', 'deb', 'rpm'];

/** The machine name packages use (aarch64, x86_64). */
function machineArch(arch = process.arch) {
  return {arm64: 'aarch64', x64: 'x86_64', ia32: 'i686', arm: 'armv7l'}[arch] || arch;
}

/** Problems with linux/main.cc's app id: the .desktop file is named after it. */
function checkMainAppId(projectRoot, appId) {
  let main = '';
  try {
    main = fs.readFileSync(path.join(projectRoot, 'linux', 'main.cc'), 'utf8');
  } catch {
    return null;
  }
  const m = main.match(/options\.appId\s*=\s*"([^"]*)"/);
  if (!m || m[1] === appId) return null;
  return (
    `linux/main.cc sets options.appId = "${m[1]}", but app.json's linux.appId is "${appId}": ` +
    'make them the same, or the desktop won\'t match the window to its .desktop file ' +
    '(icon, name, notifications)'
  );
}

/**
 * The dir package's install.sh: copies bin/, lib/ and share/ into a prefix
 * (default ~/.local), or removes them again (--uninstall). Outside /usr
 * the .desktop file gets an absolute Exec: GNOME Shell's PATH doesn't have
 * ~/.local/bin, and it ignores apps (and their notifications) whose Exec it
 * can't find.
 */
function installScript(info, files) {
  const list = files.map(f => `  ${f}`).join('\n');
  return `#!/bin/sh
# Installs ${info.displayName} ${info.version} into a prefix.
#   ./install.sh [PREFIX]              (default: ~/.local)
#   ./install.sh --uninstall [PREFIX]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
uninstall=0
if [ "\${1:-}" = --uninstall ]; then uninstall=1; shift; fi
prefix=\${1:-$HOME/.local}
files='
${list}
'
if [ $uninstall = 1 ]; then
  for f in $files; do
    rm -f "$prefix/$f"
    case "$f" in share/icons/*) rmdir "$(dirname "$prefix/$f")" "$(dirname "$(dirname "$prefix/$f")")" 2>/dev/null || true ;; esac
  done
  rm -rf "$prefix/lib/${info.name}" "$prefix/share/${info.name}" "$prefix/share/licenses/${info.appId}"
  echo "Removed ${info.name} from $prefix"
else
  for f in $files; do
    mkdir -p "$(dirname "$prefix/$f")"
    cp -f "$here/$f" "$prefix/$f"
  done
  chmod 755 "$prefix/bin/${info.name}"
  case "$prefix" in
    /usr|/usr/local) ;;
    *) sed -i "s|^Exec=${info.name}|Exec=$prefix/bin/${info.name}|" "$prefix/share/applications/${info.appId}.desktop" ;;
  esac
  echo "Installed ${info.name} into $prefix: run $prefix/bin/${info.name}, or find ${info.displayName} in your apps"
fi
command -v update-desktop-database >/dev/null && update-desktop-database -q "$prefix/share/applications" 2>/dev/null || true
if [ -f "$prefix/share/icons/hicolor/index.theme" ]; then
  command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$prefix/share/icons/hicolor" 2>/dev/null || true
fi
`;
}

/** The files under `dir`, relative to it, sorted. */
function treeFiles(dir) {
  return fs
    .readdirSync(dir, {recursive: true})
    .filter(f => fs.statSync(path.join(dir, f)).isFile())
    .map(f => f.split(path.sep).join('/'))
    .sort();
}

/** desktop-file-validate and appstreamcli validate, when installed. */
function validateMetadata(stage, info, {log = console.log} = {}) {
  const results = [];
  const checks = [
    ['desktop-file-validate', [path.join(stage, 'share', 'applications', `${info.appId}.desktop`)]],
    ['appstreamcli', ['validate', '--no-net', '--explain', path.join(stage, 'share', 'metainfo', `${info.appId}.metainfo.xml`)]],
  ];
  for (const [cmd, args] of checks) {
    if (!which(cmd)) {
      log(`(${cmd} isn't installed: skipped)`);
      continue;
    }
    const r = spawnSync(cmd, args, {encoding: 'utf8'});
    const output = `${r.stdout || ''}${r.stderr || ''}`.trim();
    results.push({cmd, ok: r.status === 0, output});
    log(`${r.status === 0 ? 'OK  ' : 'WARN'} ${cmd}${output ? `\n${output.replace(/^/gm, '     ')}` : ''}`);
  }
  return results;
}

/**
 * Builds the app and installs it into `stage` (a prefix). Returns
 * {info, buildDir, stage}.
 */
function stageApp(projectRoot, opts) {
  const quiet = !opts.logging;
  const info = opts.info;
  let configDir = opts.host && path.resolve(opts.host);
  if (!configDir) {
    // Packages ship an optimized host (a cache entry of its own).
    configDir = ensureHost(paths.hostDir(process.env, paths.PACKAGE_DIR, {release: true}), paths.depsDir(), {
      verbose: opts.logging,
      quiet,
      release: true,
      libraries: opts.libraries,
    });
  }
  const buildDir = buildApp(projectRoot, configDir, {...opts, release: true, quiet});
  bundle(projectRoot, buildDir, {quiet});

  step(`Installing ${info.name} ${info.version} into ${opts.stage}`);
  writePackagingFiles(info, path.join(buildDir, 'packaging'), {
    packageLicense: path.join(paths.PACKAGE_DIR, 'LICENSE'),
  });
  // A staging directory starts empty; a --prefix (Flatpak's /app) doesn't.
  if (!opts.prefix) fs.rmSync(opts.stage, {recursive: true, force: true});
  run('cmake', ['--install', buildDir, '--prefix', opts.stage, '--strip'], {quiet: true});
  // cmake --install --strip strips the executable; the host libraries (copied
  // files to CMake) only lost their debug info. Distributions want them
  // stripped of everything the dynamic linker doesn't need.
  const libDir = path.join(opts.stage, 'lib', info.name);
  if (which('strip') && fs.existsSync(libDir)) {
    for (const f of fs.readdirSync(libDir).filter(f => /\.so(\.|$)/.test(f))) {
      run('strip', ['--strip-unneeded', '--remove-section=.comment', '--remove-section=.note', path.join(libDir, f)], {quiet: true});
    }
  }
  const exe = path.join(opts.stage, 'bin', info.name);
  if (!fs.existsSync(exe)) throw new RunError(`Installed, but ${exe} is missing`);
  if (!fs.existsSync(path.join(opts.stage, 'share', info.name, 'index.bundle.js'))) {
    throw new RunError(
      `share/${info.name}/index.bundle.js wasn't installed: linux/CMakeLists.txt must call ` +
        `rngtk_app(${info.name}) (the package's CMake config installs the app)`,
    );
  }
  return {buildDir, stage: opts.stage, exe};
}

/** Lintian on a .deb, when it's installed (warnings don't fail the build). */
function lint(cmd, args, {log = console.log} = {}) {
  if (!which(cmd)) {
    log(`(${cmd} isn't installed: skipped)`);
    return null;
  }
  const r = spawnSync(cmd, args, {encoding: 'utf8'});
  const output = `${r.stdout || ''}${r.stderr || ''}`.trim();
  log(`${r.status === 0 && !/^E: /m.test(output) ? 'OK  ' : 'WARN'} ${cmd}${output ? `\n${output.replace(/^/gm, '     ')}` : ''}`);
  return {ok: r.status === 0, output};
}

function licenseTexts(info) {
  const read = f => (f && fs.existsSync(f) ? fs.readFileSync(f, 'utf8') : null);
  return {app: read(info.licenseFile), host: read(path.join(paths.PACKAGE_DIR, 'LICENSE'))};
}

/** --format deb: the tree under <root>/usr, then dpkg-deb. */
function packageDeb(projectRoot, opts) {
  const {info, output} = opts;
  const root = path.join(output, 'deb-root');
  fs.rmSync(root, {recursive: true, force: true});
  const staged = stageApp(projectRoot, {...opts, stage: path.join(root, 'usr')});
  step('Checking the desktop file and MetaInfo');
  validateMetadata(path.join(root, 'usr'), info);
  step('Building the .deb');
  const pkg = JSON.parse(fs.readFileSync(path.join(projectRoot, 'package.json'), 'utf8'));
  const deb = buildDeb(root, info, {
    output,
    run,
    pkg,
    scratch: path.join(output, 'deb-work'),
    date: releaseDate(),
    licenseTexts: licenseTexts(info),
  });
  console.log(`Depends: ${deb.depends}`);
  // (No man page, and no ITP bug to close: fine outside Debian's archive.)
  lint('lintian', ['--suppress-tags', 'no-manual-page,initial-upload-closes-no-bugs', deb.deb]);
  return {info, ...staged, format: 'deb', artifact: deb.deb, deb};
}

/** --format rpm: the tree under <root>/usr, then rpmbuild. */
function packageRpm(projectRoot, opts) {
  const {info, output} = opts;
  if (!which('rpmbuild')) {
    throw new RunError('rpmbuild is missing: sudo dnf install rpm-build (or apt install rpm)');
  }
  const root = path.join(output, 'rpm-root');
  fs.rmSync(root, {recursive: true, force: true});
  const staged = stageApp(projectRoot, {...opts, stage: path.join(root, 'usr')});
  step('Checking the desktop file and MetaInfo');
  validateMetadata(path.join(root, 'usr'), info);
  step('Building the .rpm');
  const pkg = JSON.parse(fs.readFileSync(path.join(projectRoot, 'package.json'), 'utf8'));
  const rpm = buildRpm(root, info, {
    output,
    run,
    scratch: path.join(output, 'rpm-work'),
    maintainer: maintainer(info, pkg),
    date: releaseDate(),
  });
  lint('rpmlint', ['-r', rpm.rpmlintrc, rpm.rpm]);
  return {info, ...staged, format: 'rpm', artifact: rpm.rpm, rpm};
}

async function packageLinux(projectRoot, options = {}) {
  const opts = {
    format: 'dir',
    output: path.join(projectRoot, 'linux', 'build', 'package'),
    logging: false,
    checks: true,
    smoke: false,
    libraries: [],
    ...options,
  };
  if (!FORMATS.includes(opts.format)) {
    throw new RunError(`Unknown format "${opts.format}" (one of: ${FORMATS.join(', ')})`);
  }
  if (!fs.existsSync(path.join(projectRoot, 'linux', 'CMakeLists.txt'))) {
    throw new RunError('No linux/CMakeLists.txt here. Run "npx react-native init-linux" first.');
  }
  const total = Date.now();
  const info = appInfo(projectRoot, {version: opts.version});
  const problems = validateAppInfo(info);
  if (problems.length) {
    throw new RunError(`app.json's "linux" block:\n${problems.map(p => `  - ${p}`).join('\n')}`);
  }
  const mismatch = checkMainAppId(projectRoot, info.appId);
  if (mismatch) console.warn(`warning: ${mismatch}`);
  console.log(`${info.displayName} (${info.appId}) ${info.version}, ${opts.format}`);

  if (opts.checks) {
    step('Checking prerequisites');
    const report = checkPrerequisites();
    formatReport(report).forEach(l => console.log(l));
    if (report.missing.length) throw new RunError('Missing prerequisites (above).');
  }

  const output = path.resolve(projectRoot, opts.output);
  if (opts.format === 'flatpak') {
    const result = buildFlatpak(projectRoot, {...opts, info, output});
    console.log(`\n${path.relative(projectRoot, result.artifact)} is ready (${elapsed(total)} in all)`);
    return result;
  }

  if (opts.format === 'rpm') {
    const result = packageRpm(projectRoot, {...opts, info, output});
    console.log(`\n${path.relative(projectRoot, result.artifact)} is ready (${elapsed(total)} in all)`);
    return result;
  }
  if (opts.format === 'deb') {
    const result = packageDeb(projectRoot, {...opts, info, output});
    console.log(`\n${path.relative(projectRoot, result.artifact)} is ready (${elapsed(total)} in all)`);
    return result;
  }

  // dir: <output>/<app>-<version>-<arch>/, or straight into --prefix.
  const stage = opts.prefix
    ? path.resolve(opts.prefix)
    : path.join(output, `${info.name}-${info.version}-${machineArch()}`);
  const staged = stageApp(projectRoot, {...opts, info, stage});
  step('Checking the desktop file and MetaInfo');
  validateMetadata(stage, info);
  if (opts.prefix) {
    console.log(`\nInstalled into ${stage} (${elapsed(total)} in all)`);
    return {info, ...staged, format: opts.format, artifact: stage};
  }
  const installer = path.join(stage, 'install.sh');
  fs.writeFileSync(installer, installScript(info, treeFiles(stage)), {mode: 0o755});

  const result = {info, ...staged, format: opts.format, artifact: stage};
  console.log(`\n${path.relative(projectRoot, result.artifact) || '.'} is ready (${elapsed(total)} in all)`);
  console.log(`Run it: ${path.relative(projectRoot, staged.exe)}`);
  console.log(`Install it (~/.local): ${path.relative(projectRoot, installer)}`);

  if (opts.smoke) {
    step(`Launching ${info.name} --smoke`);
    const r = spawnSync(staged.exe, ['--smoke'], {stdio: 'inherit', cwd: os.tmpdir()});
    if (r.status !== 0) throw new RunError(`${info.name} exited with status ${r.status}`);
  }
  return result;
}

module.exports = {packageLinux, FORMATS, machineArch, checkMainAppId, validateMetadata, installScript, treeFiles};
