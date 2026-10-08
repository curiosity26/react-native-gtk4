'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const {describe, test} = require('node:test');

const {
  checkPrerequisites,
  compareVersions,
  detectDistro,
  formatReport,
  installHint,
  parseOsRelease,
  parseVersion,
} = require('../lib/cli/prereqs');
const {appArgs, terminalCommand} = require('../lib/cli/runLinux');
const paths = require('../lib/cli/paths');

const OS_RELEASE = {
  ubuntu: 'PRETTY_NAME="Ubuntu 24.04.5 LTS"\nNAME="Ubuntu"\nVERSION_ID="24.04"\nID=ubuntu\nID_LIKE=debian\n',
  mint: 'NAME="Linux Mint"\nPRETTY_NAME="Linux Mint 22.3"\nID=linuxmint\nID_LIKE="ubuntu debian"\n',
  debian: 'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"\nID=debian\n',
  fedora: 'NAME="Fedora Linux"\nPRETTY_NAME="Fedora Linux 42 (Workstation Edition)"\nID=fedora\n',
  arch: 'NAME="Arch Linux"\nPRETTY_NAME="Arch Linux"\nID=arch\n',
};

// Tool output as Ubuntu 24.04 prints it.
const UBUNTU_TOOLS = {
  'clang --version': 'Ubuntu clang version 18.1.3 (1ubuntu1)\nTarget: aarch64-unknown-linux-gnu\n',
  'cmake --version': 'cmake version 3.28.3\n\nCMake suite maintained and supported by Kitware\n',
  'ninja --version': '1.11.1\n',
  'pkg-config --version': '1.8.1\n',
  'git --version': 'git version 2.43.0\n',
  'python3 --version': 'Python 3.12.3\n',
  'pkg-config --modversion gtk4': '4.14.5\n',
  'pkg-config --modversion libsoup-3.0': '3.4.4\n',
  'pkg-config --modversion openssl': '3.0.13\n',
  'pkg-config --modversion icu-uc': '74.2\n',
  'pkg-config --modversion readline': '8.2\n',
};

function fakeRunner(outputs) {
  return (cmd, args) => {
    const key = [cmd, ...args].join(' ');
    return key in outputs ? {ok: true, stdout: outputs[key], stderr: ''} : {ok: false, stdout: '', stderr: 'not found'};
  };
}

describe('parsing', () => {
  test('os-release', () => {
    assert.deepEqual(parseOsRelease(OS_RELEASE.mint), {
      NAME: 'Linux Mint',
      PRETTY_NAME: 'Linux Mint 22.3',
      ID: 'linuxmint',
      ID_LIKE: 'ubuntu debian',
    });
  });

  test('distributions map to package managers', () => {
    const manager = name => detectDistro(parseOsRelease(OS_RELEASE[name])).manager;
    assert.equal(manager('ubuntu'), 'apt');
    assert.equal(manager('mint'), 'apt');
    assert.equal(manager('debian'), 'apt');
    assert.equal(manager('fedora'), 'dnf');
    assert.equal(manager('arch'), null);
  });

  test('versions', () => {
    assert.equal(parseVersion('Ubuntu clang version 18.1.3 (1ubuntu1)'), '18.1.3');
    assert.equal(parseVersion('cmake version 3.28.3'), '3.28.3');
    assert.equal(parseVersion('74.2'), '74.2.0');
    assert.equal(parseVersion('v24.21.0'), '24.21.0');
    assert.equal(parseVersion('no digits'), null);
    assert.ok(compareVersions('4.14.0', '4.8.3') > 0);
    assert.ok(compareVersions('22.12.9', '22.13.0') < 0);
    assert.equal(compareVersions('3.22.0', '3.22.0'), 0);
  });

  test('install hints', () => {
    assert.equal(installHint('apt', ['gtk4', 'ninja']), 'sudo apt install -y libgtk-4-dev ninja-build');
    assert.equal(installHint('dnf', ['libsoup-3.0', 'pkg-config']), 'sudo dnf install -y libsoup3-devel pkgconf-pkg-config');
    assert.equal(installHint('apt', []), null);
  });
});

describe('checkPrerequisites', () => {
  test('a complete Ubuntu 24.04', () => {
    const r = checkPrerequisites({
      run: fakeRunner(UBUNTU_TOOLS),
      nodeVersion: '24.21.0',
      osRelease: parseOsRelease(OS_RELEASE.ubuntu),
    });
    assert.deepEqual(r.missing, []);
    assert.equal(r.hint, null);
    assert.equal(r.results.find(x => x.key === 'gtk4').version, '4.14.5');
    assert.ok(formatReport(r).every(l => l.startsWith('  ok')));
  });

  test("Ubuntu without libsoup and with apt's Node 18", () => {
    const tools = {...UBUNTU_TOOLS};
    delete tools['pkg-config --modversion libsoup-3.0'];
    const r = checkPrerequisites({
      run: fakeRunner(tools),
      nodeVersion: '18.19.1',
      osRelease: parseOsRelease(OS_RELEASE.ubuntu),
    });
    assert.deepEqual(r.missing, ['libsoup-3.0', 'node']);
    assert.equal(r.hint, 'sudo apt install -y libsoup-3.0-dev');
    const report = formatReport(r).join('\n');
    assert.match(report, /MISS Node\.js 18\.19\.1 \(need >= 22\.13\.0\)/);
    assert.match(report, /nodejs\.org/);
    assert.match(report, /On Ubuntu 24\.04\.5 LTS, install the missing packages with:\n {2}sudo apt install -y libsoup-3\.0-dev/);
  });

  test('Debian 12: GTK too old; Fedora: dnf names', () => {
    const old = checkPrerequisites({
      run: fakeRunner({...UBUNTU_TOOLS, 'pkg-config --modversion gtk4': '4.8.3\n'}),
      nodeVersion: '22.13.0',
      osRelease: parseOsRelease(OS_RELEASE.debian),
    });
    assert.deepEqual(old.missing, ['gtk4']);
    assert.match(formatReport(old).join('\n'), /MISS GTK 4 4\.8\.3 \(need >= 4\.14\.0\)[\s\S]*Ubuntu 24\.04/);

    const fedora = checkPrerequisites({
      run: fakeRunner({}),
      nodeVersion: '24.0.0',
      osRelease: parseOsRelease(OS_RELEASE.fedora),
    });
    assert.equal(
      fedora.hint,
      'sudo dnf install -y clang cmake ninja-build pkgconf-pkg-config git python3 gtk4-devel ' +
        'libsoup3-devel openssl-devel libicu-devel readline-devel',
    );
  });

  test('unknown distributions get a generic hint', () => {
    const r = checkPrerequisites({
      run: fakeRunner({}),
      nodeVersion: '24.0.0',
      osRelease: parseOsRelease(OS_RELEASE.arch),
    });
    assert.equal(r.hint, null);
    assert.match(formatReport(r).join('\n'), /development packages for Arch Linux/);
  });
});

describe('run-linux helpers', () => {
  test('app arguments', () => {
    assert.deepEqual(appArgs({release: false, port: 8090}), ['--dev-server', 'localhost:8090']);
    assert.deepEqual(appArgs({release: true, smoke: true, screenshot: '/tmp/a.png'}), [
      '--smoke',
      '--screenshot',
      '/tmp/a.png',
    ]);
  });

  test('terminal commands', () => {
    assert.deepEqual(terminalCommand('/usr/bin/gnome-terminal', 'npx x'), [
      '/usr/bin/gnome-terminal',
      ['--', 'sh', '-c', 'npx x'],
    ]);
    assert.deepEqual(terminalCommand('/usr/bin/xterm', "cd 'a b' && x"), [
      '/usr/bin/xterm',
      ['-e', `sh -c 'cd '\\''a b'\\'' && x'`],
    ]);
  });

  test('cache locations', () => {
    const rn = paths.reactNativeVersion();
    assert.match(rn, /^\d+\.\d+\.\d+$/);
    assert.equal(paths.cacheRoot({HOME: '/h', XDG_CACHE_HOME: '/x'}), '/x/react-native-gtk4');
    assert.equal(paths.cacheRoot({RNGTK_CACHE_DIR: '/c'}), '/c');
    assert.equal(paths.depsDir({RNGTK_CACHE_DIR: '/c'}), path.join('/c', rn, 'deps'));
    assert.equal(paths.depsDir({RNGTK_DEPS_DIR: '/d'}), '/d');
    assert.match(paths.hostDir({RNGTK_CACHE_DIR: '/c'}), new RegExp(`^/c/${rn.replace(/\./g, '\\.')}/host/[0-9a-f]{12}$`));
    assert.equal(paths.hostSourceId(), paths.hostSourceId());
  });
});
