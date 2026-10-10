/**
 * Metro support for the 'linux' platform, the out-of-tree way (like
 * react-native-windows): React Native's own package stays untouched, and
 * the files it splits by platform (Foo.ios.js / Foo.android.js) get Linux
 * versions from this package's overrides/ directory.
 *
 *   // metro.config.js
 *   const {getDefaultConfig} = require('@curiosity26/react-native-gtk4/metro-config');
 *   module.exports = getDefaultConfig(__dirname);
 *
 * or, to extend a config you already have:
 *
 *   const {withLinux} = require('@curiosity26/react-native-gtk4/metro-config');
 *   module.exports = withLinux(config);
 */
'use strict';

const fs = require('fs');
const path = require('path');

const PLATFORM = 'linux';
const PACKAGE_DIR = __dirname;
const OVERRIDES_DIR = path.join(PACKAGE_DIR, 'overrides');
// The package's own JS (Dialogs, Menu...): its imports resolve from the app
// too, and Metro watches it like overrides/.
const JS_DIR = path.join(PACKAGE_DIR, 'js');
const SOURCE_DIRS = [OVERRIDES_DIR, JS_DIR];
const RN_SEGMENT = `${path.sep}node_modules${path.sep}react-native${path.sep}`;
const SOURCE_EXTS = ['.js', '.jsx', '.ts', '.tsx'];
const UPSTREAM_PREFIXES = ['react-native-upstream/', 'react-native/'];
// Third-party packages whose native parts Linux doesn't have yet (native
// modules come with autolinking, Phase 3), but which ship pure-JS
// variants for another platform that work here. React Native's app
// template uses react-native-safe-area-context.
//
// With 'web', the package's web build is used: Foo.web.tsx where it
// exists, and the platform-less Foo.tsx instead of Foo.native.tsx (the
// web never picks .native). react-native-screens' web components are
// views, and @react-navigation/native-stack's web NativeStackView draws
// the stack with @react-navigation/elements' header, so native-stack works
// as it does on the web.
const JS_FALLBACK_PLATFORMS = {
  'react-native-safe-area-context': 'windows',
  'react-native-screens': 'web',
  '@react-navigation/native-stack': 'web',
};
// Packages whose JS for another platform drives the native side a Linux
// port (packages/ in this repository) adds: used when the app has the port.
// react-native-webview's iOS JS drives RNCWebView, as on iOS and macOS.
const PORT_JS_PLATFORMS = {
  'react-native-webview': {port: '@curiosity26/react-native-gtk4-webview', platform: 'ios'},
};
// Ports with native components for a library that otherwise gets a JS
// fallback: with the port installed, the library's (and `native` packages')
// own JS runs instead of the fallback, and files in the port's overrides/
// folder replace the library's at the same path (react-native-screens'
// src/core.ts, which lists the platforms it has native components on).
const PORT_OVERRIDES = {
  'react-native-screens': {
    port: '@curiosity26/react-native-gtk4-screens',
    native: ['react-native-screens', '@react-navigation/native-stack'],
  },
};

// RNGTK_IGNORE_PORTS: ports (package names, comma-separated) Metro acts as
// if weren't installed, e.g. to bundle react-native-screens' web fallback
// in an app that has the port.
function portDir(port, projectRoot) {
  if ((process.env.RNGTK_IGNORE_PORTS || '').split(',').includes(port)) return null;
  try {
    return path.dirname(
      realpath(require.resolve(`${port}/package.json`, {paths: [projectRoot || process.cwd()]})),
    );
  } catch {
    return null;
  }
}

/** JS_FALLBACK_PLATFORMS, and PORT_JS_PLATFORMS' entries the app has the port for. */
function fallbackPlatforms(projectRoot) {
  const out = {...JS_FALLBACK_PLATFORMS};
  for (const [name, {port, platform}] of Object.entries(PORT_JS_PLATFORMS)) {
    if (portDir(port, projectRoot)) out[name] = platform;
  }
  for (const {port, native} of Object.values(PORT_OVERRIDES)) {
    if (portDir(port, projectRoot)) for (const name of native) delete out[name];
  }
  return out;
}

/**
 * For modules that resolve to nothing on Linux (they only exist per
 * platform): fallbackPlatforms, plus 'web' for the ported libraries (the
 * port doesn't do react-native-screens' native tabs, say).
 */
function missingPlatforms(projectRoot) {
  const out = fallbackPlatforms(projectRoot);
  for (const {port, native} of Object.values(PORT_OVERRIDES)) {
    if (portDir(port, projectRoot)) for (const name of native) out[name] ??= 'web';
  }
  return out;
}

/** {library name: the installed port's overrides/ folder}. */
function portOverrides(projectRoot) {
  const out = {};
  for (const [name, {port}] of Object.entries(PORT_OVERRIDES)) {
    const dir = portDir(port, projectRoot);
    if (dir && fs.existsSync(path.join(dir, 'overrides'))) out[name] = path.join(dir, 'overrides');
  }
  return out;
}

/** The app's copy of a package (its folder), or null. */
function packageRoot(name, projectRoot) {
  try {
    return path.dirname(require.resolve(`${name}/package.json`, {paths: [projectRoot || process.cwd()]}));
  } catch {
    return null;
  }
}
const NM_SEGMENT = `${path.sep}node_modules${path.sep}`;

function isFile(p) {
  try {
    return fs.statSync(p).isFile();
  } catch {
    return false;
  }
}

function isInside(file, dir) {
  const rel = path.relative(dir, file);
  return rel !== '' && !rel.startsWith('..') && !path.isAbsolute(rel);
}

function realpath(p) {
  try {
    return fs.realpathSync(p);
  } catch {
    return p;
  }
}

// The react-native package directory that contains `file`, if any.
function reactNativeDirOf(file, knownRnDir) {
  if (knownRnDir && isInside(file, knownRnDir)) return knownRnDir;
  const i = file.lastIndexOf(RN_SEGMENT);
  return i === -1 ? null : file.slice(0, i + RN_SEGMENT.length - 1);
}

function stripSourceExt(file) {
  const ext = path.extname(file);
  return SOURCE_EXTS.includes(ext) ? file.slice(0, -ext.length) : file;
}

/**
 * For a react-native module path without extension (`<rnDir>/Libraries/X`),
 * the file Linux should use instead, or null to keep the normal resolution:
 * our override if there is one, otherwise the android variant when React
 * Native splits the module by platform (both .ios.js and .android.js exist,
 * or there is no platform-less file to fall back to).
 */
function linuxReplacement(rnDir, modulePath, resolvedNormally) {
  const rel = path.relative(rnDir, modulePath);
  // Packages nested inside react-native are third-party code.
  if (rel.split(path.sep).includes('node_modules')) return null;
  const override = path.join(OVERRIDES_DIR, `${rel}.${PLATFORM}.js`);
  if (isFile(override)) return override;
  const android = `${modulePath}.android.js`;
  if (!isFile(android)) return null;
  const splitByPlatform = isFile(`${modulePath}.ios.js`);
  // Some platform-split modules ship a platform-less Foo.js that just
  // re-imports './Foo' (for legacy deep imports); for an unknown platform
  // that would import itself, so the android variant is the right fallback.
  return splitByPlatform || !resolvedNormally ? android : null;
}

/**
 * For a file in a package listed in JS_FALLBACK_PLATFORMS, its variant for
 * that platform (Foo.windows.tsx next to Foo.tsx), or null. For 'web', a
 * Foo.native.tsx gives way to Foo.web.tsx or else Foo.tsx.
 */
function jsFallback(filePath, platforms = JS_FALLBACK_PLATFORMS) {
  const i = filePath.lastIndexOf(NM_SEGMENT);
  if (i === -1) return null;
  const parts = filePath.slice(i + NM_SEGMENT.length).split(path.sep);
  const name = parts[0].startsWith('@') ? `${parts[0]}/${parts[1]}` : parts[0];
  const platform = platforms[name];
  if (!platform) return null;
  let base = stripSourceExt(filePath);
  if (base === filePath || /\.linux$/.test(base)) return null;
  const native = /\.native$/.test(base);
  if (native && platform !== 'web') return null;
  if (native) base = base.slice(0, -'.native'.length);
  for (const ext of SOURCE_EXTS) {
    const candidate = `${base}.${platform}${ext}`;
    if (isFile(candidate)) return candidate;
  }
  if (native) {
    for (const ext of SOURCE_EXTS) {
      if (isFile(base + ext)) return base + ext;
    }
  }
  return null;
}

/** The package name and the path inside it of a file under node_modules. */
function packagePathOf(filePath) {
  const i = filePath.lastIndexOf(NM_SEGMENT);
  if (i === -1) return null;
  const parts = filePath.slice(i + NM_SEGMENT.length).split(path.sep);
  const n = parts[0].startsWith('@') ? 2 : 1;
  return {
    name: parts.slice(0, n).join('/'),
    root: filePath.slice(0, i + NM_SEGMENT.length) + parts.slice(0, n).join(path.sep),
    rel: parts.slice(n).join(path.sep),
  };
}

/**
 * Wraps a Metro resolveRequest so that, for platform 'linux', modules inside
 * react-native resolve to overrides/ or the android variant. Third-party
 * packages resolve normally, except the JS_FALLBACK_PLATFORMS ones.
 */
function createLinuxResolver({projectRoot, resolveRequest: upstream} = {}) {
  const platforms = fallbackPlatforms(projectRoot);
  const missing = missingPlatforms(projectRoot);
  const overrides = portOverrides(projectRoot);
  // An override file stands in for the library's: its imports resolve from
  // the library's file.
  const libraryRoots = {};
  for (const name of Object.keys(overrides)) libraryRoots[name] = packageRoot(name, projectRoot);
  let knownRnDir;
  if (projectRoot) {
    try {
      knownRnDir = path.dirname(
        realpath(
          require.resolve('react-native/package.json', {paths: [projectRoot]}),
        ),
      );
    } catch {
      // Resolved per file from its path instead.
    }
  }

  return function resolveRequest(context, moduleName, platform) {
    const next = upstream ?? context.resolveRequest;
    if (platform !== PLATFORM) return next(context, moduleName, platform);

    // Overrides import React Native's own files as
    // 'react-native-upstream/<path>' (a 'react-native/...' deep import would
    // make RN's dev Babel preset warn in every app). Resolve those as files
    // inside the app's react-native, since its "exports" map hides
    // src/private. Resolve other packages from the app: when this package
    // is linked (npm `file:` dependency) its real path has no node_modules
    // above it.
    if (
      projectRoot &&
      SOURCE_DIRS.some(dir => isInside(context.originModulePath, dir)) &&
      !moduleName.startsWith('.') &&
      !path.isAbsolute(moduleName)
    ) {
      const prefix = UPSTREAM_PREFIXES.find(p => moduleName.startsWith(p));
      if (knownRnDir && prefix) {
        context = {
          ...context,
          originModulePath: path.join(knownRnDir, 'package.json'),
        };
        moduleName = `./${moduleName.slice(prefix.length)}`;
      } else {
        context = {
          ...context,
          originModulePath: path.join(projectRoot, 'package.json'),
        };
      }
    }

    for (const [name, dir] of Object.entries(overrides)) {
      if (!isInside(context.originModulePath, dir) || !libraryRoots[name]) continue;
      context = {
        ...context,
        originModulePath: path.join(libraryRoots[name], path.relative(dir, context.originModulePath)),
      };
    }

    let resolution;
    let error;
    try {
      resolution = next(context, moduleName, platform);
    } catch (e) {
      error = e;
    }

    if (resolution) {
      if (resolution.type !== 'sourceFile') return resolution;
      const rnDir = reactNativeDirOf(resolution.filePath, knownRnDir);
      const pkg = !rnDir && packagePathOf(resolution.filePath);
      if (pkg && overrides[pkg.name]) {
        libraryRoots[pkg.name] ??= pkg.root;
        const base = stripSourceExt(path.join(overrides[pkg.name], pkg.rel));
        for (const ext of SOURCE_EXTS) {
          if (isFile(base + ext)) return {type: 'sourceFile', filePath: base + ext};
        }
      }
      if (!rnDir) {
        const fallback = jsFallback(resolution.filePath, platforms);
        return fallback ? {type: 'sourceFile', filePath: fallback} : resolution;
      }
      const replacement = linuxReplacement(
        rnDir,
        stripSourceExt(resolution.filePath),
        true,
      );
      return replacement
        ? {type: 'sourceFile', filePath: replacement}
        : resolution;
    }

    // Normal resolution failed. In a JS_FALLBACK_PLATFORMS package: a
    // module that only exists as platform variants (Foo.ios.tsx,
    // Foo.web.tsx).
    if (moduleName.startsWith('.')) {
      const modulePath = path.resolve(path.dirname(context.originModulePath), moduleName);
      const fallback = jsFallback(`${modulePath}.js`, missing);
      if (fallback) return {type: 'sourceFile', filePath: fallback};
    }
    // A react-native module that only exists as .ios.js/.android.js
    // variants.
    let target;
    if (moduleName.startsWith('.')) {
      const rnDir = reactNativeDirOf(context.originModulePath, knownRnDir);
      if (rnDir) {
        target = {
          rnDir,
          modulePath: path.resolve(
            path.dirname(context.originModulePath),
            moduleName,
          ),
        };
      }
    } else if (knownRnDir && moduleName.startsWith('react-native/')) {
      target = {
        rnDir: knownRnDir,
        modulePath: path.join(knownRnDir, moduleName.slice('react-native/'.length)),
      };
    }
    if (target && isInside(target.modulePath, target.rnDir)) {
      const replacement = linuxReplacement(
        target.rnDir,
        stripSourceExt(target.modulePath),
        false,
      );
      if (replacement) return {type: 'sourceFile', filePath: replacement};
    }
    throw error;
  };
}

/** Adds the 'linux' platform to a Metro config. */
function withLinux(config) {
  const projectRoot = config.projectRoot ?? process.cwd();
  const resolver = config.resolver ?? {};
  const platforms = resolver.platforms ?? ['ios', 'android'];
  // Metro only serves files under watched folders. Watch overrides/ and
  // js/ alone: a linked checkout of this package also holds native build
  // trees.
  const watchFolders = [...(config.watchFolders ?? [])];
  for (const sourceDir of [...SOURCE_DIRS, ...Object.values(portOverrides(projectRoot))]) {
    const dir = realpath(sourceDir);
    const watched =
      isInside(dir, realpath(projectRoot)) ||
      watchFolders.some(folder => {
        const real = realpath(folder);
        return real === dir || isInside(dir, real);
      });
    if (!watched) watchFolders.push(dir);
  }
  return {
    ...config,
    watchFolders,
    resolver: {
      ...resolver,
      platforms: platforms.includes(PLATFORM)
        ? platforms
        : [...platforms, PLATFORM],
      resolveRequest: createLinuxResolver({
        projectRoot,
        resolveRequest: resolver.resolveRequest,
      }),
    },
  };
}

/** @react-native/metro-config's default config for `projectRoot`, plus Linux. */
function getDefaultConfig(projectRoot) {
  const metroConfigPath = require.resolve('@react-native/metro-config', {
    paths: [projectRoot],
  });
  return withLinux(require(metroConfigPath).getDefaultConfig(projectRoot));
}

module.exports = {
  JS_FALLBACK_PLATFORMS,
  withLinux,
  getDefaultConfig,
  createLinuxResolver,
  OVERRIDES_DIR,
};
