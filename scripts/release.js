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
 * Pull requests don't change versions: the release does. A package whose
 * files changed since its last release tag on this line (v0.87.1 for the
 * package, react-native-gtk4-svg@0.87.0 for a port) gets the next patch,
 * with the pull requests that changed it as its changelog section.
 *
 *   scripts/release.js check
 *       Checks the versions and the peer ranges between our packages
 *       against that rule.
 *   scripts/release.js line
 *       Moves every package not on React Native's major.minor to it, at .0
 *       (upgrade-rn.sh runs it).
 *   scripts/release.js plan [--json]
 *       What a release would publish from here, and under which dist-tags.
 *   scripts/release.js version
 *       Writes those versions and changelog sections (the release commit).
 *   scripts/release.js publish core|libraries [--dry-run]
 *       Publishes them, sets their dist-tags and tags them.
 */
'use strict';

const {execFileSync} = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const SCOPE = '@curiosity26';
const REGISTRY = 'https://npm.pkg.github.com';

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function writeJson(file, json) {
  fs.writeFileSync(file, JSON.stringify(json, null, 2) + '\n');
}

function git(root, args) {
  return execFileSync('git', args, {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
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

/** The npm dist-tags a branch publishes under: main is latest, release/X.Y is vX.Y-stable. */
function distTags(branch, ln) {
  if (branch === 'main') return ['latest', `v${ln}-stable`];
  const m = /^release\/(\d+\.\d+)$/.exec(branch || '');
  if (m) {
    if (m[1] !== ln) throw new Error(`branch ${branch} is on React Native ${ln} (rn-version.properties)`);
    return [`v${ln}-stable`];
  }
  return null;
}

/** The git tag prefix for a package's releases: v for the package (its GitHub releases), react-native-gtk4-svg@ for a port. */
function tagPrefix(p) {
  return p.id === 'core' ? 'v' : `${p.pkg.name.slice(SCOPE.length + 1)}@`;
}

/** What a package's release ships, as git pathspecs: the package's `files` (and package.json), or a port's folder. */
function pathspecs(p, root = ROOT) {
  if (p.id !== 'core') return [path.relative(root, p.dir)];
  const generated = new Set(['CHANGELOG.md', 'prebuilt-hosts.json']);
  return ['package.json', ...p.pkg.files.filter(f => !f.startsWith('!') && !generated.has(f)).map(f => f.replace(/\/$/, ''))];
}

const patchOf = v => Number(v.split('.')[2]);

/**
 * The version a release from HEAD gives a package, and the pull requests
 * since its last one; version is null when there's nothing to release.
 */
function next(p, ln, root = ROOT) {
  const prefix = tagPrefix(p);
  const tags = git(root, ['tag', '--merged', 'HEAD', '--list', `${prefix}${ln}.*`])
    .split('\n')
    .filter(t => new RegExp(`^${prefix.replace(/[.@]/g, '\\$&')}${ln.replace('.', '\\.')}\\.\\d+$`).test(t))
    .sort((a, b) => patchOf(a.slice(prefix.length)) - patchOf(b.slice(prefix.length)));
  const last = tags.pop();
  const current = p.pkg.version;
  if (!last) return {version: current, last: null, changes: []}; // the line's first release
  const released = last.slice(prefix.length);
  // A release commit whose publishing didn't finish: its version again.
  if (patchOf(current) > patchOf(released)) return {version: current, last, changes: []};
  // Each commit on the branch (a pull request's merge) against the one
  // before it, so a merge counts by what it brought in.
  const log = git(root, ['log', '--first-parent', '--reverse', '--format=%H%x1f%s%x1f%b%x1e', `${last}..HEAD`]);
  const specs = pathspecs(p, root);
  const changes = log
    .split('\x1e')
    .map(e => e.trim())
    .filter(Boolean)
    .filter(e => git(root, ['diff', '--name-only', `${e.split('\x1f')[0]}^`, e.split('\x1f')[0], '--', ...specs]) !== '')
    .map(e => {
      const [, subject, body] = e.split('\x1f');
      const m = /^Merge pull request #(\d+) from /.exec(subject);
      return m ? `${(body || '').split('\n')[0].trim() || subject} (#${m[1]})` : subject;
    })
    .filter(s => !/^Release: /.test(s));
  if (!changes.length) return {version: null, last, changes};
  const [maj, min] = released.split('.');
  return {version: `${maj}.${min}.${patchOf(released) + 1}`, last, changes};
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
  }
  return out;
}

/** Dates the version's section of CHANGELOG.md, or adds one with these lines. */
function writeChangelog(dir, version, lines, date) {
  const file = path.join(dir, 'CHANGELOG.md');
  const text = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '# Changelog\n';
  const rows = text.split('\n');
  const at = rows.findIndex(l => l.split(' ')[0] === '##' && l.split(' ')[1] === version);
  if (at >= 0) {
    rows[at] = `## ${version} (${date})`;
  } else {
    const first = rows.findIndex(l => l.startsWith('## '));
    const section = [`## ${version} (${date})`, '', ...(lines.length ? lines.map(l => `- ${l}`) : ['- Released.']), ''];
    rows.splice(first < 0 ? rows.length : first, 0, ...section);
  }
  fs.writeFileSync(file, rows.join('\n').replace(/\n*$/, '\n'));
}

function moveToLine(root = ROOT) {
  const ln = line(root);
  const list = packages(root);
  const ours = new Set(list.map(p => p.pkg.name));
  for (const p of list) {
    // Already on this line: keeps its patch.
    if (!(p.pkg.version || '').startsWith(`${ln}.`)) p.pkg.version = `${ln}.0`;
    for (const dep of Object.keys(p.pkg.peerDependencies || {})) {
      if (ours.has(dep)) p.pkg.peerDependencies[dep] = peerRange(ln);
    }
    writeJson(p.file, p.pkg);
    console.log(`${p.pkg.name} ${p.pkg.version}`);
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
  return git(ROOT, ['rev-parse', '--abbrev-ref', 'HEAD']);
}

function plan() {
  const ln = line();
  const b = branch();
  return {
    branch: b,
    line: ln,
    distTags: distTags(b, ln),
    packages: packages().map(p => {
      const n = next(p, ln);
      const release = n.version !== null && !published(p.pkg.name, n.version);
      return {
        id: p.id,
        name: p.pkg.name,
        dir: path.relative(ROOT, p.dir) || '.',
        current: p.pkg.version,
        version: n.version,
        last: n.last,
        changes: n.changes,
        release,
        gitTag: n.version && `${tagPrefix(p)}${n.version}`,
      };
    }),
  };
}

function version() {
  const date = new Date().toISOString().slice(0, 10);
  const list = packages();
  const released = [];
  for (const q of plan().packages.filter(q => q.release)) {
    const p = list.find(x => x.id === q.id);
    p.pkg.version = q.version;
    writeJson(p.file, p.pkg);
    writeChangelog(p.dir, q.version, q.changes, date);
    released.push(`${q.id} ${q.version}`);
  }
  console.log(released.length ? `Release: ${released.join(', ')}` : '');
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
  const pending = p.packages.filter(q => q.release && (which === 'core') === (q.id === 'core'));
  if (!pending.length) console.log(`nothing to publish (${which})`);
  for (const q of pending) {
    const dir = path.join(ROOT, q.dir);
    if (readJson(path.join(dir, 'package.json')).version !== q.version) {
      fail(`${q.name} is ${q.current} here, not ${q.version}: run scripts/release.js version first`);
    }
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

function fail(message) {
  console.error(message);
  process.exit(1);
}

module.exports = {line, packages, peerRange, distTags, tagPrefix, pathspecs, next, problems, published, writeChangelog, moveToLine};

if (require.main === module) {
  const [cmd, ...args] = process.argv.slice(2);
  if (cmd === 'check') {
    const out = problems();
    for (const m of out) console.error(m);
    if (out.length) process.exit(1);
    console.log(`every package is on React Native ${line()}`);
  } else if (cmd === 'line') {
    moveToLine();
  } else if (cmd === 'plan') {
    const p = plan();
    if (args.includes('--json')) {
      console.log(JSON.stringify(p));
    } else {
      console.log(`${p.branch}: React Native ${p.line}, dist-tags ${p.distTags ? p.distTags.join(', ') : '(none: not a release branch)'}`);
      for (const q of p.packages) {
        console.log(`  ${q.release ? `release ${q.version}` : 'no release'}  ${q.name} (${q.last ? `since ${q.last}` : 'first on this line'})`);
        for (const c of q.changes) console.log(`      - ${c}`);
      }
    }
  } else if (cmd === 'version') {
    version();
  } else if (cmd === 'publish' && ['core', 'libraries'].includes(args[0])) {
    publish(args[0], args.includes('--dry-run'));
  } else {
    console.error('usage: release.js check | line | plan [--json] | version | publish core|libraries [--dry-run]');
    process.exit(2);
  }
}
