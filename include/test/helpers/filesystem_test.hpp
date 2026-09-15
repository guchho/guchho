#pragma once

#include "guchho/helpers.hpp"
#include "guchho/filesystem.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace guchho::test {

    using guchho::filesystem::DirEntries;
    using guchho::filesystem::Entry;
    using guchho::filesystem::EntryKind;
    using guchho::filesystem::Fs;
    using guchho::filesystem::FsResult;
    using guchho::filesystem::InMemoryOpenedFile;
    using guchho::filesystem::MockKind;
    using guchho::filesystem::ModKeyResult;
    using guchho::filesystem::OpenedFile;
    using guchho::filesystem::WatchData;

    namespace {

        // Replaces every occurrence of "from" with "to" in "text" and returns
        // the result.  The original string is passed by value so the caller
        // retains the original.
        //
        // Example:
        //   ReplaceAll("a/b/c", '/', '\\')  →  "a\\b\\c"
        std::string ReplaceAll(std::string text, char from, char to)
        {
            std::replace(text.begin(), text.end(), from, to);
            return text;
        }

        // Converts a Windows-style path to a Unix-style path by replacing
        // backslashes with forward slashes.  If the path starts with a drive
        // letter (e.g. "C:\foo"), the volume character is extracted and
        // returned separately so the caller can re-attach it later.
        //
        // Example:
        //   Win2Unix("C:\\src\\main.cpp")  →  ("/src/main.cpp", "C")
        //   Win2Unix("/usr/bin")           →  ("/usr/bin", "")
        std::pair<std::string, std::string> Win2Unix(std::string_view p)
        {
            std::string result(p);
            std::string volume;
            if (result.size() >= 3 && result[1] == ':' && result[2] == '\\') {
                char c = result[0];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                    volume = result.substr(0, 1);
                    result = result.substr(2);
                }
            }
            result = ReplaceAll(result, '\\', '/');
            return {result, volume};
        }

        // Converts a Unix-style path back to a Windows-style path.  If the
        // path is rooted (starts with '/') the volume prefix is prepended.
        // When "volume" is empty, "default_volume" is used instead.
        //
        // Example:
        //   Unix2Win("/src/main.cpp", "C", "D")  →  "C:\\src\\main.cpp"
        //   Unix2Win("foo/bar", "", "D")         →  "foo\\bar"
        std::string Unix2Win(std::string_view p, const std::string& volume, const std::string& default_volume)
        {
            std::string result = ReplaceAll(std::string(p), '/', '\\');
            if (!result.empty() && result.front() == '\\') {
                std::string effective_volume = !volume.empty() ? volume : default_volume;
                result                       = effective_volume + ":" + result;
            }
            return result;
        }

        // Lexically cleans a forward-slash-separated path by collapsing
        // redundant separators, removing "." elements, and resolving ".."
        // where possible.  An empty input produces ".".  A rooted path
        // keeps its leading slash.  ".." elements that cannot be resolved
        // (e.g. at the root) are preserved.
        //
        // Example:
        //   CleanSlashPath("/a/b/../c/./d")  →  "/a/c/d"
        //   CleanSlashPath("a//b")           →  "a/b"
        //   CleanSlashPath("../../a")        →  "../../a"
        //   CleanSlashPath("")               →  "."
        std::string CleanSlashPath(const std::string& path)
        {
            if (path.empty()) {
                return ".";
            }
            bool   rooted = path.front() == '/';
            size_t n      = path.size();

            std::string out;
            out.reserve(n);
            size_t r = 0, dotdot = 0;
            if (rooted) {
                out += '/';
                r = dotdot = 1;
            }

            while (r < n) {
                if (path[r] == '/') {
                    r++;
                } else if (path[r] == '.' && (r + 1 == n || path[r + 1] == '/')) {
                    r++;
                } else if (path[r] == '.' && path[r + 1] == '.' && (r + 2 == n || path[r + 2] == '/')) {
                    r += 2;
                    if (out.size() > dotdot) {
                        out.pop_back();
                        while (out.size() > dotdot && out.back() != '/') {
                            out.pop_back();
                        }
                        if (out.size() > dotdot) {
                            out.pop_back();
                        }
                    } else if (!rooted) {
                        if (!out.empty()) {
                            out += '/';
                        }
                        out += "..";
                        dotdot = out.size();
                    }
                } else {
                    if ((rooted && out.size() != 1) || (!rooted && !out.empty())) {
                        out += '/';
                    }
                    for (; r < n && path[r] != '/'; r++) {
                        out += path[r];
                    }
                }
            }

            if (out.empty()) {
                out += '.';
            }
            return out;
        }

        // Returns the directory portion of a forward-slash-separated path
        // (everything up to and including the last '/').  If there is no
        // slash, returns ".".  Trailing slashes beyond the first are
        // stripped before cleaning.
        //
        // Example:
        //   PathDir("/src/main.cpp")  →  "/src"
        //   PathDir("a/b/c")         →  "a/b"
        //   PathDir("foo")           →  "."
        std::string PathDir(std::string_view path)
        {
            size_t last_slash = path.find_last_of('/');
            if (last_slash == std::string_view::npos) {
                return ".";
            }
            std::string dir(path.substr(0, last_slash + 1));
            while (dir.size() > 1 && dir.back() == '/') {
                dir.pop_back();
            }
            return dir.empty() ? "." : CleanSlashPath(dir);
        }

        // Returns the base name (final component) of a forward-slash-
        // separated path.  Trailing slashes are stripped before extraction.
        // An empty path produces "."; a path of only slashes produces "/".
        //
        // Example:
        //   PathBase("/src/main.cpp")  →  "main.cpp"
        //   PathBase("/a/b/")         →  "b"
        //   PathBase("")              →  "."
        std::string PathBase(std::string_view path)
        {
            if (path.empty()) {
                return ".";
            }
            while (!path.empty() && path.back() == '/') {
                path.remove_suffix(1);
            }
            size_t last_slash = path.find_last_of('/');
            if (last_slash != std::string_view::npos) {
                path = path.substr(last_slash + 1);
            }
            if (path.empty()) {
                return "/";
            }
            return std::string(path);
        }

        // Returns the file extension (including the leading dot) from the
        // final component of a forward-slash-separated path.  The search
        // for a dot starts from the end and stops at the first '/'.  If no
        // dot is found, an empty string is returned.
        //
        // Example:
        //   PathExt("/src/main.cpp")  →  ".cpp"
        //   PathExt("/a.b/c")         →  ""
        //   PathExt("archive.tar.gz") →  ".gz"
        std::string PathExt(std::string_view path)
        {
            for (size_t i = path.size(); i > 0;) {
                i--;
                if (path[i] == '/') {
                    break;
                }
                if (path[i] == '.') {
                    return std::string(path.substr(i));
                }
            }
            return "";
        }

        // Splits "path" on the first '/' into (head, tail).  If there is no
        // '/' the entire string is the head and tail is empty.
        //
        // Example:
        //   SplitOnSlashFirst("a/b/c")  →  ("a", "b/c")
        //   SplitOnSlashFirst("foo")    →  ("foo", "")
        std::pair<std::string, std::string> SplitOnSlashFirst(const std::string& path)
        {
            size_t slash = path.find('/');
            if (slash != std::string::npos) {
                return {path.substr(0, slash), path.substr(slash + 1)};
            }
            return {path, ""};
        }

        // Joins path elements with '/' separators.  No cleaning is
        // performed; the result may contain "//" or trailing slashes.
        //
        // Example:
        //   JoinSlash({"a", "b", "c"})  →  "a/b/c"
        //   JoinSlash({"x"})            →  "x"
        std::string JoinSlash(const std::vector<std::string>& parts)
        {
            std::string joined;
            for (size_t i = 0; i < parts.size(); i++) {
                if (i > 0) {
                    joined += '/';
                }
                joined += parts[i];
            }
            return joined;
        }

    } // namespace

    namespace {

        // In-memory mock implementation of the Fs interface for tests.  All
        // data is served from the "input" map provided at construction time;
        // no real file-system calls are made.  Directory listings are built
        // automatically by walking each file path from leaf to root.
        //
        // "kind" selects whether the mock enforces Unix or Windows path
        // conventions (separator character, drive letters, etc.).
        //
        // "abs_working_dir" is the synthetic working directory returned by
        // Cwd() and used as the base for Abs() on relative paths.
        class MockFS : public Fs {
        public:
            MockFS(const std::unordered_map<std::string, std::string>& input,
                   MockKind                                            kind,
                   const std::string&                                  abs_working_dir)
                : kind_(kind),
                  abs_working_dir_(abs_working_dir)
            {
                if (kind_ == MockKind::kWindows) {
                    default_volume_ = Win2Unix(abs_working_dir).second;
                }

                for (const auto& [input_path, contents] : input) {
                    std::string k       = input_path;
                    std::string volume;
                    files_[k] = contents;
                    if (kind_ == MockKind::kWindows) {
                        auto converted = Win2Unix(k);
                        k              = converted.first;
                        volume         = converted.second;
                    }
                    std::string original = k;

                    while (true) {
                        std::string k_dir = PathDir(k);
                        std::string key   = k_dir;
                        if (kind_ == MockKind::kWindows) {
                            key = Unix2Win(key, volume, default_volume_);
                        }
                        auto found = dirs_.find(key);
                        if (found == dirs_.end()) {
                            DirEntries dir_entries;
                            dir_entries.dir  = key;
                            dir_entries.data = std::map<std::string, std::shared_ptr<Entry>>{};
                            found            = dirs_.emplace(key, std::move(dir_entries)).first;
                        }
                        DirEntries& dir = found->second;
                        if (k_dir == k) {
                            break;
                        }
                        std::string base = PathBase(k);

                        auto entry           = std::make_shared<Entry>();
                        entry->base          = base;
                        entry->kind          = k == original ? EntryKind::kFile : EntryKind::kDir;
                        dir.data->emplace(helpers::ToLowerASCII(base), std::move(entry));

                        k = k_dir;
                    }
                }
            }

            // Returns the directory listing for "path_in".  On a Windows-
            // mode mock, forward slashes in the input are converted to
            // backslashes before lookup, and trailing slashes are stripped.
            //
            // Example:
            //   ReadDirectory("/src")  →  DirEntries with {"main.cpp", "util.cpp"}
            //   ReadDirectory("/missing")  →  FsResult with canonical_error
            FsResult<DirEntries> ReadDirectory(const std::string& path_in) override
            {
                std::string path = path_in;
                if (kind_ == MockKind::kWindows) {
                    path = ReplaceAll(path, '/', '\\');
                }

                char slash = kind_ == MockKind::kWindows ? '\\' : '/';

                // Trim trailing slashes before lookup
                size_t first_slash = path.find(slash);
                while (true) {
                    size_t i = path.find_last_of(slash);
                    if (i == std::string::npos || i != path.size() - 1 ||
                        (first_slash != std::string::npos && i <= first_slash)) {
                        break;
                    }
                    path.resize(i);
                }

                auto found = dirs_.find(path);
                if (found != dirs_.end()) {
                    FsResult<DirEntries> result;
                    result.value = found->second;
                    return result;
                }
                return ErrorResult<DirEntries>();
            }

            // Reads the full contents of the file at "path_in".  On a
            // Windows-mode mock, forward slashes are converted to backslashes.
            //
            // Example:
            //   ReadFile("/src/main.cpp")  →  "int main(){}"
            //   ReadFile("/missing.txt")   →  FsResult with canonical_error
            FsResult<std::string> ReadFile(const std::string& path_in) override
            {
                std::string path = path_in;
                if (kind_ == MockKind::kWindows) {
                    path = ReplaceAll(path, '/', '\\');
                }
                auto found = files_.find(path);
                if (found != files_.end()) {
                    FsResult<std::string> result;
                    result.value = found->second;
                    return result;
                }
                return ErrorResult<std::string>();
            }

            // Opens the file at "path_in" for random access, returning an
            // InMemoryOpenedFile whose contents are a copy of the stored data.
            //
            // Example:
            //   OpenFile("/src/main.cpp")  →  InMemoryOpenedFile with full contents
            FsResult<std::shared_ptr<OpenedFile>> OpenFile(const std::string& path_in) override
            {
                std::string path = path_in;
                if (kind_ == MockKind::kWindows) {
                    path = ReplaceAll(path, '/', '\\');
                }
                auto found = files_.find(path);
                if (found != files_.end()) {
                    auto opened             = std::make_shared<InMemoryOpenedFile>();
                    opened->contents        = found->second;
                    FsResult<std::shared_ptr<OpenedFile>> result;
                    result.value = std::move(opened);
                    return result;
                }
                return ErrorResult<std::shared_ptr<OpenedFile>>();
            }

            // Always returns an error — the mock does not support metadata-
            // based change detection.  Tests that need ModKey behaviour
            // should use a real or specialised mock.
            ModKeyResult ModKey(const std::string&) override
            {
                ModKeyResult result;
                result.canonical_error = std::errc::invalid_argument;
                result.original_error  = "This is not available during tests";
                return result;
            }

            // Returns true when the (possibly Windows-style) path is
            // absolute after conversion to Unix notation.
            //
            // Example (Windows mode):
            //   IsAbs("C:\\foo")  →  true
            //   IsAbs("foo")     →  false
            bool IsAbs(std::string_view p) override
            {
                std::string path(p);
                if (kind_ == MockKind::kWindows) {
                    path = Win2Unix(path).first;
                }
                return !path.empty() && path.front() == '/';
            }

            // Returns an absolute form of the path by prepending '/' and
            // cleaning the result.  On Windows mode the volume prefix is
            // re-attached.
            //
            // Example (Unix mode, cwd="/project"):
            //   Abs("src/main.cpp")  →  "/src/main.cpp"
            std::optional<std::string> Abs(std::string_view p) override
            {
                std::string path(p);
                std::string volume;
                if (kind_ == MockKind::kWindows) {
                    auto converted = Win2Unix(path);
                    path           = converted.first;
                    volume         = converted.second;
                }

                path = CleanSlashPath(JoinSlash({"/", path}));

                if (kind_ == MockKind::kWindows) {
                    path = Unix2Win(path, volume, default_volume_);
                }

                return path;
            }

            // Returns the directory portion of the path (everything up to
            // the last separator).
            //
            // Example:
            //   Dir("/src/main.cpp")  →  "/src"
            std::string Dir(std::string_view p) override
            {
                std::string path(p);
                std::string volume;
                if (kind_ == MockKind::kWindows) {
                    auto converted = Win2Unix(path);
                    path           = converted.first;
                    volume         = converted.second;
                }

                path = PathDir(path);

                if (kind_ == MockKind::kWindows) {
                    path = Unix2Win(path, volume, default_volume_);
                }

                return path;
            }

            // Returns the final component of the path (the file or directory
            // name).  On Windows mode, a root "/" is converted to the volume
            // root (e.g. "C:\\").
            //
            // Example:
            //   Base("/src/main.cpp")  →  "main.cpp"
            std::string Base(std::string_view p) override
            {
                std::string path(p);
                std::string volume;
                if (kind_ == MockKind::kWindows) {
                    auto converted = Win2Unix(path);
                    path           = converted.first;
                    volume         = converted.second;
                }

                path = PathBase(path);

                if (kind_ == MockKind::kWindows && path == "/") {
                    path = volume + ":\\";
                }

                return path;
            }

            // Returns the file extension including the leading dot, or an
            // empty string when the final component has no dot.
            //
            // Example:
            //   Ext("/src/main.cpp")  →  ".cpp"
            std::string Ext(std::string_view p) override
            {
                std::string path(p);
                if (kind_ == MockKind::kWindows) {
                    path = Win2Unix(path).first;
                }
                return PathExt(path);
            }

            // Joins path elements with the platform separator and cleans
            // the result.  On Windows mode the volume from the first
            // element is preserved.
            //
            // Example (Unix mode):
            //   Join({"/src", "main.cpp"})  →  "/src/main.cpp"
            std::string Join(std::initializer_list<std::string_view> parts) override
            {
                std::vector<std::string> converted;
                converted.reserve(parts.size());
                std::string volume;
                if (kind_ == MockKind::kWindows) {
                    bool first = true;
                    for (std::string_view part : parts) {
                        auto [path, part_volume] = Win2Unix(part);
                        converted.push_back(std::move(path));
                        if (first) {
                            volume = part_volume;
                            first  = false;
                        }
                    }
                } else {
                    for (std::string_view part : parts) {
                        converted.emplace_back(part);
                    }
                }

                std::string path = CleanSlashPath(JoinSlash(converted));

                if (kind_ == MockKind::kWindows) {
                    path = Unix2Win(path, volume, default_volume_);
                }

                return path;
            }

            // Returns the synthetic working directory provided at
            // construction.
            std::string Cwd() override
            {
                return abs_working_dir_;
            }

            // Computes the relative path from "base_v" to "target_v".
            // Returns nullopt when the two paths live on different volumes
            // (Windows mode) or when one is absolute and the other is not.
            //
            // Example (Unix mode):
            //   Rel("/a/b", "/a/b/c/d")  →  "c/d"
            //   Rel("/a/b", "/a/b")      →  "."
            //   Rel("/a/b", "/x/y")      →  "../../x/y"
            std::optional<std::string> Rel(std::string_view base_v, std::string_view target_v) override
            {
                std::string base   = std::string(base_v);
                std::string target = std::string(target_v);
                std::string volume;
                if (kind_ == MockKind::kWindows) {
                    auto converted_base   = Win2Unix(base);
                    auto converted_target = Win2Unix(target);
                    base                  = converted_base.first;
                    target                = converted_target.first;
                    volume                = !converted_base.second.empty() ? converted_base.second : default_volume_;
                    std::string v         = !converted_target.second.empty() ? converted_target.second : default_volume_;
                    if (!helpers::EqualFoldASCII(v, volume)) {
                        return std::nullopt;
                    }
                }

                base   = CleanSlashPath(base);
                target = CleanSlashPath(target);

                if (base == target) {
                    return ".";
                }
                if (base == ".") {
                    base.clear();
                }

                if ((!base.empty() && base.front() == '/') != (!target.empty() && target.front() == '/')) {
                    return std::nullopt;
                }

                // Find the common parent directory
                while (true) {
                    auto [b_head, b_tail] = SplitOnSlashFirst(base);
                    auto [t_head, t_tail] = SplitOnSlashFirst(target);
                    if (b_head != t_head) {
                        break;
                    }
                    base   = b_tail;
                    target = t_tail;
                }

                // Stop now if base is a subpath of target
                if (base.empty()) {
                    if (kind_ == MockKind::kWindows) {
                        target = Unix2Win(target, volume, default_volume_);
                    }
                    return target;
                }

                // Traverse up to the common parent
                size_t        up_count      = size_t(std::count(base.begin(), base.end(), '/')) + 1;
                std::string   common_parent;
                for (size_t i = 0; i < up_count; i++) {
                    common_parent += "../";
                }

                // Stop now if target is a subpath of base
                if (target.empty()) {
                    common_parent.pop_back();
                    if (kind_ == MockKind::kWindows) {
                        return Unix2Win(common_parent, volume, default_volume_);
                    }
                    return common_parent;
                }

                // Otherwise, down to the parent
                target = common_parent + target;
                if (kind_ == MockKind::kWindows) {
                    return Unix2Win(target, volume, default_volume_);
                }
                return target;
            }

            // Resolves symlinks by cleaning the path — the mock has no
            // real symlinks, so this is a no-op that returns the normalised
            // form of the input.
            //
            // Example:
            //   EvalSymlinks("/a/b/../c")  →  "/a/c"
            std::optional<std::string> EvalSymlinks(std::string_view path_in) override
            {
                std::string path(path_in);
                std::string volume;
                if (kind_ == MockKind::kWindows) {
                    auto converted = Win2Unix(path);
                    path           = converted.first;
                    volume         = converted.second;
                }
                path = CleanSlashPath(JoinSlash({"/", path}));
                if (kind_ == MockKind::kWindows) {
                    path = Unix2Win(path, volume, default_volume_);
                }
                return path;
            }

            // Not implemented — the mock does not classify entries by kind
            // after construction.  Calling this throws std::logic_error.
            std::pair<std::string, EntryKind> Kind(std::string_view, std::string_view) override
            {
                throw std::logic_error("Internal error");
            }

            // Not implemented — the mock does not track watch data.
            // Calling this throws std::logic_error.
            WatchData GetWatchData() override
            {
                throw std::logic_error("Internal error");
            }

        private:
            template <typename T>
            static FsResult<T> ErrorResult()
            {
                FsResult<T> result;
                result.canonical_error = std::errc::no_such_file_or_directory;
                result.original_error  = "no such file or directory";
                return result;
            }

            MockKind                                   kind_;
            std::string                                abs_working_dir_;
            std::string                                default_volume_;
            std::unordered_map<std::string, DirEntries> dirs_;
            std::unordered_map<std::string, std::string> files_;
        };

    } // namespace

    // Factory function that creates a MockFS.  "input" maps file paths to
    // their contents; directories are inferred automatically.  "kind"
    // selects Unix or Windows path conventions.  "abs_working_dir" is the
    // synthetic working directory.
    //
    // Example:
    //   auto fs = MakeMockFS({{"/src/main.cpp", "int main(){}"}},
    //                        MockKind::kUnix, "/src");
    //   fs->ReadFile("/src/main.cpp")  →  "int main(){}"
    //   fs->Cwd()                      →  "/src"
    inline std::unique_ptr<guchho::filesystem::Fs> MakeMockFS(const std::unordered_map<std::string, std::string>& input,
                                   MockKind                                            kind,
                                   const std::string&                                  abs_working_dir)
    {
        return std::make_unique<MockFS>(input, kind, abs_working_dir);
    }
}
