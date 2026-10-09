/**
 * `react-native run-linux`: builds and launches the app's linux/ project.
 *
 * 1. Checks prerequisites (compilers, GTK, libsoup, Node).
 * 2. Fetches and builds React Native's C++ dependencies and Hermes into the
 *    shared cache, once per React Native version.
 * 3. Builds and installs the host library (ReactNativeGtk::host) into the
 *    cache, once per version of this package.
 * 4. Configures and builds linux/ with CMake and Ninja (Debug, or Release
 *    with --release, which also bundles the JS and assets), with the
 *    dependencies that have a linux/ folder autolinked (autolinking.js).
 * 5. Debug: starts Metro in a new terminal unless it's running.
 * 6. Launches the app.
 */
'use strict';

const {spawn, spawnSync} = require('child_process');
const fs = require('fs');
const http = require('http');
const path = require('path');
const paths = require('./paths');
const {checkPrerequisites, formatReport} = require('./prereqs');
const {appNames} = require('./initLinux');
const {writeAutolinking} = require('./autolinking');

class RunError extends Error {}

function step(message) {
  console.log(`\n▸ ${message}`);
}

function run(cmd, args, {cwd, env, quiet = false} = {}) {
  const r = spawnSync(cmd, args, {
    cwd,
    env: env || process.env,
    stdio: quiet ? ['ignore', 'pipe', 'pipe'] : 'inherit',
    encoding: 'utf8',
  });
  if (r.error) throw new RunError(`${cmd}: ${r.error.message}`);
  if (r.status !== 0) {
    if (quiet) process.stderr.write(`${r.stdout || ''}${r.stderr || ''}`);
    throw new RunError(`${[cmd, ...args].join(' ')} failed (exit ${r.status})`);
  }
  return r;
}

function elapsed(start) {
  const s = Math.round((Date.now() - start) / 1000);
  return s >= 60 ? `${Math.floor(s / 60)}m ${s % 60}s` : `${s}s`;
}

function depsReady(deps) {
  return (
    fs.existsSync(path.join(deps, 'react-native', 'packages', 'react-native', 'ReactCommon')) &&
    fs.existsSync(path.join(deps, 'hermes-build', 'lib', 'libhermesvm.so')) &&
    fs.existsSync(path.join(deps, 'hermes-headers'))
  );
}

/**
 * React Native's sources and Hermes, in the cache (once). Their output
 * always shows: they're the long steps.
 */
function ensureDeps(deps) {
  if (depsReady(deps)) {
    console.log(`React Native ${paths.reactNativeVersion()} dependencies: ${deps}`);
    return;
  }
  const start = Date.now();
  step(`Fetching React Native ${paths.reactNativeVersion()}'s C++ dependencies into ${deps}`);
  fs.mkdirSync(deps, {recursive: true});
  run('python3', [path.join(paths.PACKAGE_DIR, 'scripts', 'fetch-rn-deps.py'), '--deps', deps]);
  step('Building Hermes (once per React Native version)');
  run(path.join(paths.PACKAGE_DIR, 'scripts', 'build-hermes.sh'), [deps]);
  console.log(`Dependencies ready in ${elapsed(start)}`);
}

function cmakeBuildArgs(verbose) {
  return verbose ? ['--', '-v'] : [];
}

/**
 * The host library, built and installed into the cache (once). Debug for
 * run-linux; Release (`release`) for package-linux.
 */
function ensureHost(host, deps, {verbose, quiet, release = false}) {
  const install = path.join(host, 'install');
  const stamp = path.join(install, '.complete');
  const configDir = path.join(install, 'lib', 'cmake', 'ReactNativeGtk');
  ensureDeps(deps);
  if (fs.existsSync(stamp)) {
    console.log(`Host library: ${install}`);
    return configDir;
  }
  const start = Date.now();
  const build = path.join(host, 'build');
  const kind = release ? 'Release build of the React Native host library, for packages,' : 'React Native host library';
  step(`Building the ${kind} into ${host} (once per package version; this takes a while)`);
  run('cmake', [
    '-S', path.join(paths.PACKAGE_DIR, 'linux'),
    '-B', build,
    '-G', 'Ninja',
    ...paths.hostBuildTypeArgs(release),
    `-DRNGTK_DEPS_DIR=${deps}`,
    `-DCMAKE_INSTALL_PREFIX=${install}`,
    '-DRNGTK_BUILD_HARNESS=OFF',
    '--log-level=WARNING',
  ], {quiet});
  run('cmake', ['--build', build, '--target', 'rngtk_host', ...cmakeBuildArgs(verbose)]);
  run('cmake', ['--install', build], {quiet: true});
  fs.writeFileSync(stamp, `${new Date().toISOString()}\n`);
  console.log(`Host library built in ${elapsed(start)}`);
  return configDir;
}

function bundle(projectRoot, outDir, {quiet}) {
  step('Bundling the JS and assets');
  fs.mkdirSync(outDir, {recursive: true});
  run('npx', [
    'react-native', 'bundle',
    '--platform', 'linux',
    '--dev', 'false',
    '--entry-file', 'index.js',
    '--bundle-output', path.join(outDir, 'index.bundle.js'),
    '--assets-dest', outDir,
  ], {cwd: projectRoot, quiet});
}

/**
 * An app made by an earlier init-linux builds its libraries but doesn't hand
 * their packages to the host: say what to add to linux/main.cc.
 */
function warnIfNotRegistered(projectRoot) {
  let main = '';
  try {
    main = fs.readFileSync(path.join(projectRoot, 'linux', 'main.cc'), 'utf8');
  } catch {}
  if (main.includes('autolinkedPackages')) return false;
  console.warn(
    'warning: linux/main.cc doesn\'t register the autolinked libraries. Add\n' +
      '  #include <rngtk/Autolinking.h>\n' +
      'and, before rngtk::runApp:\n' +
      '  options.packages = rngtk::autolinkedPackages();',
  );
  return true;
}

function buildApp(projectRoot, configDir, {release, verbose, quiet, libraries = []}) {
  const config = release ? 'Release' : 'Debug';
  const buildDir = path.join(projectRoot, 'linux', 'build', config);
  const autolinkingDir = path.join(buildDir, 'autolinking');
  writeAutolinking(autolinkingDir, libraries);
  if (libraries.length) {
    console.log(`Autolinking: ${libraries.map(l => l.packageName).join(', ')}`);
    warnIfNotRegistered(projectRoot);
  }
  step(`Building linux/ (${config})`);
  const start = Date.now();
  run('cmake', [
    '-S', path.join(projectRoot, 'linux'),
    '-B', buildDir,
    '-G', 'Ninja',
    `-DCMAKE_BUILD_TYPE=${config}`,
    `-DReactNativeGtk_DIR=${configDir}`,
    `-DRNGTK_AUTOLINKING_DIR=${autolinkingDir}`,
    '--log-level=WARNING',
  ], {quiet});
  run('cmake', ['--build', buildDir, ...cmakeBuildArgs(verbose)]);
  console.log(`Built in ${elapsed(start)}`);
  return buildDir;
}

function httpGet(url, timeoutMs = 1000) {
  return new Promise(resolve => {
    const req = http.get(url, {timeout: timeoutMs}, res => {
      let body = '';
      res.on('data', d => (body += d));
      res.on('end', () => resolve({status: res.statusCode, body}));
    });
    req.on('timeout', () => req.destroy());
    req.on('error', () => resolve(null));
  });
}

async function isMetroRunning(port) {
  const r = await httpGet(`http://localhost:${port}/status`);
  return !!r && r.body.includes('packager-status:running');
}

function which(cmd) {
  const r = spawnSync('sh', ['-c', `command -v "${cmd}"`], {encoding: 'utf8'});
  return r.status === 0 ? r.stdout.trim() : null;
}

/** A terminal and the arguments that run `command` in it. */
function terminalCommand(terminal, command) {
  const name = path.basename(terminal);
  if (['gnome-terminal', 'kgx', 'ptyxis', 'tilix'].includes(name)) {
    return [terminal, ['--', 'sh', '-c', command]];
  }
  return [terminal, ['-e', `sh -c '${command.replace(/'/g, `'\\''`)}'`]];
}

function findTerminal(preferred) {
  const candidates = [
    preferred,
    process.env.REACT_TERMINAL,
    'x-terminal-emulator',
    'gnome-terminal',
    'kgx',
    'ptyxis',
    'konsole',
    'xfce4-terminal',
    'mate-terminal',
    'tilix',
    'xterm',
  ].filter(Boolean);
  for (const c of candidates) {
    const found = which(c);
    if (found) return found;
  }
  return null;
}

async function startMetro(projectRoot, port, {terminal}) {
  if (await isMetroRunning(port)) {
    console.log(`Metro is running on port ${port}`);
    return;
  }
  step(`Starting Metro on port ${port}`);
  const command = `cd "${projectRoot}" && npx react-native start --port ${port}`;
  const term = findTerminal(terminal);
  if (term && (process.env.DISPLAY || process.env.WAYLAND_DISPLAY)) {
    const [cmd, args] = terminalCommand(term, `${command}; exec sh`);
    spawn(cmd, args, {detached: true, stdio: 'ignore', cwd: projectRoot}).unref();
    console.log(`in a new ${path.basename(term)} window`);
  } else {
    const log = path.join(projectRoot, 'linux', 'build', 'metro.log');
    fs.mkdirSync(path.dirname(log), {recursive: true});
    const fd = fs.openSync(log, 'a');
    spawn('npx', ['react-native', 'start', '--port', String(port), '--no-interactive'], {
      cwd: projectRoot,
      detached: true,
      stdio: ['ignore', fd, fd],
    }).unref();
    console.log(`in the background (no terminal found); its output goes to ${log}`);
  }
  for (let i = 0; i < 120; i++) {
    if (await isMetroRunning(port)) return;
    await new Promise(r => setTimeout(r, 500));
  }
  throw new RunError(`Metro didn't start on port ${port}; run "npx react-native start --port ${port}" yourself`);
}

function appArgs(options) {
  const args = [];
  if (!options.release) args.push('--dev-server', `localhost:${options.port}`);
  if (options.smoke) args.push('--smoke');
  if (options.screenshot) args.push('--screenshot', path.resolve(options.screenshot));
  return args;
}

async function runLinux(projectRoot, options = {}) {
  const opts = {
    release: false,
    packager: true,
    port: 8081,
    launch: true,
    buildOnly: false,
    logging: false,
    checks: true,
    // Autolinked libraries: dependencies' `linux` CLI configs.
    libraries: [],
    ...options,
  };
  if (opts.buildOnly) {
    opts.launch = false;
    opts.packager = false;
  }
  const quiet = !opts.logging;
  const total = Date.now();

  if (!fs.existsSync(path.join(projectRoot, 'linux', 'CMakeLists.txt'))) {
    throw new RunError('No linux/CMakeLists.txt here. Run "npx react-native init-linux" first.');
  }

  if (opts.checks) {
    step('Checking prerequisites');
    const report = checkPrerequisites();
    formatReport(report).forEach(l => console.log(l));
    if (report.missing.length) throw new RunError('Missing prerequisites (above).');
  }

  const deps = paths.depsDir();
  const configDir = ensureHost(paths.hostDir(), deps, {verbose: opts.logging, quiet});
  const buildDir = buildApp(projectRoot, configDir, {...opts, quiet});
  if (opts.release) bundle(projectRoot, buildDir, {quiet});

  const {name} = appNames(projectRoot);
  const exe = path.join(buildDir, name);
  if (!fs.existsSync(exe)) throw new RunError(`Built, but ${exe} is missing`);
  console.log(`\n${path.relative(projectRoot, exe)} is ready (${elapsed(total)} in all)`);
  if (!opts.launch) return {exe, buildDir};

  if (!opts.release && opts.packager) await startMetro(projectRoot, opts.port, opts);

  step(`Launching ${name}`);
  const args = appArgs(opts);
  if (opts.logging || opts.smoke) {
    // Attached: the app's output here, and its exit status.
    const r = spawnSync(exe, args, {stdio: 'inherit', cwd: projectRoot});
    if (r.status !== 0) throw new RunError(`${name} exited with status ${r.status}`);
  } else {
    spawn(exe, args, {detached: true, stdio: 'ignore', cwd: projectRoot}).unref();
  }
  return {exe, buildDir};
}

module.exports = {
  runLinux,
  RunError,
  run,
  step,
  elapsed,
  ensureDeps,
  ensureHost,
  buildApp,
  bundle,
  which,
  terminalCommand,
  appArgs,
  depsReady,
  warnIfNotRegistered,
};
