'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {afterEach, beforeEach, describe, test} = require('node:test');

const {
  appInfo,
  desktopEntry,
  metainfo,
  releaseDate,
  validateAppInfo,
  writePackagingFiles,
} = require('../lib/cli/appInfo');
const {checkMainAppId, machineArch} = require('../lib/cli/packageLinux');
const {editAppJson, appNames} = require('../lib/cli/initLinux');

// A 1x1 PNG header with the given size (only IHDR is read).
function fakePng(width, height) {
  const b = Buffer.alloc(33);
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]).copy(b, 0);
  b.writeUInt32BE(13, 8);
  b.write('IHDR', 12, 'ascii');
  b.writeUInt32BE(width, 16);
  b.writeUInt32BE(height, 20);
  return b;
}

describe('appInfo', () => {
  let root;
  beforeEach(() => {
    root = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-pkg-'));
    fs.writeFileSync(
      path.join(root, 'package.json'),
      JSON.stringify({name: 'myapp', version: '1.2.3', license: 'MIT', repository: 'git+https://github.com/me/my-app.git'}),
    );
  });
  afterEach(() => fs.rmSync(root, {recursive: true, force: true}));

  test('defaults come from app.json and package.json', () => {
    fs.writeFileSync(path.join(root, 'app.json'), JSON.stringify({name: 'MyApp', displayName: 'My App'}));
    fs.writeFileSync(path.join(root, 'LICENSE'), 'MIT\n');
    const info = appInfo(root);
    assert.equal(info.name, 'MyApp');
    assert.equal(info.appId, 'com.myapp');
    assert.equal(info.displayName, 'My App');
    assert.equal(info.version, '1.2.3');
    assert.equal(info.license, 'MIT');
    assert.equal(info.licenseFile, path.join(root, 'LICENSE'));
    assert.equal(info.homepage, 'https://github.com/me/my-app');
    assert.deepEqual(info.categories, ['Utility']);
    assert.equal(info.icon, null);
    assert.equal(appInfo(root, {version: '2.0.0'}).version, '2.0.0');
  });

  test("the linux block wins, and the icon is relative to the project", () => {
    fs.writeFileSync(
      path.join(root, 'app.json'),
      JSON.stringify({
        name: 'MyApp',
        linux: {appId: 'org.example.MyApp', displayName: 'Mine', icon: 'linux/icon.svg', version: '3.0', license: 'GPL-3.0-or-later'},
      }),
    );
    const info = appInfo(root);
    assert.equal(info.appId, 'org.example.MyApp');
    assert.equal(info.displayName, 'Mine');
    assert.equal(info.icon, path.join(root, 'linux', 'icon.svg'));
    assert.equal(info.version, '3.0');
    assert.equal(info.license, 'GPL-3.0-or-later');
    assert.equal(appNames(root).appId, 'org.example.MyApp');
  });

  test('validateAppInfo reports what would make a broken package', () => {
    fs.writeFileSync(path.join(root, 'app.json'), JSON.stringify({name: 'MyApp', linux: {appId: 'bad', categories: ['GTK']}}));
    const problems = validateAppInfo(appInfo(root, {version: 'v1'}));
    assert.equal(problems.length, 4);
    assert.match(problems[0], /reverse-DNS/);
    assert.match(problems[1], /no icon/);
    assert.match(problems[2], /main ones/);
    assert.match(problems[3], /start with a digit/);
  });
});

describe('desktop entry and MetaInfo', () => {
  const info = {
    name: 'MyApp',
    appId: 'org.example.MyApp',
    displayName: 'My <App> & Co',
    summary: 'Does one thing',
    description: 'First paragraph\nwraps.\n\n- one\n- two',
    categories: ['Utility', 'GTK'],
    keywords: ['thing'],
    version: '1.0.0',
    license: 'MIT',
    homepage: 'https://example.com',
    developer: {id: 'org.example', name: 'Example'},
    screenshots: [{url: 'https://example.com/a.png', caption: 'Main'}, 'https://example.com/b.png'],
    releases: [{version: '0.9.0', date: '2026-01-01', description: 'Beta'}],
  };

  test('desktopEntry', () => {
    assert.equal(
      desktopEntry(info),
      [
        '[Desktop Entry]',
        'Type=Application',
        'Name=My <App> & Co',
        'Comment=Does one thing',
        'Exec=MyApp',
        'Icon=org.example.MyApp',
        'Terminal=false',
        'Categories=Utility;GTK;',
        'Keywords=thing;',
        'StartupNotify=true',
        'X-GNOME-UsesNotifications=true',
        '',
      ].join('\n'),
    );
  });

  test('metainfo', () => {
    const xml = metainfo(info, {date: '2026-10-09'});
    assert.match(xml, /<id>org\.example\.MyApp<\/id>/);
    assert.match(xml, /<name>My &lt;App&gt; &amp; Co<\/name>/);
    assert.match(xml, /<p>First paragraph wraps\.<\/p>\n {4}<ul>\n {6}<li>one<\/li>\n {6}<li>two<\/li>\n {4}<\/ul>/);
    assert.match(xml, /<developer id="org\.example">\n {4}<name>Example<\/name>/);
    assert.match(xml, /<launchable type="desktop-id">org\.example\.MyApp\.desktop<\/launchable>/);
    assert.match(xml, /<screenshot type="default">\n {6}<caption>Main<\/caption>\n {6}<image>https:\/\/example\.com\/a\.png<\/image>/);
    assert.match(xml, /<screenshot>\n {6}<image>https:\/\/example\.com\/b\.png<\/image>/);
    // The current version first, then the listed ones.
    assert.match(xml, /<release version="1\.0\.0" date="2026-10-09"\/>\n {4}<release version="0\.9\.0" date="2026-01-01">/);
    assert.match(xml, /<content_rating type="oars-1\.1"\/>/);
  });

  test('releaseDate follows SOURCE_DATE_EPOCH', () => {
    assert.equal(releaseDate({SOURCE_DATE_EPOCH: '1767225600'}), '2026-01-01');
    assert.match(releaseDate({}), /^\d{4}-\d\d-\d\d$/);
  });
});

describe('writePackagingFiles', () => {
  let dir;
  beforeEach(() => (dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-share-'))));
  afterEach(() => fs.rmSync(dir, {recursive: true, force: true}));

  const base = {
    name: 'MyApp',
    appId: 'com.myapp',
    displayName: 'MyApp',
    summary: 'An app',
    description: '',
    categories: ['Utility'],
    keywords: [],
    version: '1.0.0',
    license: 'MIT',
    homepage: null,
    developer: null,
    screenshots: [],
    releases: [],
  };

  test('lays out share/ for the CMake install step', () => {
    const icon = path.join(dir, 'icon.svg');
    fs.copyFileSync(path.join(__dirname, '..', 'template', 'linux', 'icon.svg'), icon);
    const license = path.join(dir, 'COPYING');
    fs.writeFileSync(license, 'license\n');
    const out = path.join(dir, 'packaging');
    writePackagingFiles({...base, icon, licenseFile: license}, out, {packageLicense: license});
    const share = path.join(out, 'share');
    for (const f of [
      'applications/com.myapp.desktop',
      'metainfo/com.myapp.metainfo.xml',
      'icons/hicolor/scalable/apps/com.myapp.svg',
      'licenses/com.myapp/COPYING',
      'licenses/com.myapp/react-native-gtk4/LICENSE',
    ]) {
      assert.ok(fs.existsSync(path.join(share, f)), f);
    }
    // No stray temporary directories.
    assert.deepEqual(fs.readdirSync(share).sort(), ['applications', 'icons', 'licenses', 'metainfo']);
  });

  test('rejects a PNG icon that is not square or too small', () => {
    const icon = path.join(dir, 'icon.png');
    fs.writeFileSync(icon, fakePng(256, 128));
    assert.throws(() => writePackagingFiles({...base, icon}, path.join(dir, 'p')), /must be square/);
    fs.writeFileSync(icon, fakePng(64, 64));
    assert.throws(() => writePackagingFiles({...base, icon}, path.join(dir, 'p')), /128px or more/);
  });
});

describe('package-linux helpers', () => {
  test('machineArch', () => {
    assert.equal(machineArch('arm64'), 'aarch64');
    assert.equal(machineArch('x64'), 'x86_64');
  });

  test("checkMainAppId notices a main.cc whose id isn't app.json's", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-main-'));
    try {
      fs.mkdirSync(path.join(root, 'linux'));
      fs.writeFileSync(path.join(root, 'linux', 'main.cc'), 'options.appId = "com.myapp";\n');
      assert.equal(checkMainAppId(root, 'com.myapp'), null);
      assert.match(checkMainAppId(root, 'org.example.MyApp'), /make them the same/);
    } finally {
      fs.rmSync(root, {recursive: true, force: true});
    }
  });

  test('editAppJson adds the linux block once', () => {
    const names = {moduleName: 'MyApp', name: 'MyApp', title: 'My App', appId: 'com.myapp'};
    const first = editAppJson('{\n    "name": "MyApp"\n}\n', names);
    assert.match(first.text, /^ {4}"linux": \{/m);
    assert.equal(editAppJson(first.text, names).changes.length, 0);
    const created = editAppJson(null, names);
    assert.deepEqual(JSON.parse(created.text).name, 'MyApp');
  });
});

describe('install.sh', () => {
  test('installs into a prefix with an absolute Exec, and uninstalls', () => {
    const {installScript} = require('../lib/cli/packageLinux');
    const {spawnSync} = require('node:child_process');
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-install-'));
    try {
      const tree = path.join(dir, 'tree');
      const files = {
        'bin/MyApp': '#!/bin/sh\n',
        'lib/MyApp/librngtk_host.so': '',
        'share/MyApp/index.bundle.js': '',
        'share/applications/com.myapp.desktop': '[Desktop Entry]\nExec=MyApp\n',
        'share/icons/hicolor/48x48/apps/com.myapp.png': '',
      };
      for (const [f, text] of Object.entries(files)) {
        fs.mkdirSync(path.dirname(path.join(tree, f)), {recursive: true});
        fs.writeFileSync(path.join(tree, f), text);
      }
      const script = path.join(tree, 'install.sh');
      fs.writeFileSync(script, installScript({name: 'MyApp', appId: 'com.myapp', displayName: 'My App', version: '1.0'}, Object.keys(files)), {mode: 0o755});
      const prefix = path.join(dir, 'prefix');
      assert.equal(spawnSync(script, [prefix]).status, 0);
      assert.match(fs.readFileSync(path.join(prefix, 'share/applications/com.myapp.desktop'), 'utf8'), new RegExp(`Exec=${prefix}/bin/MyApp`));
      assert.ok(fs.statSync(path.join(prefix, 'bin/MyApp')).mode & 0o100);
      assert.equal(spawnSync(script, ['--uninstall', prefix]).status, 0);
      // (update-desktop-database's cache stays.)
      const left = fs.readdirSync(prefix, {recursive: true}).filter(f => fs.statSync(path.join(prefix, f)).isFile());
      assert.deepEqual(left.filter(f => !f.endsWith('mimeinfo.cache')), []);
      assert.ok(!fs.existsSync(path.join(prefix, 'share/icons/hicolor/48x48')));
    } finally {
      fs.rmSync(dir, {recursive: true, force: true});
    }
  });
});
