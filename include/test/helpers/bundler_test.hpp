#pragma once

#include <atomic>
#include <map>
#include <mutex>

#include <string>
#include <string_view>
#include <utility>
#include <vector>


#include "guchho/config.hpp"

#include "guchho/logger.hpp"
#include "test/helpers/filesystem_test.hpp"

namespace bundler::test {

// Bundled
// -------
// The complete description of one Guchho test scenario.  `files` populates
// the in-memory mock file system (absolute path -> file contents), while
// `entry_paths` and `entry_paths_advanced` name the modules Guchho must
// resolve and bundle.  `options` carries the build configuration Guchho
// runs with, and `expected_scan_log` / `expected_compile_log` hold the
// scan-phase and compile-phase diagnostics the scenario must reproduce.
// A Bundled instance is handed to a Suite's ExpectBundled entry points,
// which turn it into a full bundle run and a snapshot comparison.
struct Bundled {
    std::unordered_map<std::string, std::string> files = {};
    std::vector<std::string>                    entry_paths = {};
    std::vector<guchho::config::EntryPoint>     entry_paths_advanced = {};
    std::string                                 abs_working_dir = {};
    guchho::config::Options                     options = {};
    // The text of a "guchho.json" project config that configures this run.
    // When non-empty the config is mapped onto `options` exactly as
    // "resolver::LoadGuchhoConfigFromText" does, so every field the file
    // specifies wins while the knobs the schema cannot express (BuildMode,
    // the harness defaults) are kept. Relative paths inside the config are
    // resolved against the directory of `guchho_config_path`, and config
    // diagnostics are compared against `expected_scan_log`. When empty, no
    // config is loaded and `options` is used verbatim.
    std::string                                 guchho_config = {};
    // The absolute path `guchho_config` is understood to come from. It only
    // supplies the base directory for relative config paths and the location
    // shown in diagnostics; the file itself is never read. Defaults to
    // "<abs_working_dir>/guchho.json".
    std::string                                 guchho_config_path = {};
    std::string                                 expected_scan_log = {};
    std::string                                 expected_compile_log = {};
    bool                                        debug_logs = false;
    // When set, the scan and compile logs are rendered in the source-frame
    // form: each diagnostic is preceded by a "path:line:column" location
    // header and followed by the highlighted offending line of source.
    // When clear (the default), a compact summary form is used in which
    // every message fits on a single line with no location header and no
    // source excerpt.
    bool                                        source_logs = false;
};

// ---------------------------------------------------------------------------
// Suite -- snapshot harness for one bundle test category
// ---------------------------------------------------------------------------
// A Suite groups every scenario belonging to a single bundle test category.
// It owns one on-disk snapshot file named after the suite, records the
// generated output for each scenario that runs, and then either compares
// those results against the stored snapshot or regenerates the file when
// snapshot updating was requested.  Every live instance registers itself in
// `global_suites_` so a single global cleanup step can finalise all suites
// before the process exits.
// ---------------------------------------------------------------------------

class Suite {
public:
    explicit Suite(std::string name) : name_(std::move(name)) {}

    // ExpectBundled
    // -------------
    // Runs the scenario twice: once against the Unix mock file system and
    // once against the Windows mock file system.  Before the Windows run,
    // every absolute path inside `args` is rewritten from the caller's
    // Unix-style notation into Windows notation (forward slashes become
    // backslashes, drive prefixes and root mounts are re-added), so the two
    // platforms can share a single Bundled spec.
    //
    // Input:  Bundled{files={"src/App.js": "export default 1\n"},
    //                 entry_paths={"src/App.js"},
    //                 options={BuildMode=kBundle}}
    // Output: Runs Guchho under both mock file systems and compares each
    //         result with the snapshot for the current test; aborts the
    //         test on any mismatch.
    void ExpectBundled(Bundled args);

    // ExpectBundledUnix
    // -----------------
    // Like ExpectBundled, but only performs the run and comparison against
    // the Unix mock file system.  Useful when a scenario is inherently tied
    // to forward-slash path conventions.
    //
    // Input:  Bundled{files={"/out/a.js": "export const x = 1;\n"},
    //                 entry_paths={"/out/a.js"}}
    // Output: Runs Guchho once on the Unix mock and verifies the generated
    //         output against the stored snapshot for the current test.
    void ExpectBundledUnix(Bundled args);

    // ExpectBundledWindows
    // --------------------
    // Like ExpectBundled, but only performs the run and comparison against
    // the Windows mock file system.  Absolute paths inside the Bundled spec
    // are expected in Windows notation (backslash separators, drive prefix).
    //
    // Input:  Bundled{files={"C:\\out\\a.js": "export const x = 1;\n"},
    //                 entry_paths={"C:\\out\\a.js"}}
    // Output: Runs Guchho once on the Windows mock and verifies the
    //         generated output against the stored snapshot for the current
    //         test.
    void ExpectBundledWindows(Bundled args);

    // RunGlobalCleanup
    // ----------------
    // The finish hook Guchho's test harness invokes once near the end of
    // the process lifetime.  For every registered suite that produced a
    // complete set of generated snapshots it either rewrites the snapshot
    // file (when regeneration was requested) or validates that every stored
    // snapshot was reproduced during this run.  A validation failure ends
    // the process with a non-zero exit code.
    static void RunGlobalCleanup();

    static std::atomic<bool> update_snapshots_;
    static std::vector<Suite*> global_suites_;

private:
    // ExpectBundledImpl
    // -----------------
    // The shared engine behind every public ExpectBundled entry point.
    // Prepares the build options for the requested mock kind, builds an
    // in-memory file system from `args.files`, runs Guchho's scan phase and
    // then its compile phase, and compares the collected log messages with
    // the scenario's expected logs.  When the logs match and no hard errors
    // were reported, the produced bundle is flattened into one output text
    // (each written file, then an optional metafile block) and passed to
    // CompareSnapshot for the snapshot check.
    //
    // Input:  Bundled{files={"src/A.js": "..."}}, MockKind::kUnix
    // Output: Throws on log or snapshot mismatch; otherwise returns after
    //         recording the generated output under the current test name.
    void ExpectBundledImpl(Bundled args,
                           guchho::test::MockKind fs_kind);

    // CompareSnapshot
    // ---------------
    // Records the generated output for `test_name` and either compares it
    // with the expected snapshot for that name or, when snapshot updating
    // is active, retains it for a later file rewrite.  The on-disk snapshot
    // file is parsed lazily on the first call.  A text mismatch is treated
    // as a test failure and aborts the current test.
    //
    // Input:  test_name = "BundlerCSS.multipleEntryPoints",
    //         generated = "---------- out/app.js ----------\nexport const x = 1;\n"
    // Output: Fails the test when the snapshot text differs; otherwise
    //         stores the name/output pairing and returns.
    void CompareSnapshot(const std::string& test_name,
                         const std::string& generated);

    // UpdateSnapshots
    // ---------------
    // Rewrites the suite's snapshot file using every generated snapshot
    // collected so far.  Entries are written in sorted test-name order and
    // separated by a fixed splitter token.  Unless every expected snapshot
    // was reproduced this call is a no-op, so a partial test run can never
    // clobber the file with an incomplete set.
    // Input:  generated_snapshots_ = {"A": "text A", "B": "text B"}
    // Output: Writes "A\ntext A<splitter>B\ntext B" to the snapshot file.
    void UpdateSnapshots();

    // ValidateSnapshots
    // -----------------
    // Cross-checks the expected snapshots parsed from the snapshot file
    // against the snapshots generated during the run.  Returns false when
    // any expected name was never generated, printing each missing entry to
    // stdout.
    //
    // Input:  expected = {"A", "B"}, generated = {"A"}
    // Output: false (entry "B" has no matching generated snapshot).
    bool ValidateSnapshots();

    // SnapshotsComplete
    // -----------------
    // Returns true when every expected snapshot name also has a generated
    // counterpart, indicating the current process exercised the whole suite
    // rather than a single filtered test.
    bool SnapshotsComplete() const;

    std::string name_;

    // Path of this suite's snapshot file.  Lazily resolved from the suite
    // name on the first CompareSnapshot call; empty until then.
    std::string path_;
    std::map<std::string, std::string> expected_snapshots_;
    std::map<std::string, std::string> generated_snapshots_;
    std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// Path conversion helpers
// ---------------------------------------------------------------------------

// Win2Unix converts a Windows-style absolute path into the Unix-style path
// Guchho uses internally when presenting snapshot output.  A leading "C:"
// drive prefix is stripped and every backslash becomes a forward slash.
// Paths that are not Windows absolute paths pass through unchanged.
//
// Input:  Win2Unix("C:\\Project\\src\\app.js")
// Output: "/Project/src/app.js"
std::string Win2Unix(std::string_view p);

// Unix2Win converts a Unix-style path into Windows notation: every forward
// slash becomes a backslash and an absolute path gains a "C:" drive prefix.
// Relative paths are only re-separated, not re-rooted.
//
// Input:  Unix2Win("/Project/src/app.js")
// Output: "C:\\Project\\src\\app.js"
std::string Unix2Win(std::string_view p);

// ---------------------------------------------------------------------------
// Log formatting
// ---------------------------------------------------------------------------

// FormatLog renders a sequence of Guchho logger messages into the plain
// text callers compare against expected logs.  Every message is printed
// through the standard logger renderer with an assumed terminal width and
// no ANSI color.  When `include_source` is set, messages that carry a
// source location additionally emit their path:line:column header and the
// highlighted source line.
//
// Input:  msgs    = [Msg(kind=kWarning, data.text="Duplicate key")],
//         include_source = false
// Output: "warning: Duplicate key\n"
std::string FormatLog(const std::vector<guchho::logger::Msg>& msgs,
                      bool include_source = false);

// The concrete suite instances live in the category test files: css_suite
// is defined in bundler_css_test.cpp and drives every CSS bundle scenario.
// The remaining extern declarations exist so a suite can be referenced from
// helper code before its defining test file is linked.
extern Suite css_suite;
extern Suite default_suite;

} // namespace bundler::test
