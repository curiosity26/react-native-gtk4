/**
 * The CLI commands this package adds to `react-native` (through
 * react-native.config.js): init-linux and run-linux.
 */
'use strict';

const {initLinux} = require('./initLinux');
const {runLinux, RunError} = require('./runLinux');

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

module.exports = [initLinuxCommand, runLinuxCommand];
