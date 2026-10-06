// The one configuration model, and the two spellings of it.
//
// The claim these tests hold is narrow and mechanical: a configuration written
// nested under "build" and the same configuration written flat must produce
// byte-identical flags and entry points, and neither may contribute a default
// the caller did not ask for.
//
// That second half matters as much as the first. The ranking of an explicit
// option against a config file against a built-in default lives in the engine —
// api::ResolveEffectiveBuildConfigs, reached through the flag grammar — and a
// default written in this layer would be a second copy of the same fact in a
// second language. The tests below are what make that a test failure rather than
// a slow drift.
//
// Nothing here needs a binary. options.js and flags.js are pure, so this file
// runs on a fresh checkout, which is most of the machines the suite runs on.

const assert = require("node:assert/strict");
const { describe, it } = require("node:test");

const { normalize } = require("../guchho/lib/options");
const { toFlags, toEntryPoints, SOURCEMAP_VALUES } = require("../guchho/lib/flags");

// The flag list and the entry points of a configuration, in the form a request
// would carry them. This is the whole of what normalization is allowed to
// change, so it is what is compared.
function requestShape(config) {
  const { build } = normalize(config);
  return {
    flags: toFlags(build).slice().sort(),
    entries: toEntryPoints(build),
  };
}

describe("configuration spelling", () => {
  // The contract, stated once as data so that each test below is a line rather
  // than a paragraph. Each pair is the same configuration twice.
  const EQUIVALENT = [
    {
      what: "one entry point",
      flat: { entrypoints: "src/index.js" },
      nested: { build: { entry: "src/index.js" } },
    },
    {
      what: "an array of entry points",
      flat: { entrypoints: ["src/a.js", "src/b.js"] },
      nested: { build: { entry: ["src/a.js", "src/b.js"] } },
    },
    {
      what: "entry points as { out, in } records",
      flat: { entrypoints: [{ out: "a.js", in: "src/a.js" }] },
      nested: { build: { entry: [{ out: "a.js", in: "src/a.js" }] } },
    },
    {
      what: "one entry point under the old camel-cased name",
      flat: { entryPoints: "src/index.js" },
      nested: { build: { entry: "src/index.js" } },
    },
    {
      what: "an array of entry points under the old camel-cased name",
      flat: { entryPoints: ["src/a.js", "src/b.js"] },
      nested: { build: { entry: ["src/a.js", "src/b.js"] } },
    },
    {
      what: "output options",
      flat: { entry: "src/i.js", outdir: "out", format: "esm", platform: "browser" },
      nested: {
        build: { entry: "src/i.js", outdir: "out", format: "esm", platform: "browser" },
      },
    },
    {
      what: "minify and friends",
      flat: { entry: "src/i.js", minify: true, treeShaking: true, splitting: false },
      nested: { build: { entry: "src/i.js", minify: true, treeShaking: true, splitting: false } },
    },
    {
      what: "source maps",
      flat: { entry: "src/i.js", sourcemap: "inline" },
      nested: { build: { entry: "src/i.js", sourcemap: "inline" } },
    },
    {
      what: "target",
      flat: { entry: "src/i.js", target: "es2022" },
      nested: { build: { entry: "src/i.js", target: "es2022" } },
    },
    {
      what: "external packages",
      flat: { entry: "src/i.js", external: ["react", "react-dom"] },
      nested: { build: { entry: "src/i.js", external: ["react", "react-dom"] } },
    },
    {
      what: "define and loader",
      flat: { entry: "src/i.js", define: { DEBUG: "false" }, loader: { ".ts": "ts" } },
      nested: {
        build: { entry: "src/i.js", define: { DEBUG: "false" }, loader: { ".ts": "ts" } },
      },
    },
  ];

  for (const { what, flat, nested } of EQUIVALENT) {
    it(`means the same thing flat and nested: ${what}`, () => {
      assert.deepEqual(requestShape(nested), requestShape(flat));
    });
  }

  it("leaves a flat configuration's spelling alone", () => {
    // "entrypoints" is canonical, so it is the one thing that must NOT move.
    // Every other key reaches the engine under the name it was written with.
    const { build } = normalize({ entrypoints: "src/i.js", outdir: "out" });
    assert.deepEqual(Object.keys(build).sort(), ["entrypoints", "outdir"]);
  });

  it("renames entry to entrypoints", () => {
    const { build } = normalize({ build: { entry: "src/i.js" } });
    assert.equal(build.entrypoints, "src/i.js");
    assert.equal("entry" in build, false);
  });

  it("renames entryPoints to entrypoints, one and several", () => {
    // The spelling the API published for years. A caller who has it in a working
    // script should not be broken by the rename, so it reaches the same place
    // the canonical name does rather than being quietly dropped.
    for (const given of ["src/i.js", ["src/a.js", "src/b.js"]]) {
      const { build } = normalize({ entryPoints: given });
      assert.deepEqual(build.entrypoints, given);
      assert.equal("entryPoints" in build, false);
    }
  });

  it("refuses entrypoints and entryPoints together", () => {
    // Canonical alongside its own alias is the most likely way to collide by
    // accident: a half-finished rename. Two names for one option is a caller
    // who does not know which they meant, and picking one for them is a guess
    // about where their output goes.
    assert.throws(
      () => normalize({ entrypoints: "a.js", entryPoints: "b.js" }),
      /same option/
    );
  });

  it("refuses entry and entryPoints together", () => {
    assert.throws(
      () => normalize({ build: { entry: "a.js", entryPoints: "b.js" } }),
      /same option/
    );
  });

  it("refuses all three spellings together", () => {
    assert.throws(
      () => normalize({ entrypoints: "a.js", entryPoints: "b.js", entry: "c.js" }),
      /same option/
    );
  });

  it("renames name to globalName, the spelling the wrapper formats read", () => {
    // "name" is what a command line and a Rollup config call the global a UMD
    // or IIFE wrapper publishes; "globalName" is what the rest of this schema
    // calls it. Reaching the grammar under either spelling is one option, and
    // dropping "name" instead would be a caller's working Rollup options
    // object silently building without its global.
    const { build } = normalize({ build: { name: "Lib" } });
    assert.equal(build.globalName, "Lib");
    assert.equal("name" in build, false);
  });

  it("refuses name and globalName together", () => {
    assert.throws(
      () => normalize({ build: { name: "a", globalName: "b" } }),
      /same option/
    );
  });
});

describe("configuration conflicts", () => {
  it("refuses an option set flat and nested at once", () => {
    assert.throws(
      () => normalize({ outdir: "a", build: { outdir: "b" } }),
      (error) => {
        assert.ok(error instanceof TypeError);
        assert.match(error.message, /outdir/);
        assert.match(error.message, /twice/);
        // Both spellings named, because which one was the mistake is exactly the
        // thing the caller has to work out.
        assert.match(error.message, /top level/);
        assert.match(error.message, /"build"/);
        return true;
      }
    );
  });

  it("allows different options at the two levels", () => {
    // Mixing spellings is fine when they do not collide; only a genuine
    // double-statement of one option is refused.
    const { build } = normalize({ minify: true, build: { entry: "src/i.js" } });
    assert.equal(build.minify, true);
    assert.equal(build.entrypoints, "src/i.js");
  });

  it("does not mutate the caller's object", () => {
    const original = { build: { entry: "src/i.js" } };
    normalize(original);
    assert.deepEqual(original, { build: { entry: "src/i.js" } });
  });

  it("refuses a configuration that is not an object", () => {
    for (const bad of [42, "src/i.js", true, []]) {
      assert.throws(() => normalize(bad), TypeError);
    }
  });
});

describe("sections that are not build options", () => {
  it("keeps server options out of the build flags", () => {
    // The whole point of peeling them off. "port" is not a build flag, and
    // sending it to the grammar would be a build that fails outright.
    const { build, server } = normalize({
      build: { entry: "src/i.html" },
      server: { port: 3000, host: "localhost" },
    });

    assert.deepEqual(toFlags(build).includes("--port=3000"), false);
    assert.deepEqual(server, { port: 3000, host: "localhost" });
  });

  it("refuses a key that is not a server option", () => {
    // A typo here is otherwise silent, and "prot" instead of "port" is a server
    // on a random port that the caller believes is 3000.
    assert.throws(
      () => normalize({ build: { entry: "a.html" }, server: { prot: 3000 } }),
      /not a "server" option/
    );
  });

  it("reads watch as a section", () => {
    const { watch } = normalize({ build: { entry: "a.js" }, watch: { delay: 120 } });
    assert.deepEqual(watch, { delay: 120 });
  });

  it("reads watch as true", () => {
    const { watch } = normalize({ build: { entry: "a.js" }, watch: true });
    assert.deepEqual(watch, {});
  });

  it("reads a delay from the flat form as well", () => {
    // The flat form has no sections, so the delay is next to the build options,
    // which is where it has always been read from.
    const { watch } = normalize({ entry: "a.js", delay: 80 });
    assert.deepEqual(watch, { delay: 80 });
  });

  it("accepts one plugin or a list of them", () => {
    const plugin = { name: "p", setup() {} };

    assert.deepEqual(normalize({ build: { entry: "a.js" }, plugins: [plugin] }).plugins, [
      plugin,
    ]);
    assert.deepEqual(normalize({ build: { entry: "a.js" }, plugins: plugin }).plugins, [
      plugin,
    ]);
  });

  it("refuses plugins that are neither an object nor a function", () => {
    assert.throws(
      () => normalize({ build: { entry: "a.js" }, plugins: "my-plugin" }),
      /must be a plugin object or an array/
    );
  });
});

describe("no defaults", () => {
  // The engine owns the defaults. Every one of these is a value guchho ships
  // with — outdir "dist", minify true, format "esm", platform "browser",
  // sourcemap none — and none of them may appear in a normalized configuration
  // the caller did not write. A default here would outrank a config file, because
  // this layer's output is what reaches the grammar as an explicit option.
  const NEVER_INVENTED = [
    "outdir",
    "outfile",
    "minify",
    "minifyWhitespace",
    "minifyIdentifiers",
    "minifySyntax",
    "sourcemap",
    "format",
    "platform",
    "target",
    "bundle",
    "treeShaking",
    "splitting",
    "jsx",
    "logLevel",
  ];

  it("invents nothing for an empty configuration", () => {
    const { build } = normalize({});
    assert.deepEqual(Object.keys(build), []);
    assert.deepEqual(toFlags(build), []);
  });

  it("invents nothing for an empty nested configuration", () => {
    const { build } = normalize({ build: {} });
    assert.deepEqual(Object.keys(build), []);
  });

  for (const option of NEVER_INVENTED) {
    it(`leaves ${option} unset when the caller did not set it`, () => {
      const { build } = normalize({ build: { entry: "src/i.js" } });
      assert.equal(
        Object.prototype.hasOwnProperty.call(build, option),
        false,
        `normalize() invented "${option}"`
      );
    });
  }

  it("never derives outfile from outdir", () => {
    // Stated separately because it is the one derivation that would be actively
    // wrong: outfile names one file, and an outdir names a tree. Filling one in
    // from the other turns "write my bundle here" into "overwrite this one file".
    const { build } = normalize({ build: { outdir: "dist" } });
    assert.equal(build.outfile, undefined);
  });
});

describe("sourcemap", () => {
  // Four values, a boolean, and the absence of a request. The four values used
  // to be dropped on the floor: the flag table read sourcemap as a boolean, and a
  // string is not true, so "inline" produced no flag and no complaint.

  it("sends a bare flag for true", () => {
    assert.deepEqual(toFlags({ sourcemap: true }), ["--sourcemap"]);
  });

  it("sends no flag for false", () => {
    assert.deepEqual(toFlags({ sourcemap: false }), []);
  });

  for (const value of ["inline", "external", "linked", "both"]) {
    it(`sends a valued flag for "${value}"`, () => {
      assert.deepEqual(toFlags({ sourcemap: value }), [`--sourcemap=${value}`]);
    });
  }

  it("sends no flag for none", () => {
    // "none" is the absence of a request written as a word, so that a config
    // file can name the thing it means.
    assert.deepEqual(toFlags({ sourcemap: "none" }), []);
  });

  it("sends no flag when unset", () => {
    assert.deepEqual(toFlags({}), []);
  });

  it("refuses a value the grammar has no name for", () => {
    assert.throws(() => toFlags({ sourcemap: "external-ish" }), /sourcemap must be one of/);
  });

  it("refuses a sourcemap that is neither a boolean nor a name", () => {
    assert.throws(() => toFlags({ sourcemap: 1 }), /sourcemap takes true, false, or one of/);
  });

  it("publishes the values it accepts", () => {
    // The list is a contract with the type declarations and the docs, so it is
    // checked against what the grammar actually takes rather than being restated
    // here — a second copy is a second thing to fall behind.
    assert.deepEqual([...SOURCEMAP_VALUES].sort(), ["both", "external", "inline", "linked", "none"]);
  });
});

describe("banner and footer", () => {
  // Text placed around the output, and the one option whose value is the bytes
  // themselves rather than a name for something. The record spelling these
  // types used to publish never worked: it was stringified into the flag and
  // arrived as "[object Object]", so a caller who wrote { js: "..." } got a
  // banner nobody wrote. The text is now the whole API, and it is sent through
  // unchanged — punctuation, newlines and all — because that is what the
  // engine prints above or below the code.

  const TEXT = "/*! MyLib v1.0.0 | MIT License */";

  for (const name of ["banner", "footer"]) {
    const flag = `--${name}=`;

    it(`sends ${name} as a valued flag`, () => {
      assert.deepEqual(toFlags({ [name]: TEXT }), [flag + TEXT]);
    });

    it(`sends no flag when ${name} is unset`, () => {
      assert.deepEqual(toFlags({}), []);
    });

    it(`keeps every character of ${name}, including "=" and a newline`, () => {
      // The grammar reads the value as the rest of the argument, so an "="
      // inside the text is part of it rather than a second separator. Losing
      // that would silently truncate the banner at the first "=".
      const text = "/*! v=1.0\nMIT License */";
      assert.deepEqual(toFlags({ [name]: text }), [flag + text]);
    });

    it(`refuses a ${name} that is not a string`, () => {
      for (const value of [{ js: TEXT }, true, 1, [TEXT]]) {
        assert.throws(
          () => toFlags({ [name]: value }),
          new RegExp(`${name} takes a string`),
          `${name} accepted ${JSON.stringify(value)}`
        );
      }
    });

    it(`sends ${name} from the nested spelling too`, () => {
      // The flat and nested configurations are one configuration, so the text
      // has to survive the walk through normalize() on its way to the flag.
      assert.deepEqual(requestShape({ build: { entry: "src/i.js", [name]: TEXT } }).flags, [
        flag + TEXT,
      ]);
    });
  }
});

describe("option names reach the grammar", () => {
  // A name this layer invents would be a flag the binary turns away with "Invalid
  // build flag" — which is a build that fails outright, loudly, at the first
  // call. These are here so that a typo is a failure in this suite instead.

  it("does not send a flag for a key that is not an option", () => {
    // "plugins" is a section, and lib/options.js peels it off. If it ever leaked
    // into the flat bag it would arrive as a build option with no flag, which is
    // harmless — but "port" arriving the same way is not.
    const { build } = normalize({ server: { port: 1 }, plugins: [] });
    assert.deepEqual(toFlags(build), []);
  });

  it("leaves outdir and outfile independent", () => {
    const { build } = normalize({ build: { outfile: "dist/bundle.js" } });
    assert.deepEqual(toFlags(build), ["--outfile=dist/bundle.js"]);
  });

  it("sends exports as a valued flag", () => {
    const { build } = normalize({ build: { exports: "default" } });
    assert.deepEqual(toFlags(build), ["--exports=default"]);
  });

  it("sends name as --global-name once the alias has been applied", () => {
    // The flag is spelled "--global-name" on the command line; "name" only
    // reaches it through the rename above, and this is where that stops being
    // a property of the option table and becomes a flag the binary accepts.
    const { build } = normalize({ build: { name: "Lib" } });
    assert.deepEqual(toFlags(build), ["--global-name=Lib"]);
  });
});