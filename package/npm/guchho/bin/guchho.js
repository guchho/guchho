#!/usr/bin/env node

"use strict";

const { execFileSync } = require("child_process");

const { resolveEngine } = require("../lib/main");

// A "guchho.config.js" or "guchho.config.json" may name plugins, and a plugin
// is JavaScript the engine's process cannot call. When the nearest config names
// some, the command is run through the service instead of the bare binary, so a
// plugin host is on the far end of the pipe to answer the hooks. When it does
// not — which is most projects — the engine chosen by resolveEngine runs
// directly: the WebAssembly one in-process, or the native binary as before.
const BUILTIN_PLUGINLESS = ["build", "dev", "serve", "watch"];

function runBinary(binaryPath, args) {
  try {
    execFileSync(binaryPath, args, { stdio: "inherit" });
  } catch (err) {
    if (err.status !== undefined) {
      process.exit(err.status);
    }
    throw err;
  }
}

// Runs the command through the WebAssembly engine, in this process. The engine
// mounts the host file system and maps paths the same way its own bin script
// does, so a relative entry point means the same thing as under the native
// binary. Plugins cannot travel this way — they need the service protocol —
// and that refusal is made before the engine is loaded, so a plugin project
// never pays for a wasm instantiation it will not use.
async function runWasm(args) {
  const { runCli } = require("@guchho/wasm");
  const code = await runCli(args);
  process.exit(typeof code === "number" ? code : 0);
}

// The config's plugins, grouped by configuration element, or none. A config that
// cannot be read here is left for the engine to report in its own words — the
// fallback is the plain engine, not a second opinion about what is wrong with
// the file.
async function configPluginGroups() {
  try {
    const { loadConfigPluginGroups } = require("../lib/config");
    return await loadConfigPluginGroups(process.cwd());
  } catch {
    return [];
  }
}

async function runWithPlugins(args, pluginGroups) {
  const { Service } = require("../lib/service");
  const { makePluginHost, callWithPlugins } = require("../lib/plugins");

  const service = new Service();
  await service.start();

  // Ctrl-C at the terminal reaches this process and, through the shared process
  // group, the service child. This handler is the belt to that suspenders: it
  // makes sure the child is gone before this process is, so a run is not left
  // orphaned holding the terminal.
  const stop = () => {
    if (service.child !== null) {
      try {
        service.child.kill();
      } catch {
        // Already gone.
      }
    }
    process.exit(130);
  };
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);

  try {
    const response = await callWithPlugins(service, makePluginHost(pluginGroups), {
      command: "run",
      argv: args,
      configPluginsHandled: true,
    });
    await service.stop();
    process.exit(typeof response.code === "number" ? response.code : 0);
  } catch (error) {
    await service.stop();
    process.stderr.write((error && error.message ? error.message : String(error)) + "\n");
    process.exit(1);
  }
}

async function main() {
  const args = process.argv.slice(2);
  const command = args[0];

  if (BUILTIN_PLUGINLESS.includes(command)) {
    const pluginGroups = await configPluginGroups();
    if (pluginGroups.some((group) => group.length > 0)) {
      // The service protocol speaks to a native child process; the WebAssembly
      // engine cannot host it. A plugin project therefore needs the native
      // binary. An explicit GUCHHO_BACKEND=wasm is refused rather than quietly
      // ignored — the setting asked for one engine and the config makes that
      // impossible, and running the other one anyway is not an answer.
      if (process.env.GUCHHO_BACKEND === "wasm") {
        process.stderr.write(
          "guchho: this project's configuration names plugins, which need the native engine, " +
            "but GUCHHO_BACKEND=wasm was set.\n" +
            "Unset GUCHHO_BACKEND (or set it to \"native\"), or remove the plugins from the configuration.\n"
        );
        process.exit(1);
      }
      // Asking for the native binary before starting the service, so a missing
      // one fails here with the error that names what to install rather than
      // from inside the service handshake.
      const { getBinaryPath } = require("../lib/main");
      getBinaryPath();
      await runWithPlugins(args, pluginGroups);
      return;
    }
  }

  const engine = resolveEngine();
  if (engine.type === "wasm") {
    await runWasm(args);
    return;
  }
  runBinary(engine.binaryPath, args);
}

main().catch((error) => {
  process.stderr.write((error && error.stack ? error.stack : String(error)) + "\n");
  process.exit(1);
});
