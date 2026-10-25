#include "guchho/lint.hpp"

#include "guchho/helpers.hpp"

#include <algorithm>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace guchho::lint::detail {

namespace {

std::string NormalizeSeparators(std::string text) {
    for (char& c : text) {
        if (c == '\\') c = '/';
    }
    while (text.starts_with("./")) text.erase(0, 2);
    return text;
}

bool IsRegexMeta(char c) {
    switch (c) {
    case '.': case '^': case '$': case '|': case '(': case ')':
    case '[': case ']': case '{': case '}': case '*': case '+':
    case '?': case '\\':
        return true;
    default:
        return false;
    }
}

std::string EscapeRegex(char c) {
    if (IsRegexMeta(c)) return std::string("\\") + c;
    return std::string(1, c);
}

bool ExpandBracesOnce(const std::string& pattern, std::vector<std::string>& out) {
    int depth = 0;
    size_t open = std::string::npos;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '{') {
            if (depth == 0) open = i;
            depth++;
        } else if (pattern[i] == '}') {
            depth--;
            if (depth < 0) return false;
            if (depth == 0 && open != std::string::npos) {
                std::vector<std::string> alternatives;
                std::string current;
                int inner = 0;
                for (size_t j = open + 1; j < i; j++) {
                    const char c = pattern[j];
                    if (c == '{') inner++;
                    if (c == '}') inner--;
                    if (c == ',' && inner == 0) {
                        alternatives.push_back(current);
                        current.clear();
                    } else {
                        current.push_back(c);
                    }
                }
                alternatives.push_back(current);

                for (const std::string& alternative : alternatives) {
                    const std::string expanded = pattern.substr(0, open) +
                                                 alternative +
                                                 pattern.substr(i + 1);
                    if (!ExpandBracesOnce(expanded, out)) return false;
                    if (out.size() > 1024) return false;
                }
                return true;
            }
        }
    }
    if (depth != 0) return false;
    out.push_back(pattern);
    return true;
}

bool ExpandBraces(const std::string& pattern, std::vector<std::string>& out) {
    out.clear();
    return ExpandBracesOnce(pattern, out) && out.size() <= 1024;
}

std::string GlobToRegex(const std::string& pattern) {
    std::string regex = "^";
    const size_t n = pattern.size();
    for (size_t i = 0; i < n; i++) {
        char c = pattern[i];
        if (c == '*') {
            size_t count = 1;
            while (i + count < n && pattern[i + count] == '*') count++;
            const bool prev_separator = i == 0 || pattern[i - 1] == '/';
            const bool next_separator = i + count == n || pattern[i + count] == '/';
            if (count >= 2 && prev_separator && next_separator) {
                i += count - 1;
                if (i + 1 < n && pattern[i + 1] == '/') {
                    i++;
                    regex += "(?:[^/]+/)*";
                } else {
                    regex += ".*";
                }
            } else {
                regex += "[^/]*";
                i += count - 1;
            }
        } else if (c == '?') {
            regex += "[^/]";
        } else if (c == '[') {
            size_t close = i + 1;
            if (close < n && (pattern[close] == '!' || pattern[close] == '^')) close++;
            if (close < n && pattern[close] == ']') close++;
            while (close < n && pattern[close] != ']') close++;
            if (close >= n) {
                regex += "\\[";
            } else {
                regex += "[";
                size_t start = i + 1;
                if (pattern[start] == '!') {
                    regex += "^";
                    start++;
                } else if (pattern[start] == '^') {
                    regex += "\\^";
                    start++;
                }
                for (size_t j = start; j < close; j++) {
                    if (pattern[j] == '\\') regex += "\\\\";
                    else regex += pattern[j];
                }
                regex += "]";
                i = close;
            }
        } else {
            regex += EscapeRegex(c);
        }
    }
    regex += "$";
    return regex;
}

bool GlobMatchInner(const std::string& pattern, const std::string& path) {
    try {
        std::regex compiled(GlobToRegex(pattern));
        return std::regex_match(path, compiled);
    } catch (const std::regex_error&) {
        return false;
    }
}

std::string BaseName(std::string_view path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string_view::npos) return std::string(path);
    return std::string(path.substr(slash + 1));
}

bool IsSupportedExtension(const std::string& path) {
    static const std::unordered_set<std::string> extensions = {
        ".js", ".jsx", ".ts", ".tsx", ".mjs", ".cjs", ".mts", ".cts",
    };
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return false;
    }
    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extensions.contains(extension);
}

bool IsDefaultExcludedSegment(const std::string& name) {
    return name == "node_modules" || name == ".git" || name == "dist" ||
           name == "build" || name == "coverage";
}

bool MatchesAny(const std::vector<std::string>& patterns, std::string_view path) {
    for (const std::string& pattern : patterns) {
        if (GlobMatch(pattern, path)) return true;
    }
    return false;
}

bool DirectoryIsExcluded(const Config& config, std::string_view relative) {
    const std::string normalized = NormalizeSeparators(std::string(relative));
    if (normalized.empty()) return false;
    const std::string base = BaseName(normalized);
    if (IsDefaultExcludedSegment(base)) return true;
    for (const std::string& pattern : config.exclude) {
        if (PatternCoversDirectory(pattern, normalized)) return true;
    }
    return false;
}

struct Walker {
    filesystem::Fs& fs;
    const Config& config;
    const std::string& cwd;
    std::vector<std::string> includes{};
    bool use_includes{};
    std::vector<std::string> files{};
    std::unordered_set<std::string> seen_files{};
    std::unordered_set<std::string> seen_dirs{};
    std::string error{};
    size_t file_limit{100000};

    std::string Relative(const std::string& path) const {
        if (auto rel = fs.Rel(cwd, path); rel && !rel->empty()) {
            return NormalizeSeparators(*rel);
        }
        return NormalizeSeparators(path);
    }

    void AddFile(const std::string& path, bool explicit_file) {
        if (!explicit_file) {
            if (!IsSupportedExtension(path)) return;
            const std::string relative = Relative(path);
            if (use_includes && !MatchesAny(includes, relative) &&
                !MatchesAny(includes, NormalizeSeparators(path))) {
                return;
            }
            if (MatchesAny(config.exclude, relative)) return;
            for (const std::string& segment : SplitSegments(relative)) {
                if (IsDefaultExcludedSegment(segment)) return;
            }
        } else {
            const std::string relative = Relative(path);
            if (MatchesAny(config.exclude, relative)) return;
        }

        const std::string canonical = fs.EvalSymlinks(path).value_or(path);
        if (!seen_files.insert(canonical).second) return;
        files.push_back(path);
    }

    static std::vector<std::string> SplitSegments(std::string_view path) {
        std::vector<std::string> segments;
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const size_t end = slash == std::string_view::npos ? path.size() : slash;
            if (end > start) segments.emplace_back(path.substr(start, end - start));
            if (slash == std::string_view::npos) break;
            start = slash + 1;
        }
        return segments;
    }

    void Walk(const std::string& dir, int depth) {
        if (depth > 256) {
            if (error.empty()) error = "directory tree is too deep";
            return;
        }
        if (files.size() >= file_limit) {
            if (error.empty()) error = "too many source files (limit is 100000)";
            return;
        }

        const std::string canonical = fs.EvalSymlinks(dir).value_or(dir);
        if (!seen_dirs.insert(canonical).second) return;

        filesystem::FsResult<filesystem::DirEntries> result =
            fs.ReadDirectory(dir);
        if (!result.Ok()) {
            if (error.empty()) {
                error = "cannot read directory " + dir + ": " +
                        result.original_error;
            }
            return;
        }

        for (const std::string& name : result.value.SortedKeys()) {
            if (files.size() >= file_limit) {
                if (error.empty()) error = "too many source files (limit is 100000)";
                break;
            }
            auto [entry, different_case] = result.value.Get(name);
            (void)different_case;
            if (entry == nullptr) continue;
            const std::string full = fs.Join({dir, name});
            const filesystem::EntryKind kind = entry->Kind(fs);
            if (kind == filesystem::EntryKind::kDir) {
                const std::string relative = Relative(full);
                if (DirectoryIsExcluded(config, relative)) continue;
                Walk(full, depth + 1);
            } else if (kind == filesystem::EntryKind::kFile) {
                AddFile(full, false);
            }
        }
    }
};

bool IsDirectory(filesystem::Fs& fs, const std::string& path) {
    filesystem::FsResult<filesystem::DirEntries> entries =
        fs.ReadDirectory(path);
    return entries.Ok();
}

bool IsFile(filesystem::Fs& fs, const std::string& path) {
    filesystem::FsResult<filesystem::DirEntries> entries =
        fs.ReadDirectory(fs.Dir(path));
    if (!entries.Ok()) return false;
    auto [entry, different_case] = entries.value.Get(fs.Base(path));
    (void)different_case;
    return entry != nullptr && entry->Kind(fs) == filesystem::EntryKind::kFile;
}

std::string RootForPattern(filesystem::Fs& fs, const std::string& cwd,
                           const std::string& pattern) {
    size_t meta = std::string::npos;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '*' || pattern[i] == '?' || pattern[i] == '[' ||
            pattern[i] == '{') {
            meta = i;
            break;
        }
    }
    if (meta == std::string::npos) return cwd;
    const std::string prefix = pattern.substr(0, meta);
    const size_t slash = prefix.find_last_of('/');
    if (slash == std::string::npos) return cwd;
    std::string root = prefix.substr(0, slash);
    if (root.empty()) root = "/";
    if (!fs.IsAbs(root)) root = fs.Join({cwd, root});
    std::optional<std::string> absolute = fs.Abs(root);
    return absolute ? *absolute : root;
}

} // namespace

bool HasGlobMeta(std::string_view pattern) {
    return pattern.find_first_of("*?[{") != std::string_view::npos;
}

bool ValidateGlobPattern(std::string_view input, std::string& error) {
    const std::string pattern(input);
    int brace_depth = 0;
    bool in_class = false;
    for (size_t i = 0; i < pattern.size(); i++) {
        const char c = pattern[i];
        if (c == '\\') {
            error = "escape sequences are not supported";
            return false;
        }
        if (in_class) {
            if (c == ']') in_class = false;
            continue;
        }
        if (c == '[') {
            in_class = true;
            continue;
        }
        if (c == '{') {
            brace_depth++;
            continue;
        }
        if (c == '}') {
            brace_depth--;
            if (brace_depth < 0) {
                error = "unbalanced \"}\"";
                return false;
            }
            continue;
        }
        if (c == '(' || c == ')' || c == '|' || c == '+' || c == '@') {
            error = std::string("unsupported character \"") + c + "\"";
            return false;
        }
        if (c == '!' && brace_depth == 0) {
            error = "unsupported negation";
            return false;
        }
    }
    if (in_class) {
        error = "unclosed character class";
        return false;
    }
    if (brace_depth != 0) {
        error = "unbalanced \"{\"";
        return false;
    }

    std::vector<std::string> expanded;
    if (!ExpandBraces(pattern, expanded)) {
        error = "invalid brace expansion";
        return false;
    }
    for (const std::string& value : expanded) {
        if (value.find("***") != std::string::npos) {
            error = "too many consecutive \"*\" characters";
            return false;
        }
    }
    return true;
}

bool GlobMatch(std::string_view input_pattern, std::string_view input_path) {
    const std::string pattern = NormalizeSeparators(std::string(input_pattern));
    const std::string path = NormalizeSeparators(std::string(input_path));
    std::vector<std::string> expanded;
    if (!ExpandBraces(pattern, expanded)) return false;
    for (const std::string& candidate : expanded) {
        if (GlobMatchInner(candidate, path)) return true;
        if (candidate.find('/') == std::string::npos &&
            GlobMatchInner(candidate, BaseName(path))) {
            return true;
        }
    }
    return false;
}

bool PatternCoversDirectory(std::string_view input_pattern,
                            std::string_view input_dir) {
    std::string pattern = NormalizeSeparators(std::string(input_pattern));
    const std::string dir = NormalizeSeparators(std::string(input_dir));
    while (pattern.ends_with("/")) pattern.pop_back();
    if (pattern.ends_with("/**")) {
        pattern.erase(pattern.size() - 3);
    } else if (pattern.ends_with("/*")) {
        pattern.erase(pattern.size() - 2);
    } else if (!HasGlobMeta(pattern)) {
        return pattern == dir;
    }
    if (pattern == dir) return true;
    return GlobMatch(pattern, dir);
}

bool DiscoverFiles(filesystem::Fs& fs, const Config& config,
                   const std::vector<std::string>& paths, const std::string& cwd,
                   std::vector<std::string>& files, std::string& error) {
    files.clear();
    error.clear();

    Walker walker{fs, config, cwd};
    walker.includes = config.include;

    if (paths.empty()) {
        walker.use_includes = !config.include.empty();
        walker.Walk(cwd, 0);
        if (!walker.error.empty()) {
            error = walker.error;
            return false;
        }
        files = std::move(walker.files);
        return true;
    }

    for (const std::string& argument : paths) {
        if (HasGlobMeta(argument)) {
            std::string glob_error;
            if (!ValidateGlobPattern(argument, glob_error)) {
                error = "invalid glob pattern \"" + argument + "\": " + glob_error;
                return false;
            }

            const std::string root = RootForPattern(fs, cwd, argument);
            Walker pattern_walker{fs, config, cwd};
            pattern_walker.includes = {argument};
            pattern_walker.use_includes = true;

            pattern_walker.Walk(root, 0);
            if (!pattern_walker.error.empty()) {
                error = pattern_walker.error;
                return false;
            }
            if (pattern_walker.files.empty()) {
                error = "no files matched pattern \"" + argument + "\"";
                return false;
            }
            for (const std::string& file : pattern_walker.files) {
                walker.AddFile(file, true);
            }
            continue;
        }

        const std::string full = fs.IsAbs(argument)
                                     ? argument
                                     : fs.Join({cwd, argument});
        std::optional<std::string> absolute = fs.Abs(full);
        const std::string resolved = absolute ? *absolute : full;

        if (IsDirectory(fs, resolved)) {
            Walker dir_walker{fs, config, cwd};
            dir_walker.includes = config.include;
            dir_walker.use_includes = !config.include.empty();
            dir_walker.Walk(resolved, 0);
            if (!dir_walker.error.empty()) {
                error = dir_walker.error;
                return false;
            }
            for (const std::string& file : dir_walker.files) {
                walker.AddFile(file, true);
            }
            continue;
        }

        if (IsFile(fs, resolved)) {
            if (!IsSupportedExtension(resolved)) {
                error = "unsupported source file: " + argument;
                return false;
            }
            walker.AddFile(resolved, true);
            continue;
        }

        error = "path does not exist: " + argument;
        return false;
    }

    files = std::move(walker.files);
    std::sort(files.begin(), files.end());
    return true;
}

} // namespace guchho::lint::detail
