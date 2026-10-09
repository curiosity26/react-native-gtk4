/**
 * The CLI commands this package adds to `react-native` (through
 * react-native.config.js): init-linux and run-linux.
 */
'use strict';

const {initLinux} = require('./initLinux');
const {runLinux, RunError} = require('./runLinux');
const {linuxLibraries} = require('./autolinking');
const {initLinuxLibrary} = require('./initLinuxLibrary');
const {packageLinux, FORMATS} = require('./packageLinux');

function fail(e) {
  if (e instanceof RunError) {
    console.error(`\nerror: ${e.message}`);
    process.exitCode = 1;
    return;
  }
  throw e;
}

const initLinuxCommand = {
  name: 'init-linux',
  description: 'Add a Linux (GTK4) app to this React Native project',
  func: async (_argv, config, args) => {
    const summary = initLinux(config.root, {overwrite: !!args.overwrite, appId: args.appId});
    const install = summary.packageChanges.some(c => c.includes('dependencies'))
      ? 'Install the new dependency (npm install), then run'
      : 'Run it with';
    console.log(
      `\nLinux app "${summary.names.name}" (${summary.names.appId}) is set up. ` +
        `${install}:\n  npx react-native run-linux`,
    );
  },
  options: [
    {name: '--overwrite', description: "Replace linux/'s files if they exist"},
    {
      name: '--app-id <id>',
      description: 'GApplication id, e.g. com.example.MyApp (default com.<name>)',
    },
  ],
};

const runLinuxCommand = {
  name: 'run-linux',
  description: 'Build and run the Linux (GTK4) app',
  func: async (_argv, config, args) => {
    try {
      await runLinux(config.root, {
        release: !!args.release,
        packager: args.packager !== false,
        port: Number(args.port) || 8081,
        launch: args.launch !== false,
        buildOnly: !!args.buildOnly,
        logging: !!args.logging,
        smoke: !!args.smoke,
        screenshot: args.screenshot,
        terminal: args.terminal,
        checks: args.checks !== false,
        libraries: linuxLibraries(config.dependencies),
      });
    } catch (e) {
      fail(e);
    }
  },
  options: [
    {name: '--release', description: 'Release build: bundle the JS and assets with the app'},
    {name: '--no-packager', description: "Don't start Metro (Debug)"},
    {name: '--port <number>', description: "Metro's port", default: 8081},
    {name: '--no-launch', description: "Build, but don't launch the app"},
    {name: '--build-only', description: 'Only build (no Metro, no launch)'},
    {name: '--logging', description: 'Show build output and keep the app attached, with its logs'},
    {name: '--smoke', description: 'Launch with --smoke: exit once the app has mounted'},
    {name: '--screenshot <png>', description: 'With --smoke: save a screenshot'},
    {name: '--terminal <app>', description: 'Terminal to start Metro in'},
    {name: '--no-checks', description: "Skip the prerequisite checks"},
  ],
};

const packageLinuxCommand = {
  name: 'package-linux',
  description: 'Build the Linux (GTK4) app for Release and package it',
  func: async (_argv, config, args) => {
    try {
      await packageLinux(config.root, {
        format: args.format,
        output: args.output,
        version: args.version,
        local: !!args.local,
        manifestOnly: !!args.manifestOnly,
        prefix: args.prefix,
        host: args.host,
        logging: !!args.logging,
        smoke: !!args.smoke,
        checks: args.checks !== false,
        libraries: linuxLibraries(config.dependencies),
      });
    } catch (e) {
      fail(e);
    }
  },
  options: [
    {name: '--format <format>', description: `Package format: ${FORMATS.join(', ')}`, default: 'dir'},
    {name: '--output <dir>', description: 'Where the package goes', default: 'linux/build/package'},
    {name: '--version <version>', description: "The app's version (default: app.json's linux.version, or package.json's)"},
    {name: '--local', description: 'Flatpak: build from the project\'s directories (no tarballs), with ccache'},
    {name: '--manifest-only', description: 'Flatpak: write the manifest and its sources, but don\'t build'},
    {name: '--prefix <dir>', description: 'dir: install straight into this prefix (what the Flatpak build does)'},
    {name: '--host <dir>', description: "The host library's CMake config dir (default: build it into the cache)"},
    {name: '--logging', description: 'Show all build output'},
    {name: '--smoke', description: 'Launch the packaged app with --smoke afterwards (Flatpak: installs the bundle for your user)'},
    {name: '--no-checks', description: 'Skip the prerequisite checks'},
  ],
};

const initLinuxLibraryCommand = {
  name: 'init-linux-library',
  description: "Add Linux native code (a TurboModule and a native component) to this library",
  func: async (_argv, config, args) => {
    try {
      const summary = initLinuxLibrary(config.root, {overwrite: !!args.overwrite, name: args.name});
      console.log(
        `\nlinux/ is set up: CMake target "${summary.names.target}", package function ` +
          `${summary.names.packageFunction}(). Apps that depend on this library build and ` +
          'register it with run-linux (autolinking).',
      );
    } catch (e) {
      fail(e);
    }
  },
  options: [
    {name: '--name <name>', description: 'The module name, e.g. Calendar (default: from package.json)'},
    {name: '--overwrite', description: 'Replace files that exist'},
  ],
};

module.exports = [initLinuxCommand, runLinuxCommand, packageLinuxCommand, initLinuxLibraryCommand];
