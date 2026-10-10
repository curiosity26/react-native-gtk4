'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {describe, test} = require('node:test');

const {
  changelogFile,
  controlFile,
  copyrightFile,
  debDescription,
  debPackageName,
  debVersion,
  maintainer,
  md5sums,
} = require('../lib/cli/deb');

const info = {
  name: 'MyApp',
  appId: 'com.example.MyApp',
  displayName: 'My App',
  summary: 'Does one thing',
  description: 'A first paragraph that is long enough to need folding onto a second line of the Description field.\n\nSecond.',
  version: '1.2.0',
  license: 'MIT',
  homepage: 'https://example.com',
  developer: {id: 'com.example', name: 'Example'},
};

describe('deb', () => {
  test('package name and version', () => {
    assert.equal(debPackageName(info), 'myapp');
    assert.equal(debPackageName({name: 'My_App'}), 'my-app');
    assert.equal(debPackageName({...info, deb: {package: 'example-app'}}), 'example-app');
    assert.equal(debVersion(info), '1.2.0-1');
    assert.equal(debVersion({version: '2.0.0-beta.1', deb: {revision: 3}}), '2.0.0-beta.1-3');
  });

  test('maintainer: app.json, DEBEMAIL, package.json author', () => {
    assert.equal(maintainer({...info, maintainer: 'A <a@b.c>'}), 'A <a@b.c>');
    assert.equal(maintainer(info, {}, {DEBEMAIL: 'x@y.z', DEBFULLNAME: 'X'}), 'X <x@y.z>');
    assert.equal(maintainer(info, {author: 'Pat <pat@example.com> (https://pat.dev)'}, {}), 'Pat <pat@example.com>');
    assert.equal(maintainer(info, {author: {name: 'Sam', email: 'sam@example.com'}}, {}), 'Sam <sam@example.com>');
  });

  test('the Description field folds paragraphs, with " ." between them', () => {
    const d = debDescription(info).split('\n');
    assert.equal(d[0], 'Does one thing');
    assert.ok(d.slice(1).every(l => l.startsWith(' ') && l.length <= 77));
    assert.ok(d.includes(' .'));
    assert.equal(d.at(-1), ' Second.');
    assert.equal(debDescription({summary: 'Only'}), 'Only\n Only');
  });

  test('control, copyright, changelog', () => {
    const control = controlFile(info, {package: 'myapp', version: '1.2.0-1', arch: 'arm64', depends: 'libc6 (>= 2.34), libgtk-4-1', installedSize: 41000, maintainer: 'A <a@b.c>'});
    assert.match(control, /^Package: myapp\nVersion: 1\.2\.0-1\nArchitecture: arm64\nMaintainer: A <a@b\.c>\nInstalled-Size: 41000\nDepends: libc6/);
    assert.match(control, /\nHomepage: https:\/\/example\.com\nDescription: Does one thing\n /);
    const copyright = copyrightFile(info, {licenseTexts: {app: 'MIT License\n\nText.\n', host: 'MIT\n'}});
    assert.match(copyright, /^Format: https:\/\/www\.debian\.org\/doc\/packaging-manuals\/copyright-format\/1\.0\//);
    assert.match(copyright, /License: MIT\n MIT License\n \.\n Text\./);
    assert.match(copyright, /Files: usr\/lib\/MyApp\/\*/);
    const changelog = changelogFile(info, {package: 'myapp', version: '1.2.0-1', maintainer: 'A <a@b.c>', date: '2026-10-09'});
    assert.equal(changelog, 'myapp (1.2.0-1) unstable; urgency=medium\n\n  * Release 1.2.0.\n\n -- A <a@b.c>  Fri, 09 Oct 2026 00:00:00 +0000\n');
  });

  test('md5sums lists files relative to the root, without DEBIAN/', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-deb-'));
    try {
      fs.mkdirSync(path.join(root, 'usr', 'bin'), {recursive: true});
      fs.mkdirSync(path.join(root, 'DEBIAN'));
      fs.writeFileSync(path.join(root, 'usr', 'bin', 'MyApp'), 'x');
      fs.writeFileSync(path.join(root, 'DEBIAN', 'control'), 'c');
      assert.equal(md5sums(root), '9dd4e461268c8034f5c8564e155c67a6  usr/bin/MyApp\n');
    } finally {
      fs.rmSync(root, {recursive: true, force: true});
    }
  });
});

describe('rpm', () => {
  const {rpmName, rpmVersion, rpmSpec, changelogDate} = require('../lib/cli/rpm');

  test('name, version and changelog date', () => {
    assert.equal(rpmName(info), 'myapp');
    assert.equal(rpmName({...info, rpm: {name: 'my-app'}}), 'my-app');
    assert.equal(rpmVersion('1.0.0-beta.1'), '1.0.0~beta.1');
    assert.equal(changelogDate('2026-10-09'), 'Fri Oct 09 2026');
  });

  test('the spec keeps the host libraries private and lists the tree', () => {
    const spec = rpmSpec(info, {maintainer: 'A <a@b.c>', date: '2026-10-09'});
    assert.match(spec, /^%global __provides_exclude_from \^%\{_prefix\}\/lib\/MyApp\/\.\*\$$/m);
    assert.match(spec, /^%global __requires_exclude \^\(librngtk_host\|libhermesvm\|libjsi\)\\\\\.so\.\*\$$/m);
    assert.match(spec, /^Name: +myapp$/m);
    assert.match(spec, /^Version: +1\.2\.0$/m);
    assert.match(spec, /^License: +MIT$/m);
    assert.match(spec, /^%\{_datadir\}\/applications\/com\.example\.MyApp\.desktop$/m);
    assert.match(spec, /^%license %\{_datadir\}\/licenses\/com\.example\.MyApp\/$/m);
    assert.match(spec, /^\* Fri Oct 09 2026 A <a@b\.c> - 1\.2\.0-1$/m);
  });
});

describe('deb: web views', () => {
  const {apparmorProfile, apparmorScripts} = require('../lib/cli/deb');

  test("an AppArmor profile like Ubuntu's for Epiphany, for WebKit's sandbox", () => {
    const profile = apparmorProfile('myapp', '/usr/bin/MyApp');
    assert.match(profile, /^profile myapp \/usr\/bin\/MyApp flags=\(unconfined\) \{\n {2}userns,$/m);
    assert.match(profile, /include if exists <local\/myapp>/);
    const {postinst, postrm} = apparmorScripts('myapp');
    assert.match(postinst, /apparmor_parser -r -T -W \/etc\/apparmor\.d\/myapp/);
    assert.match(postrm, /apparmor_parser -R -T -W \/etc\/apparmor\.d\/myapp/);
  });
});
