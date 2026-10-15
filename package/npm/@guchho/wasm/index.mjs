// =============================================================================
// @guchho/wasm — the JavaScript surface of the WebAssembly engine
// =============================================================================
//
// The engine itself is the same C++ binary the native npm packages ship,
// compiled to WebAssembly and wrapped in Emscripten's MODULARIZE glue. This
// file is the thin layer that makes that binary usable from JavaScript:
//
//   Node (the primary target): the glue's filesystem starts empty, so the
//   host's real file system is mounted through NODEFS at "/host" and the
//   module's working directory is changed to the mount point corresponding
//   to process.cwd(). After that every relative path — entry points,
//   --outdir, imports walked by the resolver — behaves exactly as it does in
//   the native binary, and outputs land on the real disk with no copy-back.
//   -sNODERAWFS is deliberately not used: it is broken on Windows
//   (NotFound: FileSystem.access), and a mount is enough.
//
//   Browser (secondary): MEMFS only. Passing a `files` map to createGuchho
//   populates the in-memory tree; build() then returns the outputs as bytes.
//   No COOP/COEP headers are needed because the engine is single-threaded —
//   there are no pthreads to fence.
//
// The CLI is the same story one level down: bin/guchho.js calls runCli with
// process.argv, which mounts the host and hands the arguments straight to
// callMain. Flag grammar, diagnostics and exit codes are the engine's own.

import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const isNode = typeof process !== "undefined" && process.versions?.node;

// Loads the Emscripten glue as an ES module. The .wasm binary is resolved
// relative to this package file rather than the working directory, so the
// engine can be installed anywhere and still find its own binary.
async function instantiate(options = {}) {
    const { default: createGuchhoModule } = await import("./lib/guchho.js");

    const moduleConfig = {
        locateFile: (file) => path.join(here, "lib", file),
        noInitialRun: true,
    };
    if (options.print) moduleConfig.print = options.print;
    if (options.printErr) moduleConfig.printErr = options.printErr;

    return createGuchhoModule(moduleConfig);
}

// Maps an absolute host path to its "/host" counterpart inside the module's
// file system. "C:\proj" becomes "/host/proj"; "/home/u/proj" becomes
// "/host/home/u/proj". The drive root (or "/" on POSIX) is what gets mounted,
// so the resolver's walk up towards node_modules crosses out of the project
// the same way it does natively.
function toVirtual(absPath) {
    const root = path.parse(absPath).root;
    const rest = absPath.slice(root.length).replace(/\\/g, "/");
    return "/host" + (rest === "" ? "" : "/" + rest);
}

// Mounts the host file system and chdirs into the mount point for "cwd".
// Returns the helpers a caller needs to translate paths the other way.
function mountHost(module, cwd = process.cwd()) {
    const FS = module.FS;
    const abs = path.resolve(cwd);
    const root = path.parse(abs).root;

    try {
        FS.mkdir("/host");
    } catch {
        // Already there — a second engine in the same process is fine.
    }
    FS.mount(FS.filesystems.NODEFS, { root }, "/host");
    FS.chdir(toVirtual(abs));

    return { FS, root, virtual: (p) => toVirtual(path.resolve(abs, p)) };
}

// Populates the in-memory file system from a {"src/index.html": "..."} map.
// This is the browser path, and it is also the Node path when a caller wants
// to build something that never touches disk.
function writeFiles(FS, files) {
    for (const [relPath, content] of Object.entries(files)) {
        const abs = path.posix.resolve("/", relPath);
        const parts = abs.split("/").filter(Boolean);
        let dir = "";
        for (const part of parts.slice(0, -1)) {
            dir = path.posix.join(dir, part);
            try {
                FS.mkdir(dir);
            } catch {
                // Exists — fine.
            }
        }
        FS.writeFile(abs, content);
    }
}

// Creates an engine instance.
//
// Input:  options.cwd        — Node only; the directory to mount and chdir to
//                             (default process.cwd()).
//         options.files      — a { path: string|Uint8Array } map written into
//                             MEMFS before the first call (browser / sandbox).
//         options.print / options.printErr
//                           — replace the console sinks diagnostics use.
// Output: an engine object: { module, FS, callMain, build, version }.
export async function createGuchho(options = {}) {
    const module = await instantiate(options);

    let virtual = (p) => toVirtual(path.resolve(p));
    if (isNode && !options.files) {
        mountHost(module, options.cwd);
        virtual = (p) => toVirtual(path.resolve(options.cwd ?? process.cwd(), p));
    } else if (options.files) {
        writeFiles(module.FS, options.files);
        module.FS.chdir("/");
    }

    const engine = {
        module,
        FS: module.FS,

        // Runs the CLI exactly as the native binary would. Arguments are the
        // ones after the program name. Returns the exit code.
        //
        // Emscripten's callMain does not merely return main's code — when the
        // glue is built for Node its quit hook assigns the code to
        // process.exitCode, so a build that fails inside an embedded engine
        // would take the embedding process down with it on the way out. The
        // engine's own code is returned to the caller, and the host's exit
        // status is left as the host set it: the two are different things, and
        // only the second one is the host's to end with.
        callMain(argv) {
            const hostExitCode = isNode ? process.exitCode : undefined;
            try {
                return module.callMain(argv);
            } finally {
                if (isNode) process.exitCode = hostExitCode;
            }
        },

        // One-shot build through the C ABI. Node paths are host paths;
        // in-memory builds pass a files map at creation time and use paths
        // relative to "/".
        build(entry, opts = {}) {
            const flags = {};
            if (opts.minify) flags.minify = true;
            if (opts.quiet) flags.quiet = true;
            if (opts.verbose) flags.verbose = true;
            if (opts.name) flags.name = opts.name;

            const code = module.ccall(
                "guchho_build",
                "number",
                ["string", "string", "string"],
                [entry, opts.outdir ?? "", Object.keys(flags).length ? JSON.stringify(flags) : ""],
            );
            if (code !== 0) {
                const message = module.ccall("guchho_last_error", "string", [], []);
                throw new Error(message || `guchho build failed with exit code ${code}`);
            }
            return { exitCode: 0 };
        },

        version() {
            return module.ccall("guchho_version", "string", [], []);
        },

        // Mounts a files map into this instance after creation.
        loadFiles(files) {
            writeFiles(module.FS, files);
        },

        dispose() {
            // The module frees itself with the process; nothing is retained
            // on this side beyond the object returned here.
        },
    };

    return engine;
}

// The CLI entry the npm bin field points at: mount, run, exit. Kept here so
// tests and hosts that spawn themselves get the same path mapping the bin
// script does.
export async function runCli(argv) {
    const engine = await createGuchho();
    return engine.callMain(argv);
}

export { toVirtual, mountHost };
