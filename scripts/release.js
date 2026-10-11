#!/usr/bin/env node
/**
 * Versions and publishes the package and the library ports in packages/
 * (docs/releasing.md).
 *
 * Every package's major.minor is React Native's (rn-version.properties):
 * 0.87.x on React Native 0.87, 0.83.x on 0.83. The patch is each package's
 * own, so a fix to one port releases that port alone. A new React Native
 * minor starts every package at <major>.<minor>.0.
 *
 *   scripts/release.js check
 *       Checks the versions, the peer ranges between our packages and the
 *       changelogs against that rule.
 *   scripts/release.js bump NAME... | all
 *       Raises those packages' patch and adds an unreleased section to
 *       their CHANGELOG.md. NAME is core or a folder in packages/.
 *   scripts/release.js line
 *       Moves every package not on React Native's major.minor to it, at .0
 *       (upgrade-rn.sh runs it).
 *   scripts/release.js plan [--json]
 *       Which versions GitHub Packages doesn't have yet, and the dist-tag
 *       they go out under on this branch.
 *   scripts/release.js publish core|libraries [--dry-run]
 *       Publishes those of them, tags them and sets their dist-tags.
 */
'use strict';

const {execFileSync} = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const SCOPE = '@curiosity26';
const REGISTRY = 'https://npm.pkg.github.com';
const CORE = `${SCOPE}/react-native-gtk4`;

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function writeJson(file, json) {
  fs.writeFileSync(file, JSON.stringify(json, null, 2) + '\n');
}

/** React Native's major.minor: the release line. */
function line(root = ROOT) {
  const text = fs.readFileSync(path.join(root, 'rn-version.properties'), 'utf8');
  const m = /^reactNative=(\d+)\.(\d+)\./m.exec(text);
  if (!m) throw new Error('rn-version.properties has no reactNative=X.Y.Z');
  return `${m[1]}.${m[2]}`;
}

/** The package and each port: {id, dir, file, pkg}. */
function packages(root = ROOT) {
  const list = [{id: 'core', dir: root}];
  const dir = path.join(root, 'packages');
  for (const d of fs.readdirSync(dir).sort()) {
    if (fs.existsSync(path.join(dir, d, 'package.json'))) list.push({id: d, dir: path.join(dir, d)});
  }
  for (const p of list) {
    p.file = path.join(p.dir, 'package.json');
    p.pkg = readJson(p.file);
  }
  return list;
}

/** The peer range a package of ours gives another on this line. */
function peerRange(ln) {
  return `${ln}.x`;
}

/** The npm dist-tag a branch publishes under: main is latest, release/X.Y is vX.Y-stable. */
function distTags(branch, ln) {
  if (branch === 'main') return ['latest', `v${ln}-stable`];
  const m = /^release\/(\d+\.\d+)$/.exec(branch || '');
  if (m) {
    if (m[1] !== ln) throw new Error(`branch ${branch} is on React Native ${ln} (rn-version.properties)`);
    return [`v${ln}-stable`];
  }
  return null;
}

/** The git tag for a package's version: v0.87.0 for the package (its GitHub release), react-native-gtk4-svg@0.87.1 for a port. */
function gitTag(p) {
  return p.id === 'core' ? `v${p.pkg.version}` : `${p.pkg.name.slice(SCOPE.length + 1)}@${p.pkg.version}`;
}

function changelogHas(dir, version) {
  const file = path.join(dir, 'CHANGELOG.md');
  if (!fs.existsSync(file)) return false;
  return fs.readFileSync(file, 'utf8').split('\n').some(l => l.split(' ')[0] === '##' && l.split(' ')[1] === version);
}

/** What breaks the versioning rule, as messages. */
function problems(root = ROOT) {
  const ln = line(root);
  const list = packages(root);
  const ours = new Set(list.map(p => p.pkg.name));
  const out = [];
  for (const p of list) {
    const {name, version} = p.pkg;
    if (!new RegExp(`^${ln.replace('.', '\\.')}\\.\\d+$`).test(version || '')) {
      out.push(`${name} is ${version}: it should be ${ln}.<patch> (React Native ${ln})`);
    }
    for (const [dep, range] of Object.entries(p.pkg.peerDependencies || {})) {
      if (ours.has(dep) && range !== peerRange(ln)) out.push(`${name} wants ${dep} ${range}: it should be ${peerRange(ln)}`);
    }
    if (p.id !== 'core' && !(p.pkg.files || []).includes('CHANGELOG.md')) out.push(`${name} doesn't ship its CHANGELOG.md`);
    if (!changelogHas(p.dir, version)) out.push(`${path.relative(root, p.dir) || '.'}/CHANGELOG.md has no "## ${version}" section`);
  }
  return out;
}

function addChangelogSection(dir, version, body) {
  const file = path.join(dir, 'CHANGELOG.md');
  if (changelogHas(dir, version)) return;
  const text = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '# Changelog\n';
  const lines = text.split('\n');
  const at = lines.findIndex(l => l.startsWith('## '));
  const section = [`## ${version} (unreleased)`, '', body, ''];
  if (at < 0) lines.push('', ...section);
  else lines.splice(at, 0, ...section);
  fs.writeFileSync(file, lines.join('\n').replace(/\n*$/, '\n'));
}

function bump(names, root = ROOT) {
  const list = packages(root);
  const chosen = names.includes('all') ? list : names.map(n => list.find(p => p.id === n) || error(`no package ${n}`));
  for (const p of chosen) {
    const [maj, min, patch] = p.pkg.version.split('.').map(Number);
    const version = `${maj}.${min}.${patch + 1}`;
    p.pkg.version = version;
    writeJson(p.file, p.pkg);
    addChangelogSection(p.dir, version, '- ');
    console.log(`${p.pkg.name} ${version}`);
  }
}

function moveToLine(root = ROOT) {
  const ln = line(root);
  const list = packages(root);
  const ours = new Set(list.map(p => p.pkg.name));
  for (const p of list) {
    // Already on this line: keeps its patch.
    const onLine = (p.pkg.version || '').startsWith(`${ln}.`);
    const version = onLine ? p.pkg.version : `${ln}.0`;
    if (!onLine) {
      p.pkg.version = version;
      addChangelogSection(p.dir, version, `- For React Native ${ln}.`);
    }
    for (const dep of Object.keys(p.pkg.peerDependencies || {})) {
      if (ours.has(dep)) p.pkg.peerDependencies[dep] = peerRange(ln);
    }
    writeJson(p.file, p.pkg);
    console.log(`${p.pkg.name} ${version}`);
  }
}

/** Whether GitHub Packages has name@version (npm view; a 404 is no). */
function published(name, version, exec = execFileSync) {
  try {
    const out = exec('npm', ['view', `${name}@${version}`, 'version', '--json', '--registry', REGISTRY], {
      cwd: ROOT,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    return out.trim() !== '';
  } catch (e) {
    if (/E404|404 Not Found|is not in this registry/.test(`${e.stderr}${e.stdout}`)) return false;
    throw new Error(`npm view ${name}@${version}: ${e.stderr || e.message}`);
  }
}

function branch() {
  if (process.env.GITHUB_REF_NAME && process.env.GITHUB_REF_TYPE !== 'tag') return process.env.GITHUB_REF_NAME;
  return execFileSync('git', ['rev-parse', '--abbrev-ref', 'HEAD'], {cwd: ROOT, encoding: 'utf8'}).trim();
}

function plan(exec) {
  const ln = line();
  const b = branch();
  const tags = distTags(b, ln);
  return {
    branch: b,
    line: ln,
    distTags: tags,
    packages: packages().map(p => ({
      id: p.id,
      name: p.pkg.name,
      version: p.pkg.version,
      dir: path.relative(ROOT, p.dir) || '.',
      gitTag: gitTag(p),
      published: published(p.pkg.name, p.pkg.version, exec),
    })),
  };
}

function sh(cmd, args, opts = {}) {
  console.log(`$ ${cmd} ${args.join(' ')}`);
  execFileSync(cmd, args, {cwd: ROOT, stdio: 'inherit', ...opts});
}

function publish(which, dryRun) {
  const p = plan();
  // A branch that isn't a release line (a pull request's) can only dry-run,
  // under the dist-tag of the line it's on.
  const tags = p.distTags || (dryRun ? [`v${p.line}-stable`] : fail(`${p.branch} isn't main or release/${p.line}`));
  const pending = p.packages.filter(q => !q.published && (which === 'core') === (q.id === 'core'));
  if (!pending.length) console.log(`nothing to publish (${which})`);
  for (const q of pending) {
    const dir = path.join(ROOT, q.dir);
    sh('npm', ['publish', '--tag', tags[0], ...(dryRun ? ['--dry-run'] : [])], {cwd: dir});
    if (dryRun) continue;
    for (const t of tags.slice(1)) sh('npm', ['dist-tag', 'add', `${q.name}@${q.version}`, t]);
    // The package's tag comes with its GitHub release (the workflow).
    if (q.id !== 'core') {
      sh('git', ['tag', q.gitTag]);
      sh('git', ['push', 'origin', `refs/tags/${q.gitTag}`]);
    }
  }
}

function error(message) {
  throw new Error(message);
}

function fail(message) {
  console.error(message);
  process.exit(1);
}

module.exports = {line, packages, peerRange, distTags, gitTag, problems, published, bump, moveToLine};

if (require.main === module) {
  const [cmd, ...args] = process.argv.slice(2);
  if (cmd === 'check') {
    const out = problems();
    for (const m of out) console.error(m);
    if (out.length) process.exit(1);
    console.log(`every package is on React Native ${line()}`);
  } else if (cmd === 'bump' && args.length) {
    bump(args);
  } else if (cmd === 'line') {
    moveToLine();
  } else if (cmd === 'plan') {
    const p = plan();
    if (args.includes('--json')) {
      console.log(JSON.stringify(p));
    } else {
      console.log(`${p.branch}: React Native ${p.line}, dist-tags ${p.distTags ? p.distTags.join(', ') : '(none: not a release branch)'}`);
      for (const q of p.packages) console.log(`  ${q.published ? 'published  ' : 'to publish '} ${q.name}@${q.version}`);
    }
  } else if (cmd === 'publish' && ['core', 'libraries'].includes(args[0])) {
    publish(args[0], args.includes('--dry-run'));
  } else {
    console.error('usage: release.js check | bump NAME...|all | line | plan [--json] | publish core|libraries [--dry-run]');
    process.exit(2);
  }
}
