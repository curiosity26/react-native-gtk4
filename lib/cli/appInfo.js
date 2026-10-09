/**
 * The app's identity on the desktop, from app.json's `linux` block, and the
 * files that carry it: a .desktop file, AppStream MetaInfo, hicolor icons
 * and the license.
 *
 *   {
 *     "name": "MyApp",
 *     "displayName": "My App",
 *     "linux": {
 *       "appId": "com.example.MyApp",      GApplication id, .desktop name
 *       "displayName": "My App",
 *       "summary": "Does one thing well",  one line, no full stop
 *       "description": "Paragraphs.\n\nSeparated by blank lines.",
 *       "icon": "linux/icon.svg",          SVG, or a square PNG (>= 128px)
 *       "categories": ["Utility"],         freedesktop.org main categories
 *       "version": "1.0.0",                default: package.json's
 *       "license": "MIT",                  SPDX; default: package.json's
 *       "licenseFile": "LICENSE",          default: LICENSE, COPYING, ...
 *       "homepage": "https://example.com",
 *       "developer": {"id": "com.example", "name": "Example Ltd"},
 *       "screenshots": [{"url": "https://...png", "caption": "Main window"}],
 *       "releases": [{"version": "1.0.0", "date": "2026-01-31", "description": "..."}],
 *       "maintainer": "Name <email>",       .deb (default: package.json's author)
 *       "deb": {"package": "my-app", "section": "utils"}
 *     }
 *   }
 */
'use strict';

const {spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');
const {appNames} = require('./initLinux');

// Sizes of the hicolor PNGs made from the icon (no upscaling a PNG).
const ICON_SIZES = [16, 24, 32, 48, 64, 128, 256, 512];
const LICENSE_FILES = ['LICENSE', 'LICENSE.md', 'LICENSE.txt', 'COPYING', 'COPYING.md'];
const MAIN_CATEGORIES = [
  'AudioVideo', 'Audio', 'Video', 'Development', 'Education', 'Game', 'Graphics',
  'Network', 'Office', 'Science', 'Settings', 'System', 'Utility',
];

class AppInfoError extends Error {}

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    return {};
  }
}

/**
 * app.json's `linux` block with its defaults filled in. `version` overrides
 * the version (package-linux --version).
 */
function appInfo(projectRoot, {version} = {}) {
  const appJson = readJson(path.join(projectRoot, 'app.json'));
  const pkg = readJson(path.join(projectRoot, 'package.json'));
  const linux = appJson.linux || {};
  const names = appNames(projectRoot, {appId: linux.appId});
  const displayName = linux.displayName || names.title;
  const info = {
    name: names.name,
    moduleName: names.moduleName,
    appId: names.appId,
    displayName,
    summary: linux.summary || `${displayName}, a React Native app`,
    description: linux.description || '',
    icon: linux.icon ? path.resolve(projectRoot, linux.icon) : null,
    categories: linux.categories || ['Utility'],
    keywords: linux.keywords || [],
    version: String(version || linux.version || pkg.version || '0.0.0'),
    license: linux.license || pkg.license || 'LicenseRef-proprietary',
    licenseFile: licenseFile(projectRoot, linux.licenseFile),
    homepage: linux.homepage || homepageOf(pkg),
    developer: linux.developer || null,
    screenshots: linux.screenshots || [],
    releases: linux.releases || [],
    // .deb: "Name <email>", and its overrides.
    maintainer: linux.maintainer || null,
    deb: linux.deb || null,
  };
  return info;
}

function homepageOf(pkg) {
  if (pkg.homepage) return pkg.homepage;
  const repo = typeof pkg.repository === 'string' ? pkg.repository : pkg.repository?.url;
  const m = repo && repo.match(/github\.com[/:]([^/]+\/[^/.]+)/);
  return m ? `https://github.com/${m[1]}` : null;
}

function licenseFile(projectRoot, configured) {
  if (configured) return path.resolve(projectRoot, configured);
  for (const name of LICENSE_FILES) {
    const file = path.join(projectRoot, name);
    if (fs.existsSync(file)) return file;
  }
  return null;
}

/** Problems that would make a broken package: [] when there are none. */
function validateAppInfo(info) {
  const problems = [];
  // A GApplication id that's also a valid D-Bus name and AppStream id:
  // reverse DNS, at least three segments for Flathub, no '-' in the last.
  if (!/^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_-]*)+$/.test(info.appId) || info.appId.length > 255) {
    problems.push(`appId "${info.appId}" isn't a reverse-DNS application id (com.example.MyApp)`);
  }
  if (!info.icon) {
    problems.push('no icon: set linux.icon in app.json (an SVG, or a square PNG of 128px or more)');
  } else if (!fs.existsSync(info.icon)) {
    problems.push(`the icon ${info.icon} doesn't exist`);
  } else if (!/\.(svg|png)$/i.test(info.icon)) {
    problems.push(`the icon ${info.icon} isn't an SVG or a PNG`);
  }
  if (!info.categories.some(c => MAIN_CATEGORIES.includes(c))) {
    problems.push(`categories need one of the main ones (${MAIN_CATEGORIES.join(', ')})`);
  }
  if (!/^[0-9][0-9A-Za-z.+~-]*$/.test(info.version)) {
    problems.push(`version "${info.version}" should start with a digit (1.0.0)`);
  }
  return problems;
}

/** A desktop entry value: one line, with its escapes. */
function desktopValue(s) {
  return String(s).replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\t/g, '\\t');
}

function desktopList(items) {
  return items.map(i => `${String(i).replace(/;/g, '\\;')};`).join('');
}

/** The .desktop file (share/applications/<appId>.desktop). */
function desktopEntry(info) {
  const lines = [
    '[Desktop Entry]',
    'Type=Application',
    `Name=${desktopValue(info.displayName)}`,
    `Comment=${desktopValue(info.summary)}`,
    `Exec=${info.name}`,
    `Icon=${info.appId}`,
    'Terminal=false',
    `Categories=${desktopList(info.categories)}`,
  ];
  if (info.keywords.length) lines.push(`Keywords=${desktopList(info.keywords)}`);
  lines.push('StartupNotify=true', 'X-GNOME-UsesNotifications=true');
  return `${lines.join('\n')}\n`;
}

function xml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/** Paragraphs (blank-line separated) and "- " lists as AppStream markup. */
function descriptionMarkup(text, indent) {
  const out = [];
  for (const block of String(text).trim().split(/\n\s*\n/)) {
    const lines = block.split('\n').map(l => l.trim()).filter(Boolean);
    if (!lines.length) continue;
    if (lines.every(l => /^[-*] /.test(l))) {
      out.push(`${indent}<ul>`);
      lines.forEach(l => out.push(`${indent}  <li>${xml(l.slice(2))}</li>`));
      out.push(`${indent}</ul>`);
    } else {
      out.push(`${indent}<p>${xml(lines.join(' '))}</p>`);
    }
  }
  return out;
}

function isoDate(date) {
  return date.toISOString().slice(0, 10);
}

/** The release date: SOURCE_DATE_EPOCH's (reproducible builds), or today. */
function releaseDate(env = process.env) {
  const epoch = Number(env.SOURCE_DATE_EPOCH);
  return isoDate(Number.isFinite(epoch) && epoch > 0 ? new Date(epoch * 1000) : new Date());
}

/** AppStream MetaInfo (share/metainfo/<appId>.metainfo.xml). */
function metainfo(info, {date = releaseDate()} = {}) {
  const l = [
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<component type="desktop-application">',
    `  <id>${xml(info.appId)}</id>`,
    '  <metadata_license>CC0-1.0</metadata_license>',
    `  <project_license>${xml(info.license)}</project_license>`,
    `  <name>${xml(info.displayName)}</name>`,
    `  <summary>${xml(info.summary)}</summary>`,
    '  <description>',
    ...descriptionMarkup(info.description || info.summary, '    '),
    '  </description>',
  ];
  if (info.developer) {
    const id = info.developer.id ? ` id="${xml(info.developer.id)}"` : '';
    l.push(`  <developer${id}>`, `    <name>${xml(info.developer.name || info.developer.id)}</name>`, '  </developer>');
  }
  l.push(`  <launchable type="desktop-id">${xml(info.appId)}.desktop</launchable>`);
  if (info.homepage) l.push(`  <url type="homepage">${xml(info.homepage)}</url>`);
  l.push('  <provides>', `    <binary>${xml(info.name)}</binary>`, '  </provides>');
  if (info.screenshots.length) {
    l.push('  <screenshots>');
    info.screenshots.forEach((s, i) => {
      const shot = typeof s === 'string' ? {url: s} : s;
      l.push(`    <screenshot${i === 0 ? ' type="default"' : ''}>`);
      if (shot.caption) l.push(`      <caption>${xml(shot.caption)}</caption>`);
      l.push(`      <image>${xml(shot.url)}</image>`, '    </screenshot>');
    });
    l.push('  </screenshots>');
  }
  l.push(
    '  <supports>',
    '    <control>pointing</control>',
    '    <control>keyboard</control>',
    '  </supports>',
    '  <content_rating type="oars-1.1"/>',
    '  <releases>',
  );
  const releases = info.releases.length ? info.releases : [{version: info.version, date}];
  if (!releases.some(r => r.version === info.version)) releases.unshift({version: info.version, date});
  for (const r of releases) {
    const attrs = `version="${xml(r.version)}" date="${xml(r.date || date)}"`;
    if (r.description) {
      l.push(`    <release ${attrs}>`, '      <description>', ...descriptionMarkup(r.description, '        '), '      </description>', '    </release>');
    } else {
      l.push(`    <release ${attrs}/>`);
    }
  }
  l.push('  </releases>', '</component>');
  return `${l.join('\n')}\n`;
}

// Renders an icon at the given sizes with GdkPixbuf (python3 gi, which
// GNOME and GTK desktops have). argv: icon, output dir, sizes...
const RENDER_ICON = `
import sys, os, gi
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf
src, out, sizes = sys.argv[1], sys.argv[2], [int(s) for s in sys.argv[3:]]
for size in sizes:
    pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(src, size, size, True)
    d = os.path.join(out, '%dx%d' % (size, size), 'apps')
    os.makedirs(d, exist_ok=True)
    pixbuf.savev(os.path.join(d, 'icon.png'), 'png', [], [])
`;

/** A PNG's width and height, from its IHDR chunk. */
function pngSize(file) {
  const b = fs.readFileSync(file);
  if (b.length < 24 || b.toString('ascii', 1, 4) !== 'PNG') throw new AppInfoError(`${file} isn't a PNG`);
  return {width: b.readUInt32BE(16), height: b.readUInt32BE(20)};
}

/**
 * share/icons/hicolor/: the SVG as scalable/apps/<appId>.svg plus PNGs, or
 * PNGs from a square PNG (at its size and below). Returns the files made.
 */
function writeIcons(info, shareDir) {
  const hicolor = path.join(shareDir, 'icons', 'hicolor');
  const made = [];
  const svg = /\.svg$/i.test(info.icon);
  let sizes = ICON_SIZES;
  if (svg) {
    const dest = path.join(hicolor, 'scalable', 'apps', `${info.appId}.svg`);
    fs.mkdirSync(path.dirname(dest), {recursive: true});
    fs.copyFileSync(info.icon, dest);
    made.push(dest);
  } else {
    const {width, height} = pngSize(info.icon);
    if (width !== height) throw new AppInfoError(`the icon ${info.icon} is ${width}x${height}; it must be square`);
    if (width < 128) throw new AppInfoError(`the icon ${info.icon} is ${width}px; it must be 128px or more (or an SVG)`);
    sizes = ICON_SIZES.filter(s => s <= width);
  }
  const tmp = fs.mkdtempSync(path.join(shareDir, '.icons-'));
  const r = spawnSync('python3', ['-c', RENDER_ICON, info.icon, tmp, ...sizes.map(String)], {encoding: 'utf8'});
  if (r.status === 0) {
    for (const size of sizes) {
      const dest = path.join(hicolor, `${size}x${size}`, 'apps', `${info.appId}.png`);
      fs.mkdirSync(path.dirname(dest), {recursive: true});
      fs.renameSync(path.join(tmp, `${size}x${size}`, 'apps', 'icon.png'), dest);
      made.push(dest);
    }
  } else if (!svg) {
    // Without GdkPixbuf: the PNG at its own size, when that's a hicolor one.
    const {width} = pngSize(info.icon);
    const size = ICON_SIZES.includes(width) ? width : null;
    if (!size) throw new AppInfoError(`can't scale ${info.icon} (python3 with GdkPixbuf is missing); use a 128, 256 or 512px PNG, or an SVG`);
    const dest = path.join(hicolor, `${size}x${size}`, 'apps', `${info.appId}.png`);
    fs.mkdirSync(path.dirname(dest), {recursive: true});
    fs.copyFileSync(info.icon, dest);
    made.push(dest);
  }
  fs.rmSync(tmp, {recursive: true, force: true});
  return made;
}

/**
 * Writes `<dir>/share/{applications,metainfo,icons,licenses}`: what the app's
 * CMake install step adds to the prefix (rngtk_app installs
 * <build>/packaging/share). Returns the files written.
 */
function writePackagingFiles(info, dir, {packageLicense, date} = {}) {
  fs.rmSync(dir, {recursive: true, force: true});
  const share = path.join(dir, 'share');
  const files = [];
  const write = (rel, text) => {
    const file = path.join(share, rel);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, text);
    files.push(file);
  };
  write(path.join('applications', `${info.appId}.desktop`), desktopEntry(info));
  write(path.join('metainfo', `${info.appId}.metainfo.xml`), metainfo(info, {date}));
  files.push(...writeIcons(info, share));
  const licenses = path.join('licenses', info.appId);
  if (info.licenseFile) write(path.join(licenses, path.basename(info.licenseFile)), fs.readFileSync(info.licenseFile));
  // React Native for GTK4's own license: the host libraries ship with the app.
  if (packageLicense) write(path.join(licenses, 'react-native-gtk4', 'LICENSE'), fs.readFileSync(packageLicense));
  return files;
}

module.exports = {
  AppInfoError,
  ICON_SIZES,
  appInfo,
  validateAppInfo,
  desktopEntry,
  metainfo,
  releaseDate,
  pngSize,
  writeIcons,
  writePackagingFiles,
};
