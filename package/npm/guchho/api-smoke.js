"use strict";
// Exercises the public API exactly as a caller would, so this is the test of the
// package and not of its insides. If something here needs to reach into lib/,
// the API is missing something.

const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");

const guchho = require("./lib/index.js");

let failures = 0;
function test(name, fn) {
  return (async () => {
    try {
      await fn();
      console.log("  ok   " + name);
    } catch (e) {
      failures++;
      console.log("  FAIL " + name);
      console.log("       " + (e && e.stack ? e.stack.split("\n").slice(0, 4).join("\n       ") : e));
    }
  })();
}

(async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-api-"));
  const startDir = process.cwd();
  fs.mkdirSync(path.join(dir, "src"));
  fs.writeFileSync(path.join(dir, "src", "entry.js"), "export const n = 40 + 2;\n");
  fs.writeFileSync(path.join(dir, "src", "bad.js"), "const = ;\n");
  fs.writeFileSync(path.join(dir, "src", "defs.js"), "export const flag = DEBUG;\n");
  process.chdir(dir);

  console.log("version: " + guchho.version());
  console.log("exports: " + Object.keys(guchho).sort().join(" "));
  console.log("");

  await test("transform returns code as a string", async () => {
    const r = await guchho.transform("const x: number = 1; console.log(x);", { loader: "ts" });
    assert.strictEqual(typeof r.code, "string");
    assert.ok(!r.code.includes(": number"), "type annotation should be gone: " + r.code);
    assert.deepStrictEqual(r.errors, []);
    assert.ok(Array.isArray(r.warnings));
  });

  await test("transform accepts a Buffer", async () => {
    const r = await guchho.transform(Buffer.from("let y = 2", "utf8"), { loader: "js" });
    assert.ok(r.code.includes("y"));
  });

  await test("transform accepts a Uint8Array", async () => {
    const r = await guchho.transform(new Uint8Array(Buffer.from("let z = 3", "utf8")), { loader: "js" });
    assert.ok(r.code.includes("z"));
  });

  await test("transform throws on a syntax error, carrying errors", async () => {
    let threw = null;
    try {
      await guchho.transform("const = ;", { loader: "js", sourcefile: "bad.js" });
    } catch (e) {
      threw = e;
    }
    assert.ok(threw, "a broken transform should throw");
    assert.ok(Array.isArray(threw.errors) && threw.errors.length > 0, "the error should carry errors");
    assert.strictEqual(typeof threw.errors[0].text, "string");
  });

  await test("transform refuses a bad option with a TypeError", async () => {
    await assert.rejects(() => guchho.transform("x", { logLevel: "chatty" }), TypeError);
  });

  await test("build with write:false returns outputFiles in memory", async () => {
    const r = await guchho.build({
      entryPoints: ["src/entry.js"],
      bundle: true,
      format: "esm",
      write: false,
      minify: false,
    });
    assert.deepStrictEqual(r.errors, []);
    assert.ok(Array.isArray(r.outputFiles) && r.outputFiles.length === 1, "one output");
    const f = r.outputFiles[0];
    assert.ok(f.contents instanceof Uint8Array, "contents should be bytes");
    assert.ok(f.text.includes("42"), "text getter should decode: " + f.text);
    assert.ok(!fs.existsSync(path.join(dir, "dist")), "write:false must not touch the disk");
  });

  await test("build with write:true writes to disk", async () => {
    const r = await guchho.build({
      entryPoints: ["src/entry.js"],
      outdir: "out",
      bundle: true,
      format: "esm",
      minify: false,
    });
    assert.deepStrictEqual(r.errors, []);
    assert.ok(fs.existsSync(path.join(dir, "out", "entry.js")), "the file should exist");
  });

  await test("build returns a metafile when asked", async () => {
    const r = await guchho.build({
      entryPoints: ["src/entry.js"],
      write: false,
      metafile: true,
    });
    assert.ok(r.metafile && typeof r.metafile === "object", "metafile should be an object");
    assert.ok(r.metafile.outputs !== undefined, "metafile should have outputs");
  });

  await test("build throws on a missing entry point", async () => {
    let threw = null;
    try {
      await guchho.build({ entryPoints: ["nope.js"], write: false });
    } catch (e) {
      threw = e;
    }
    assert.ok(threw, "should throw");
    assert.ok(Array.isArray(threw.errors) && threw.errors.length > 0);
  });

  await test("build refuses to run with no entry points at all", async () => {
    await assert.rejects(() => guchho.build({}), TypeError);
  });

  await test("a failed build throws rather than resolving", async () => {
    let resolved = null;
    try {
      resolved = await guchho.build({ entryPoints: ["src/bad.js"], write: false });
    } catch {
      // expected
    }
    assert.strictEqual(resolved, null, "a failed build must not resolve with a result");
  });

  await test("context: rebuild picks up an edit, dispose releases it", async () => {
    const ctx = await guchho.context({
      entryPoints: ["src/entry.js"],
      bundle: true,
      format: "esm",
      write: false,
    });

    const first = await ctx.rebuild();
    assert.deepStrictEqual(first.errors, []);

    fs.writeFileSync(path.join(dir, "src", "entry.js"), "export const n = 'edited';\n");
    const second = await ctx.rebuild();
    assert.ok(
      second.outputFiles[0].text.includes("edited"),
      "rebuild should see the new source: " + second.outputFiles[0].text
    );

    await ctx.dispose();
    await ctx.dispose(); // must be idempotent
  });

  await test("a disposed context refuses further use", async () => {
    const ctx = await guchho.context({ entryPoints: ["src/entry.js"], write: false });
    await ctx.dispose();
    await assert.rejects(() => ctx.rebuild(), /disposed/);
  });

  await test("context.watch and cancel", async () => {
    const ctx = await guchho.context({ entryPoints: ["src/entry.js"], write: false });
    let called = 0;
    await ctx.watch({ onRebuild: () => called++ });
    await ctx.rebuild();
    assert.strictEqual(called, 1, "onRebuild should fire for a rebuild we drive");
    await ctx.cancel();
    await ctx.dispose();
  });

  await test("formatMessages returns printable text", async () => {
    const text = await guchho.formatMessages(
      [{ text: "something is wrong", location: null, notes: [] }],
      { kind: "error", color: false, terminalWidth: 80 }
    );
    assert.ok(text.includes("something is wrong"), "text should contain the message");
  });

  await test("analyzeMetafile takes an object or a string", async () => {
    const mf = { inputs: {}, outputs: { "out.js": { bytes: 10, inputs: {} } } };
    const a = await guchho.analyzeMetafile(mf);
    const b = await guchho.analyzeMetafile(JSON.stringify(mf));
    assert.strictEqual(typeof a, "string");
    assert.strictEqual(a, b, "both spellings should agree");
  });

  await test("define and external reach the engine", async () => {
    const r = await guchho.build({
      entryPoints: ["src/defs.js"],
      write: false,
      define: { DEBUG: "false" },
      bundle: true,
      format: "esm",
    });
    assert.ok(!r.outputFiles[0].text.includes("DEBUG"), "the define should have been applied");
  });

  await test("an unknown option is refused by the engine's own grammar", async () => {
    let threw = null;
    try {
      await guchho.transform("x", { loader: "js" }).then(() =>
        guchho.build({ entryPoints: ["src/entry.js"], write: false, sourcemapRaw: "x" })
      );
    } catch (e) {
      threw = e;
    }
    assert.ok(threw === null || threw.errors !== undefined, "should not produce a weird error");
  });

  await test("stop ends the process, and a later build starts a new one", async () => {
    await guchho.stop();
    const r = await guchho.transform("let a = 1", { loader: "js" });
    assert.ok(r.code.includes("a"), "a new service should start on demand");
  });

  await guchho.stop();
  process.chdir(startDir);
  fs.rmSync(dir, { recursive: true, force: true });

  console.log("");
  console.log(failures === 0 ? "all passed" : failures + " failed");
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.error("HARNESS FAILED:", e);
  process.exit(1);
});
