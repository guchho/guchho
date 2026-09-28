// Tests for symlink handling during resolution, which is what the
// PreserveSymlinks option exists to turn off.
//
// The shared mock file system cannot express a symlink: its EvalSymlinks only
// cleans the path, and it throws from Kind(). So this file carries its own file
// system, which is the same trade the other suites make when they need a
// behaviour the shared mock cannot produce. Nothing here touches the real disk,
// so a test that needs a symlink on Windows needs nothing Windows does not
// already give it.

#include "test/guchho_test.hpp"

#include "guchho/bundler.hpp"
#include "guchho/cache.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bundler    = guchho::bundler;
namespace cache      = guchho::cache;
namespace compiler   = guchho::compiler;
namespace config     = guchho::config;
namespace filesystem = guchho::filesystem;
namespace logger     = guchho::logger;
namespace resolver   = guchho::resolver;

namespace {

// The extension order guchho itself uses when nothing overrides it.
const std::vector<std::string> kDefaultExtensions = {
    ".tsx", ".ts", ".jsx", ".js", ".css", ".json"
};

// A file system that serves files from a map and symlinks from another one. A
// symlink is declared as the path it sits at plus the path it points at, and
// the directory listing reports the link so the resolver can see it, while
// EvalSymlinks follows the chain.
class SymlinkFs : public filesystem::Fs {
public:
    // "files" is the real content, "links" the links. A path may be both a
    // prefix of real files and a link, which is how a linked package directory
    // is expressed.
    SymlinkFs(std::unordered_map<std::string, std::string> files,
              std::unordered_map<std::string, std::string> links)
        : files_(std::move(files)), links_(std::move(links)) {}

    // Every directory that has something in it, computed once from both maps
    // so ReadDirectory never has to consult the disk.
    void BuildDirs()
    {
        dirs_.insert({"/", std::map<std::string, std::shared_ptr<filesystem::Entry>>()});
        for (const auto& [path, contents] : files_) {
            (void)contents;
            AddFileToDirs(path);
        }
        for (const auto& [path, target] : links_) {
            (void)target;
            std::string dir = DirOf(path);
            // A link's own directory may be a chain that no real file occupies
            // — a linked package puts "node_modules" in a project that has
            // none — so the chain above it has to be registered as well, or the
            // walk that climbs to find it never sees the level.
            AddDirToDirs(dir);
            auto entry      = std::make_shared<filesystem::Entry>();
            entry->base     = BaseOf(path);
            entry->dir      = dir;
            // The target is recorded already followed to the end, which is what
            // a real file system reports: the resolver reads this field as the
            // answer rather than as another link to chase, so a chain left
            // half-resolved here would stop the walk one hop short.
            std::optional<std::string> followed = EvalSymlinks(path);
            entry->symlink  = followed ? *followed : std::string();
            // A link is reported as whatever it points at, which is how the
            // real file system classifies one: a linked package is a
            // directory, a linked file is a file. The chain is followed to the
            // end first, because a link to a link is still classified by what
            // it finally reaches — otherwise the intermediate link would be
            // reported as existing nowhere at all.
            entry->kind      = LinkKindOf(links_[path]);
            entry->need_stat = false;
            AddEntry(dir, entry);
        }
    }

    filesystem::FsResult<filesystem::DirEntries> ReadDirectory(
            const std::string& path_in) override
    {
        // Reading through a link lands on the target, which is what a real file
        // system does: the contents of a linked package are the contents of the
        // package it points at.
        std::optional<std::string> followed = EvalSymlinks(path_in);
        const std::string&         path     = followed ? *followed : path_in;
        auto found = dirs_.find(path);
        if (found == dirs_.end()) {
            filesystem::FsResult<filesystem::DirEntries> result;
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error  = "no such file or directory";
            return result;
        }
        filesystem::FsResult<filesystem::DirEntries> result;
        filesystem::DirEntries                       entries;
        entries.dir  = path_in;
        entries.data = found->second;
        result.value = std::move(entries);
        return result;
    }

    filesystem::FsResult<std::string> ReadFile(const std::string& path_in) override
    {
        std::optional<std::string> followed = EvalSymlinks(path_in);
        const std::string&         path     = followed ? *followed : path_in;
        auto found = files_.find(path);
        if (found == files_.end()) {
            filesystem::FsResult<std::string> result;
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error  = "no such file or directory";
            return result;
        }
        filesystem::FsResult<std::string> result;
        result.value = found->second;
        return result;
    }

    filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> OpenFile(
            const std::string& path_in) override
    {
        std::optional<std::string> followed = EvalSymlinks(path_in);
        const std::string&         path     = followed ? *followed : path_in;
        auto found = files_.find(path);
        if (found == files_.end()) {
            filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> result;
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error  = "no such file or directory";
            return result;
        }
        auto opened              = std::make_shared<filesystem::InMemoryOpenedFile>();
        opened->contents         = found->second;
        filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> result;
        result.value = std::move(opened);
        return result;
    }

    filesystem::ModKeyResult ModKey(const std::string&) override
    {
        filesystem::ModKeyResult result;
        result.canonical_error = std::errc::invalid_argument;
        result.original_error  = "This is not available during tests";
        return result;
    }

    // Follows the chain of links starting at "path_in" until it reaches
    // something that is not a link. A chain that returns to a path already
    // visited stops there rather than looping, so a cycle degrades to "the last
    // path in the cycle" instead of hanging the test.
    std::optional<std::string> EvalSymlinks(std::string_view path_in) override
    {
        std::string path(path_in);
        std::unordered_set<std::string> seen;
        while (links_.find(path) != links_.end() && seen.insert(path).second) {
            path = links_[path];
        }
        return path;
    }

    bool IsAbs(std::string_view p) override
    {
        return !p.empty() && p.front() == '/';
    }

    std::optional<std::string> Abs(std::string_view p) override
    {
        std::string path(p);
        if (IsAbs(path)) {
            return path;
        }
        return "/" + path;
    }

    std::string Dir(std::string_view p) override
    {
        return DirOf(std::string(p));
    }

    std::string Base(std::string_view p) override
    {
        return BaseOf(std::string(p));
    }

    std::string Ext(std::string_view p) override
    {
        std::string base = BaseOf(std::string(p));
        size_t      pos  = base.rfind('.');
        return pos == std::string::npos ? std::string() : base.substr(pos);
    }

    std::string Join(std::initializer_list<std::string_view> parts) override
    {
        std::string out;
        for (std::string_view part : parts) {
            if (part.empty()) {
                continue;
            }
            if (part.front() == '/') {
                out = std::string(part);
                continue;
            }
            if (out.empty() || out.back() == '/') {
                out += std::string(part);
            } else {
                out += "/" + std::string(part);
            }
        }
        return Normalize(out);
    }

    std::string Cwd() override
    {
        return "/app";
    }

    std::optional<std::string> Rel(std::string_view, std::string_view) override
    {
        return std::nullopt;
    }

    std::pair<std::string, filesystem::EntryKind> Kind(
            std::string_view dir, std::string_view base) override
    {
        std::string path = Join({std::string(dir), std::string(base)});
        auto        link = links_.find(path);
        if (link != links_.end()) {
            // The first half of the pair is the link target, empty when the
            // entry is not a link, which is the contract every Fs implements.
            // Both halves follow the chain, for the reason BuildDirs gives.
            std::optional<std::string> followed = EvalSymlinks(path);
            return {followed ? *followed : link->second, LinkKindOf(link->second)};
        }
        return {{}, KindOf(path)};
    }

    filesystem::WatchData GetWatchData() override
    {
        return {};
    }

private:
    // Collapses the "." and ".." segments the resolver builds up as it walks,
    // the way a real file system does. It has to: a relative specifier like
    // "./linked.js" is joined onto its directory, and without this the join
    // yields "/app/src/.", which nothing in this map-backed file system has
    // ever heard of and so every lookup misses.
    //
    //   Normalize("/app/src/.")      =>  "/app/src"
    //   Normalize("/app/src/../lib") =>  "/app/lib"
    //   Normalize("/app//src/./x")  =>  "/app/src/x"
    static std::string Normalize(const std::string& path)
    {
        const bool absolute = !path.empty() && path.front() == '/';
        std::vector<std::string> kept;
        std::string segment;
        for (size_t i = 0; i <= path.size(); i++) {
            if (i == path.size() || path[i] == '/') {
                if (segment == "..") {
                    if (!kept.empty()) {
                        kept.pop_back();
                    } else if (!absolute) {
                        kept.push_back(segment);
                    }
                } else if (!segment.empty() && segment != ".") {
                    kept.push_back(segment);
                }
                segment.clear();
            } else {
                segment.push_back(path[i]);
            }
        }

        std::string result = absolute ? "/" : "";
        for (size_t i = 0; i < kept.size(); i++) {
            if (i > 0) {
                result += "/";
            }
            result += kept[i];
        }
        // A path made entirely of segments that normalize away is the current
        // directory, which is what the real file system calls it.
        return result.empty() ? "." : result;
    }

    // Classifies a path from what the maps hold: a path with contents is a
    // file, a path that only holds other paths is a directory, anything else
    // does not exist.
    filesystem::EntryKind KindOf(const std::string& path) const
    {
        if (files_.find(path) != files_.end()) {
            return filesystem::EntryKind::kFile;
        }
        if (dirs_.find(path) != dirs_.end()) {
            return filesystem::EntryKind::kDir;
        }
        return filesystem::EntryKind::kInvalid;
    }

    // Classifies a link by the thing it finally points at, following the chain
    // to the end. Non-const only because EvalSymlinks walks the link map, which
    // the override requires; nothing here mutates anything.
    filesystem::EntryKind LinkKindOf(const std::string& target)
    {
        return KindOf(EvalSymlinks(target).value_or(target));
    }

    static std::string DirOf(const std::string& path)
    {
        size_t pos = path.rfind('/');
        if (pos == std::string::npos || pos == 0) {
            return "/";
        }
        return path.substr(0, pos);
    }

    static std::string BaseOf(const std::string& path)
    {
        size_t pos = path.rfind('/');
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    void AddEntry(const std::string& dir,
                  const std::shared_ptr<filesystem::Entry>& entry)
    {
        dirs_[dir][entry->base] = entry;
    }

    // Registers "dir" and every directory above it, adding each one to the
    // listing of its parent as a directory. This is what makes a walk that
    // climbs the tree find every level it should: the entries it reads out of
    // a listing are how it knows a child is a directory it can descend into.
    void AddDirToDirs(const std::string& dir)
    {
        std::string current = dir;
        while (true) {
            dirs_.insert({current, {}});
            if (current == "/") {
                break;
            }
            std::string parent = DirOf(current);
            auto entry         = std::make_shared<filesystem::Entry>();
            entry->base        = BaseOf(current);
            entry->dir         = parent;
            entry->kind        = filesystem::EntryKind::kDir;
            AddEntry(parent, entry);
            current = parent;
        }
    }

    // Registers "path" as a file, and the chain of directories holding it.
    void AddFileToDirs(const std::string& path)
    {
        std::string dir = DirOf(path);
        AddDirToDirs(dir);
        auto entry  = std::make_shared<filesystem::Entry>();
        entry->base = BaseOf(path);
        entry->dir  = dir;
        entry->kind = filesystem::EntryKind::kFile;
        AddEntry(dir, entry);
    }

    std::unordered_map<std::string, std::string> files_;
    std::unordered_map<std::string, std::string> links_;
    std::map<std::string, std::map<std::string, std::shared_ptr<filesystem::Entry>>> dirs_;
};

// A resolver transaction over a file system the test supplies, configured
// through the same factory production code uses.
struct NpmEnv {
    config::Options                     options;
    std::unique_ptr<filesystem::Fs>      fs;
    logger::Log                         log;
    std::unique_ptr<cache::CacheSet>    caches;
    std::unique_ptr<resolver::Resolver> res;
};

NpmEnv MakeEnv(std::unique_ptr<filesystem::Fs> fs, config::Options options = {})
{
    if (options.ExtensionOrder.empty()) {
        options.ExtensionOrder = kDefaultExtensions;
    }
    bundler::ApplyOptionDefaults(options);

    NpmEnv env;
    env.options = options;
    env.fs      = std::move(fs);
    env.log    = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
    env.caches = cache::MakeCacheSet();
    env.res    = resolver::NewResolver(config::APICall::kBuildCall,
                                       *env.fs, env.log, *env.caches, &env.options);
    return env;
}

std::string ResolveTo(NpmEnv& env,
                      const std::string& source_dir,
                      const std::string& specifier)
{
    resolver::DebugMeta debug_meta;
    auto result = env.res->Resolve(
            source_dir, specifier, compiler::ImportKind::kStmt, &debug_meta);
    if (!result.has_value()) {
        return std::string();
    }
    return result->path_pair.primary.text;
}

} // namespace

// ---------------------------------------------------------------------------
// Resolution through a link
// ---------------------------------------------------------------------------

TEST(NpmSymlink, DefaultResolvesThroughSymlink)
{
    // By default a linked file resolves to the file it points at, so two names
    // for the same source collapse to one entry in the output.
    auto fs = std::make_unique<SymlinkFs>(
            std::unordered_map<std::string, std::string>{
                    {"/app/real.js", "export const x = 1;\n"},
            },
            std::unordered_map<std::string, std::string>{
                    {"/app/src/linked.js", "/app/real.js"},
            });
    fs->BuildDirs();
    NpmEnv env = MakeEnv(std::move(fs));

    EXPECT_EQ(ResolveTo(env, "/app/src", "./linked.js"),
              std::string("/app/real.js"));
}

TEST(NpmSymlink, PreserveSymlinksKeepsLinkedPath)
{
    // With the option on, the path the import named is the path that is kept, so
    // the same source can legitimately be built twice under two names.
    config::Options options;
    options.PreserveSymlinks = true;

    auto fs = std::make_unique<SymlinkFs>(
            std::unordered_map<std::string, std::string>{
                    {"/app/real.js", "export const x = 1;\n"},
            },
            std::unordered_map<std::string, std::string>{
                    {"/app/src/linked.js", "/app/real.js"},
            });
    fs->BuildDirs();
    NpmEnv env = MakeEnv(std::move(fs), options);

    EXPECT_EQ(ResolveTo(env, "/app/src", "./linked.js"),
              std::string("/app/src/linked.js"));
}

TEST(NpmSymlink, SymlinkChainResolvesToRealTarget)
{
    // A link to a link is followed all the way down, which is what a workspace
    // that symlinks one checkout into another produces.
    auto fs = std::make_unique<SymlinkFs>(
            std::unordered_map<std::string, std::string>{
                    {"/app/real.js", "export const x = 1;\n"},
            },
            std::unordered_map<std::string, std::string>{
                    {"/app/src/second.js", "/app/src/first.js"},
                    {"/app/src/first.js",  "/app/real.js"},
            });
    fs->BuildDirs();
    NpmEnv env = MakeEnv(std::move(fs));

    EXPECT_EQ(ResolveTo(env, "/app/src", "./second.js"),
              std::string("/app/real.js"));
}

TEST(NpmSymlink, SymlinkedPackageRootUsesRealPackageJSON)
{
    // A package installed as a link is still a package: its exports come from
    // the package.json at the far end of the link, not from a missing one at
    // the link.
    auto fs = std::make_unique<SymlinkFs>(
            std::unordered_map<std::string, std::string>{
                    {"/store/pkg/package.json",
                        R"({"name":"pkg","exports":{".":"./index.js"}})"},
                    {"/store/pkg/index.js", "export const x = 1;\n"},
            },
            std::unordered_map<std::string, std::string>{
                    {"/app/node_modules/pkg", "/store/pkg"},
            });
    fs->BuildDirs();
    NpmEnv env = MakeEnv(std::move(fs));

    EXPECT_EQ(ResolveTo(env, "/app", "pkg"), std::string("/store/pkg/index.js"));
}

TEST(NpmSymlink, PreserveSymlinksIsRequiredForLinkedPackage)
{
    // The same install with the option on keeps the linked path, so the answer
    // names the node_modules entry the import actually asked for. The pair with
    // the test above is what makes the option meaningful: same tree, different
    // answer, decided by one flag.
    config::Options options;
    options.PreserveSymlinks = true;

    auto fs = std::make_unique<SymlinkFs>(
            std::unordered_map<std::string, std::string>{
                    {"/store/pkg/package.json",
                        R"({"name":"pkg","exports":{".":"./index.js"}})"},
                    {"/store/pkg/index.js", "export const x = 1;\n"},
            },
            std::unordered_map<std::string, std::string>{
                    {"/app/node_modules/pkg", "/store/pkg"},
            });
    fs->BuildDirs();
    NpmEnv env = MakeEnv(std::move(fs), options);

    EXPECT_EQ(ResolveTo(env, "/app", "pkg"),
              std::string("/app/node_modules/pkg/index.js"));
}
