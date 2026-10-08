/**
 * `react-native init-linux`: adds Linux to a React Native app.
 *
 * - linux/ from this package's template/linux (CMakeLists.txt, main.cc)
 * - the @curiosity26/react-native-gtk4 dependency in package.json
 * - a "linux": "react-native run-linux" script
 * - metro.config.js wrapped with withLinux (or the edit to make, printed)
 *
 * Idempotent: files and settings that are already there are left alone,
 * unless --overwrite replaces linux/'s files.
 */
'use strict';

const fs = require('fs');
const path = require('path');
const {PACKAGE_NAME, PACKAGE_DIR, packageVersion} = require('./paths');

const TEMPLATE_DIR = path.join(PACKAGE_DIR, 'template', 'linux');
const METRO_REQUIRE = `const {withLinux} = require('${PACKAGE_NAME}/metro-config');`;

/** The app's names, from app.json (or package.json). */
function appNames(projectRoot, {appId} = {}) {
  let appJson = {};
  try {
    appJson = JSON.parse(fs.readFileSync(path.join(projectRoot, 'app.json'), 'utf8'));
  } catch {}
  let pkg = {};
  try {
    pkg = JSON.parse(fs.readFileSync(path.join(projectRoot, 'package.json'), 'utf8'));
  } catch {}
  const moduleName = appJson.name || pkg.name || path.basename(projectRoot);
  return {
    moduleName,
    name: targetName(moduleName),
    title: appJson.displayName || moduleName,
    appId: appId || defaultAppId(moduleName),
  };
}

/** A CMake target and executable name. */
function targetName(name) {
  const s = String(name).replace(/^@[^/]+\//, '').replace(/[^A-Za-z0-9_-]/g, '');
  return s && /^[A-Za-z]/.test(s) ? s : `App${s}`;
}

/** Like React Native's Android default (com.<name>): a valid GApplication id. */
function defaultAppId(name) {
  let s = String(name).replace(/^@[^/]+\//, '').toLowerCase().replace(/[^a-z0-9_]/g, '');
  if (!s || /^[0-9]/.test(s)) s = `app${s}`;
  return `com.${s}`;
}

function renderTemplate(text, vars) {
  return text.replace(/\{\{(\w+)\}\}/g, (m, key) =>
    key in vars ? String(vars[key]).replace(/["\\]/g, '\\$&') : m,
  );
}

/**
 * Edits package.json's text: adds the dependency and the "linux" script.
 * Returns {text, changes}.
 */
function editPackageJson(text, {version, overwrite = false} = {}) {
  const pkg = JSON.parse(text);
  const changes = [];
  const inDeps =
    (pkg.dependencies && pkg.dependencies[PACKAGE_NAME]) ||
    (pkg.devDependencies && pkg.devDependencies[PACKAGE_NAME]);
  if (!inDeps) {
    pkg.dependencies = {...(pkg.dependencies || {}), [PACKAGE_NAME]: version};
    pkg.dependencies = sortKeys(pkg.dependencies);
    changes.push(`added ${PACKAGE_NAME}@${version} to dependencies`);
  }
  pkg.scripts = pkg.scripts || {};
  const script = 'react-native run-linux';
  if (pkg.scripts.linux !== script && (overwrite || !pkg.scripts.linux)) {
    pkg.scripts.linux = script;
    changes.push(`added the "linux" script (${script})`);
  }
  if (!changes.length) return {text, changes};
  const indent = text.match(/^[ \t]+(?=")/m)?.[0] ?? '  ';
  const eol = text.endsWith('\n') ? '\n' : '';
  return {text: JSON.stringify(pkg, null, indent) + eol, changes};
}

function sortKeys(obj) {
  return Object.fromEntries(Object.entries(obj).sort(([a], [b]) => a.localeCompare(b)));
}

/**
 * Wraps a metro.config.js's export with withLinux. Returns {text, status}:
 * status is 'unchanged' (already uses this package), 'edited', or 'manual'
 * (an unusual file; `instructions` says what to change).
 */
function editMetroConfig(text) {
  if (text.includes(`${PACKAGE_NAME}/metro-config`)) {
    return {text, status: 'unchanged'};
  }
  const exports = [...text.matchAll(/^module\.exports\s*=\s*/gm)];
  const end = exports.length === 1 ? statementEnd(text, exports[0].index + exports[0][0].length) : -1;
  if (end < 0) {
    return {
      text,
      status: 'manual',
      instructions: [
        'Edit metro.config.js to add the linux platform:',
        `  ${METRO_REQUIRE}`,
        '  module.exports = withLinux(<your config>);',
      ],
    };
  }
  const start = exports[0].index + exports[0][0].length;
  const expr = text.slice(start, end).trim();
  let out = `${text.slice(0, start)}withLinux(${expr})${text.slice(end)}`;
  // The require goes after the last top-level require, or first.
  const requires = [...out.matchAll(/^(const|let|var)\s[^\n]*require\([^\n]*\);?[ \t]*$/gm)];
  if (requires.length) {
    const last = requires[requires.length - 1];
    const at = last.index + last[0].length;
    out = `${out.slice(0, at)}\n${METRO_REQUIRE}${out.slice(at)}`;
  } else {
    out = `${METRO_REQUIRE}\n${out}`;
  }
  return {text: out, status: 'edited'};
}

// The end of the expression starting at `from`: the `;` (or end of file)
// at bracket depth 0, skipping strings and comments.
function statementEnd(text, from) {
  let depth = 0;
  for (let i = from; i < text.length; i++) {
    const c = text[i];
    if (c === '"' || c === "'" || c === '`') {
      for (i++; i < text.length && text[i] !== c; i++) if (text[i] === '\\') i++;
    } else if (c === '/' && text[i + 1] === '/') {
      while (i < text.length && text[i] !== '\n') i++;
    } else if (c === '/' && text[i + 1] === '*') {
      i = text.indexOf('*/', i + 2);
      if (i < 0) return -1;
      i++;
    } else if ('([{'.includes(c)) {
      depth++;
    } else if (')]}'.includes(c)) {
      depth--;
    } else if (c === ';' && depth === 0) {
      return i;
    }
  }
  return depth === 0 ? text.replace(/\s+$/, '').length : -1;
}

/** Copies template/linux into `dir`. Returns {written, skipped}. */
function copyTemplate(dir, vars, {overwrite = false, templateDir = TEMPLATE_DIR} = {}) {
  const written = [];
  const skipped = [];
  const walk = (from, to) => {
    fs.mkdirSync(to, {recursive: true});
    for (const entry of fs.readdirSync(from, {withFileTypes: true})) {
      // npm drops .gitignore files from packages: the template has _gitignore.
      const name = entry.name === '_gitignore' ? '.gitignore' : entry.name;
      const src = path.join(from, entry.name);
      const dest = path.join(to, name);
      if (entry.isDirectory()) {
        walk(src, dest);
        continue;
      }
      if (fs.existsSync(dest) && !overwrite) {
        skipped.push(dest);
        continue;
      }
      fs.writeFileSync(dest, renderTemplate(fs.readFileSync(src, 'utf8'), vars));
      written.push(dest);
    }
  };
  walk(templateDir, dir);
  return {written, skipped};
}

/**
 * Runs init-linux in `projectRoot`. Returns a summary
 * {names, written, skipped, packageChanges, metro}; logs through `log`.
 */
function initLinux(projectRoot, {overwrite = false, appId, log = console.log} = {}) {
  const names = appNames(projectRoot, {appId});
  const rel = p => path.relative(projectRoot, p) || '.';

  const {written, skipped} = copyTemplate(path.join(projectRoot, 'linux'), names, {overwrite});
  written.forEach(f => log(`created ${rel(f)}`));
  skipped.forEach(f => log(`kept ${rel(f)} (exists; --overwrite replaces it)`));

  const pkgPath = path.join(projectRoot, 'package.json');
  let packageChanges = [];
  if (fs.existsSync(pkgPath)) {
    const edit = editPackageJson(fs.readFileSync(pkgPath, 'utf8'), {
      version: `^${packageVersion()}`,
      overwrite,
    });
    packageChanges = edit.changes;
    if (packageChanges.length) fs.writeFileSync(pkgPath, edit.text);
    packageChanges.forEach(c => log(`package.json: ${c}`));
  }

  const metroPath = path.join(projectRoot, 'metro.config.js');
  let metro;
  if (fs.existsSync(metroPath)) {
    metro = editMetroConfig(fs.readFileSync(metroPath, 'utf8'));
    if (metro.status === 'edited') {
      fs.writeFileSync(metroPath, metro.text);
      log('metro.config.js: wrapped the config with withLinux');
    } else if (metro.status === 'manual') {
      metro.instructions.forEach(l => log(l));
    }
  } else {
    metro = {status: 'created'};
    fs.writeFileSync(
      metroPath,
      `const {getDefaultConfig} = require('${PACKAGE_NAME}/metro-config');\n\n` +
        'module.exports = getDefaultConfig(__dirname);\n',
    );
    log('created metro.config.js');
  }
  return {names, written, skipped, packageChanges, metro};
}

module.exports = {
  appNames,
  targetName,
  defaultAppId,
  renderTemplate,
  editPackageJson,
  editMetroConfig,
  copyTemplate,
  initLinux,
  TEMPLATE_DIR,
};
