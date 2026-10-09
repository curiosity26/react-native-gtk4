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
const JS_FALLBACK_PLATFORMS = {
  'react-native-safe-area-context': 'windows',
};
// Packages whose JS for another platform drives the native side a Linux
// port (packages/ in this repository) adds: used when the app has the port.
// react-native-webview's iOS JS drives RNCWebView, as on iOS and macOS.
const PORT_JS_PLATFORMS = {
  'react-native-webview': {port: '@curiosity26/react-native-gtk4-webview', platform: 'ios'},
};

/** JS_FALLBACK_PLATFORMS, and PORT_JS_PLATFORMS' entries the app has the port for. */
function fallbackPlatforms(projectRoot) {
  const out = {...JS_FALLBACK_PLATFORMS};
  for (const [name, {port, platform}] of Object.entries(PORT_JS_PLATFORMS)) {
    try {
      require.resolve(`${port}/package.json`, {paths: [projectRoot || process.cwd()]});
      out[name] = platform;
    } catch {}
  }
  return out;
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
 * that platform (Foo.windows.tsx next to Foo.tsx), or null.
 */
function jsFallback(filePath, platforms = JS_FALLBACK_PLATFORMS) {
  const i = filePath.lastIndexOf(NM_SEGMENT);
  if (i === -1) return null;
  const parts = filePath.slice(i + NM_SEGMENT.length).split(path.sep);
  const name = parts[0].startsWith('@') ? `${parts[0]}/${parts[1]}` : parts[0];
  const platform = platforms[name];
  if (!platform) return null;
  const base = stripSourceExt(filePath);
  if (base === filePath || /\.(linux|native)$/.test(base)) return null;
  for (const ext of SOURCE_EXTS) {
    const candidate = `${base}.${platform}${ext}`;
    if (isFile(candidate)) return candidate;
  }
  return null;
}

/**
 * Wraps a Metro resolveRequest so that, for platform 'linux', modules inside
 * react-native resolve to overrides/ or the android variant. Third-party
 * packages resolve normally, except the JS_FALLBACK_PLATFORMS ones.
 */
function createLinuxResolver({projectRoot, resolveRequest: upstream} = {}) {
  const platforms = fallbackPlatforms(projectRoot);
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

    // Normal resolution failed: a react-native module that only exists as
    // .ios.js/.android.js variants.
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
  for (const sourceDir of SOURCE_DIRS) {
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
