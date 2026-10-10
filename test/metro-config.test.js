'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {afterEach, beforeEach, describe, test} = require('node:test');

const {
  OVERRIDES_DIR,
  createLinuxResolver,
  withLinux,
} = require('../metro-config');

// A fake app with a react-native package laid out like RN 0.87's, and a
// stand-in for Metro's resolver: exact file, then platform extensions.
let app;
let rn;

function write(file, contents = '') {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, contents);
}

function metroResolve(context, moduleName, platform) {
  let base;
  if (moduleName.startsWith('.')) {
    base = path.resolve(path.dirname(context.originModulePath), moduleName);
  } else {
    let dir = path.dirname(context.originModulePath);
    for (;;) {
      const candidate = path.join(dir, 'node_modules', moduleName);
      if (fs.existsSync(path.dirname(candidate))) {
        base = candidate;
        break;
      }
      if (dir === path.dirname(dir)) break;
      dir = path.dirname(dir);
    }
  }
  if (base) {
    for (const ext of [`.${platform}.js`, '.native.js', '.js', '', '/index.js']) {
      const file = base + ext;
      if (fs.existsSync(file) && fs.statSync(file).isFile()) {
        return {type: 'sourceFile', filePath: file};
      }
    }
  }
  const error = new Error(`Unable to resolve ${moduleName}`);
  error.code = 'UNRESOLVED';
  throw error;
}

function resolve(from, moduleName, platform = 'linux', options = {}) {
  const resolver = createLinuxResolver({projectRoot: app, ...options});
  return resolver(
    {originModulePath: from, resolveRequest: metroResolve},
    moduleName,
    platform,
  );
}

const override = rel => path.join(OVERRIDES_DIR, rel);

beforeEach(() => {
  app = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'rngtk-')));
  rn = path.join(app, 'node_modules', 'react-native');
  write(path.join(app, 'package.json'), '{}');
  write(path.join(app, 'App.js'));
  write(path.join(rn, 'package.json'), '{"name":"react-native"}');
  write(path.join(rn, 'index.js'));
  const lib = rel => path.join(rn, 'Libraries', rel);
  // Platform-split with a legacy platform-less shim (has an override).
  for (const ext of ['js', 'ios.js', 'android.js']) {
    write(lib(`Utilities/Platform.${ext}`));
    write(lib(`Utilities/BackHandler.${ext}`));
    // Split, but no override in this package.
    write(lib(`Fake/Split.${ext}`));
  }
  // Only platform variants, no platform-less file.
  write(lib('Fake/OnlyVariants.ios.js'));
  write(lib('Fake/OnlyVariants.android.js'));
  // A real platform-less implementation next to a single variant.
  write(lib('Settings/Settings.js'));
  write(lib('Settings/Settings.ios.js'));
  write(lib('Utilities/Plain.js'));
  // A package nested inside react-native is third-party code.
  write(path.join(rn, 'node_modules', 'dep', 'Thing.js'));
  write(path.join(rn, 'node_modules', 'dep', 'Thing.ios.js'));
  write(path.join(rn, 'node_modules', 'dep', 'Thing.android.js'));
  // A third-party package with platform variants only.
  write(path.join(app, 'node_modules', 'other', 'index.ios.js'));
  write(path.join(app, 'node_modules', 'other', 'index.android.js'));
});

afterEach(() => {
  fs.rmSync(app, {recursive: true, force: true});
});

describe('createLinuxResolver', () => {
  test('uses the Platform override for relative imports inside react-native', () => {
    const from = path.join(rn, 'Libraries', 'Utilities', 'Thing.js');
    assert.deepEqual(resolve(from, './Platform'), {
      type: 'sourceFile',
      filePath: override('Libraries/Utilities/Platform.linux.js'),
    });
  });

  test('uses overrides for deep imports of react-native', () => {
    assert.equal(
      resolve(path.join(app, 'App.js'), 'react-native/Libraries/Utilities/BackHandler')
        .filePath,
      override('Libraries/Utilities/BackHandler.linux.js'),
    );
  });

  test('falls back to the android variant of a split module without an override', () => {
    const from = path.join(rn, 'index.js');
    assert.equal(
      resolve(from, './Libraries/Fake/Split').filePath,
      path.join(rn, 'Libraries/Fake/Split.android.js'),
    );
    assert.equal(
      resolve(from, './Libraries/Fake/OnlyVariants').filePath,
      path.join(rn, 'Libraries/Fake/OnlyVariants.android.js'),
    );
  });

  test('keeps a real platform-less implementation', () => {
    const from = path.join(rn, 'index.js');
    assert.equal(
      resolve(from, './Libraries/Settings/Settings').filePath,
      path.join(rn, 'Libraries/Settings/Settings.js'),
    );
    assert.equal(
      resolve(from, './Libraries/Utilities/Plain').filePath,
      path.join(rn, 'Libraries/Utilities/Plain.js'),
    );
  });

  test('leaves explicit platform variants alone', () => {
    assert.equal(
      resolve(path.join(rn, 'index.js'), './Libraries/Utilities/BackHandler.ios')
        .filePath,
      path.join(rn, 'Libraries/Utilities/BackHandler.ios.js'),
    );
  });

  test('resolves react-native imports in overrides from the app', () => {
    const from = override('Libraries/Utilities/BackHandler.linux.js');
    for (const prefix of ['react-native-upstream', 'react-native']) {
      assert.equal(
        resolve(from, `${prefix}/Libraries/Utilities/BackHandler.ios`).filePath,
        path.join(rn, 'Libraries/Utilities/BackHandler.ios.js'),
      );
    }
    assert.equal(resolve(from, 'react-native').filePath, path.join(rn, 'index.js'));
  });

  test("resolves the package's own JS imports (js/) from the app", () => {
    const from = path.join(OVERRIDES_DIR, '..', 'js', 'Dialogs.js');
    assert.equal(resolve(from, 'react-native').filePath, path.join(rn, 'index.js'));
  });

  test('does not touch third-party packages', () => {
    assert.throws(
      () => resolve(path.join(app, 'App.js'), 'other'),
      {code: 'UNRESOLVED'},
    );
    assert.equal(
      resolve(path.join(rn, 'index.js'), './node_modules/dep/Thing').filePath,
      path.join(rn, 'node_modules/dep/Thing.js'),
    );
  });

  test("uses a listed package's pure-JS variant for another platform", () => {
    const sac = path.join(app, 'node_modules', 'react-native-safe-area-context', 'src');
    write(path.join(sac, 'SafeAreaContext.js'));
    write(path.join(sac, 'NativeSafeAreaProvider.js'));
    write(path.join(sac, 'NativeSafeAreaProvider.windows.tsx'));
    write(path.join(sac, 'InitialWindow.js'));
    const from = path.join(sac, 'SafeAreaContext.js');
    assert.equal(
      resolve(from, './NativeSafeAreaProvider').filePath,
      path.join(sac, 'NativeSafeAreaProvider.windows.tsx'),
    );
    // No variant: unchanged.
    assert.equal(resolve(from, './InitialWindow').filePath, path.join(sac, 'InitialWindow.js'));
    // Other platforms and unlisted packages: unchanged.
    assert.equal(
      resolve(from, './NativeSafeAreaProvider', 'android').filePath,
      path.join(sac, 'NativeSafeAreaProvider.js'),
    );
    const other = path.join(app, 'node_modules', 'unlisted');
    write(path.join(other, 'A.js'));
    write(path.join(other, 'A.windows.js'));
    assert.equal(resolve(path.join(other, 'B.js'), './A').filePath, path.join(other, 'A.js'));
  });

  test('web packages: .web variants, and the platform-less file over .native', () => {
    const rns = path.join(app, 'node_modules', 'react-native-screens', 'src');
    write(path.join(rns, 'index.js'));
    write(path.join(rns, 'ScreenStack.js'));
    write(path.join(rns, 'ScreenStack.web.js'));
    write(path.join(rns, 'core.js'));
    const stack = path.join(app, 'node_modules', '@react-navigation', 'native-stack', 'src');
    write(path.join(stack, 'index.js'));
    write(path.join(stack, 'NativeStackView.native.js'));
    write(path.join(stack, 'NativeStackView.js'));
    write(path.join(stack, 'Only.native.js'));
    assert.equal(
      resolve(path.join(rns, 'index.js'), './ScreenStack').filePath,
      path.join(rns, 'ScreenStack.web.js'),
    );
    assert.equal(resolve(path.join(rns, 'index.js'), './core').filePath, path.join(rns, 'core.js'));
    const from = path.join(stack, 'index.js');
    assert.equal(resolve(from, './NativeStackView').filePath, path.join(stack, 'NativeStackView.js'));
    // No web file to use instead: unchanged.
    assert.equal(resolve(from, './Only').filePath, path.join(stack, 'Only.native.js'));
    // Only platform variants (no platform-less file): the web one.
    write(path.join(rns, 'TabsHost.ios.js'));
    write(path.join(rns, 'TabsHost.web.js'));
    assert.equal(
      resolve(path.join(rns, 'index.js'), './TabsHost').filePath,
      path.join(rns, 'TabsHost.web.js'),
    );
  });

  test("with the screens port: the libraries' native JS, the port's overrides", () => {
    const rns = path.join(app, 'node_modules', 'react-native-screens', 'src');
    write(path.join(rns, '..', 'package.json'), '{"name":"react-native-screens"}');
    write(path.join(rns, 'index.js'));
    write(path.join(rns, 'core.js'));
    write(path.join(rns, 'ScreenStack.js'));
    write(path.join(rns, 'ScreenStack.web.js'));
    write(path.join(rns, 'TabsHost.ios.js'));
    write(path.join(rns, 'TabsHost.web.js'));
    const stack = path.join(app, 'node_modules', '@react-navigation', 'native-stack', 'src');
    write(path.join(stack, 'index.js'));
    write(path.join(stack, 'NativeStackView.native.js'));
    write(path.join(stack, 'NativeStackView.js'));
    const port = path.join(app, 'node_modules', '@curiosity26', 'react-native-gtk4-screens');
    write(path.join(port, 'package.json'), '{}');
    write(path.join(port, 'overrides', 'src', 'core.ts'));
    const from = path.join(rns, 'index.js');
    assert.equal(resolve(from, './ScreenStack').filePath, path.join(rns, 'ScreenStack.js'));
    assert.equal(resolve(from, './core').filePath, path.join(port, 'overrides', 'src', 'core.ts'));
    // Native tabs aren't ported: their web variant.
    assert.equal(resolve(from, './TabsHost').filePath, path.join(rns, 'TabsHost.web.js'));
    assert.equal(
      resolve(path.join(stack, 'index.js'), './NativeStackView').filePath,
      path.join(stack, 'NativeStackView.native.js'),
    );
    // An override's imports resolve from the library's file.
    assert.equal(
      resolve(path.join(port, 'overrides', 'src', 'core.ts'), './ScreenStack').filePath,
      path.join(rns, 'ScreenStack.js'),
    );
    // RNGTK_IGNORE_PORTS: as if it weren't installed.
    process.env.RNGTK_IGNORE_PORTS = '@curiosity26/react-native-gtk4-screens';
    try {
      assert.equal(resolve(from, './ScreenStack').filePath, path.join(rns, 'ScreenStack.web.js'));
      assert.equal(resolve(from, './core').filePath, path.join(rns, 'core.js'));
    } finally {
      delete process.env.RNGTK_IGNORE_PORTS;
    }
  });

  test("react-native-webview gets its iOS JS only with the Linux port installed", () => {
    const wv = path.join(app, 'node_modules', 'react-native-webview', 'src');
    write(path.join(wv, 'index.js'));
    write(path.join(wv, 'WebView.js'));
    write(path.join(wv, 'WebView.ios.js'));
    const from = path.join(wv, 'index.js');
    // Without the port: the library's own "not supported" WebView.
    assert.equal(resolve(from, './WebView').filePath, path.join(wv, 'WebView.js'));
    write(path.join(app, 'node_modules', '@curiosity26', 'react-native-gtk4-webview', 'package.json'), '{}');
    assert.equal(resolve(from, './WebView').filePath, path.join(wv, 'WebView.ios.js'));
  });

    test('does nothing for other platforms', () => {
    const from = path.join(rn, 'Libraries', 'Utilities', 'Thing.js');
    assert.equal(
      resolve(from, './Platform', 'android').filePath,
      path.join(rn, 'Libraries/Utilities/Platform.android.js'),
    );
  });

  test('chains an existing resolveRequest', () => {
    const calls = [];
    const upstream = (context, moduleName, platform) => {
      calls.push([moduleName, platform]);
      return metroResolve(context, moduleName, platform);
    };
    const from = path.join(rn, 'Libraries', 'Utilities', 'Thing.js');
    assert.equal(
      resolve(from, './Platform', 'linux', {resolveRequest: upstream}).filePath,
      override('Libraries/Utilities/Platform.linux.js'),
    );
    resolve(from, './Platform', 'ios', {resolveRequest: upstream});
    assert.deepEqual(calls, [
      ['./Platform', 'linux'],
      ['./Platform', 'ios'],
    ]);
  });
});

describe('withLinux', () => {
  test('adds the platform, the resolver and the overrides and js folders', () => {
    const existing = () => null;
    const config = withLinux({
      projectRoot: app,
      watchFolders: [],
      resolver: {platforms: ['ios', 'android'], resolveRequest: existing},
    });
    assert.deepEqual(config.resolver.platforms, ['ios', 'android', 'linux']);
    assert.equal(typeof config.resolver.resolveRequest, 'function');
    assert.notEqual(config.resolver.resolveRequest, existing);
    assert.deepEqual(config.watchFolders, [
      fs.realpathSync(OVERRIDES_DIR),
      fs.realpathSync(path.join(OVERRIDES_DIR, '..', 'js')),
    ]);
  });

  test('is idempotent about the platform and watch folders', () => {
    const once = withLinux({projectRoot: app});
    const twice = withLinux(once);
    assert.deepEqual(twice.resolver.platforms, ['ios', 'android', 'linux']);
    assert.equal(twice.watchFolders.length, 2);
  });
});

test('every override replaces a react-native file', () => {
  const rnDir = path.join(
    __dirname,
    '..',
    'examples',
    'hello-world',
    'node_modules',
    'react-native',
  );
  const overrides = fs
    .readdirSync(OVERRIDES_DIR, {recursive: true})
    .filter(f => f.endsWith('.linux.js'));
  assert.ok(overrides.includes(path.join('Libraries', 'Utilities', 'Platform.linux.js')));
  if (!fs.existsSync(rnDir)) return; // example not installed
  for (const f of overrides) {
    const base = path.join(rnDir, f.replace(/\.linux\.js$/, ''));
    assert.ok(
      fs.existsSync(`${base}.android.js`) ||
        fs.existsSync(`${base}.ios.js`) ||
        fs.existsSync(`${base}.js`),
      `${f} replaces nothing in react-native`,
    );
  }
});
