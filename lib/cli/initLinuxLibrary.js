/**
 * `react-native init-linux-library`: adds Linux native code to a React
 * Native library, from this package's template-library/:
 *
 * - linux/: CMakeLists.txt and C++ for a TurboModule (<Name>Module) and a
 *   native component (<Name>View, a GtkCalendar to start from), with the
 *   function apps' autolinking calls (<name>_package()).
 * - src/: their JS sides (Native<Name>.ts, <Name>ViewNativeComponent.ts),
 *   unless those files exist.
 * - react-native.config.js: the Linux entry (cmakeTarget, packageFunction),
 *   or the lines to add when the file exists already.
 *
 * <Name> comes from --name or package.json ("react-native-gtk-calendar" ->
 * GtkCalendar).
 */
'use strict';

const fs = require('fs');
const path = require('path');
const {PACKAGE_DIR} = require('./paths');
const {RunError} = require('./runLinux');

const TEMPLATE_DIR = path.join(PACKAGE_DIR, 'template-library');

/** The library's names: Pascal (types, files), snake (target, functions). */
function libraryNames(projectRoot, {name} = {}) {
  let pkgName = path.basename(projectRoot);
  try {
    pkgName = JSON.parse(fs.readFileSync(path.join(projectRoot, 'package.json'), 'utf8')).name || pkgName;
  } catch {}
  const words = String(name || pkgName.replace(/^@[^/]+\//, '').replace(/^react-native-/, ''))
    .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
    .split(/[^A-Za-z0-9]+/)
    .filter(Boolean);
  if (!words.length) throw new RunError(`Can't make a module name from "${name || pkgName}"; pass --name`);
  let pascal = words.map(w => w[0].toUpperCase() + w.slice(1)).join('');
  if (/^[0-9]/.test(pascal)) pascal = `Lib${pascal}`;
  const snake = words.map(w => w.toLowerCase()).join('_').replace(/^([0-9])/, 'lib_$1');
  return {
    pascal,
    snake,
    target: snake,
    packageFunction: `${snake}_package`,
    component: `${pascal}View`,
    packageName: pkgName,
  };
}

/** The template's text (or file name) with this library's names. */
function rename(text, names) {
  return text
    .replace(/RNGtkExampleView/g, names.component)
    .replace(/Example/g, names.pascal)
    .replace(/example/g, names.snake);
}

function copy(fromDir, toDir, names, {overwrite, written, skipped}) {
  fs.mkdirSync(toDir, {recursive: true});
  for (const entry of fs.readdirSync(fromDir, {withFileTypes: true})) {
    const src = path.join(fromDir, entry.name);
    const dest = path.join(toDir, rename(entry.name, names));
    if (entry.isDirectory()) {
      copy(src, dest, names, {overwrite, written, skipped});
    } else if (fs.existsSync(dest) && !overwrite) {
      skipped.push(dest);
    } else {
      fs.writeFileSync(dest, rename(fs.readFileSync(src, 'utf8'), names));
      written.push(dest);
    }
  }
}

function initLinuxLibrary(projectRoot, {overwrite = false, name, log = console.log, templateDir = TEMPLATE_DIR} = {}) {
  const names = libraryNames(projectRoot, {name});
  const rel = p => path.relative(projectRoot, p) || '.';
  const written = [];
  const skipped = [];
  copy(path.join(templateDir, 'linux'), path.join(projectRoot, 'linux'), names, {overwrite, written, skipped});
  // The JS sides; index.ts only where the library has none.
  const srcDir = path.join(projectRoot, 'src');
  for (const file of fs.readdirSync(path.join(templateDir, 'src'))) {
    if (file === 'index.ts' && ['index.ts', 'index.tsx', 'index.js'].some(f => fs.existsSync(path.join(srcDir, f)))) {
      continue;
    }
    const dest = path.join(srcDir, rename(file, names));
    if (fs.existsSync(dest) && !overwrite) {
      skipped.push(dest);
      continue;
    }
    fs.mkdirSync(srcDir, {recursive: true});
    fs.writeFileSync(dest, rename(fs.readFileSync(path.join(templateDir, 'src', file), 'utf8'), names));
    written.push(dest);
  }
  written.forEach(f => log(`created ${rel(f)}`));
  skipped.forEach(f => log(`kept ${rel(f)} (exists; --overwrite replaces it)`));

  const configPath = path.join(projectRoot, 'react-native.config.js');
  const entry = `linux: {cmakeTarget: '${names.target}', packageFunction: '${names.packageFunction}'}`;
  let config;
  if (!fs.existsSync(configPath)) {
    fs.writeFileSync(configPath, rename(fs.readFileSync(path.join(templateDir, 'react-native.config.js'), 'utf8'), names));
    log('created react-native.config.js');
    config = 'created';
  } else if (fs.readFileSync(configPath, 'utf8').includes(names.packageFunction)) {
    config = 'unchanged';
  } else {
    log('Add Linux to react-native.config.js (dependency.platforms):');
    log(`  ${entry}`);
    config = 'manual';
  }
  return {names, written, skipped, config};
}

module.exports = {initLinuxLibrary, libraryNames, rename, TEMPLATE_DIR};
