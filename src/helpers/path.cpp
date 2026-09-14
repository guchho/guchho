#include "guchho/helpers.hpp"

namespace guchho::helpers {

    // Returns true if the given path contains a "node_modules" directory
    // at any level.
    //
    // The function walks up the path by splitting on both '/' and '\'
    // separators, checking each component against the literal string
    // "node_modules".  It stops as soon as a match is found or the path
    // is exhausted.
    //
    // Example:
    //   IsInsideNodeModules("src/node_modules/pkg/index.js") => true
    //   IsInsideNodeModules("vendor/lib.js")                  => false
    //
    // Edge cases:
    //   - Empty string: returns false.
    //   - Path that IS "node_modules" (no separators): returns true.
    //   - Windows-style backslashes are handled identically to forward
    //     slashes.
    bool IsInsideNodeModules(std::string_view path)
    {
        while (true) {
            auto slash = path.find_last_of("/\\");
            if (slash == std::string_view::npos) {
                return false;
            }
            auto dir  = path.substr(0, slash);
            auto base = path.substr(slash + 1);
            if (base == "node_modules") {
                return true;
            }
            path = dir;
        }
    }

    // Returns true when the three URL components describe a local file URL.
    //
    // A file URL is identified by:
    //   - scheme == "file"
    //   - host is empty or "localhost"
    //   - path is non-empty and starts with '/'
    //
    // Example:
    //   IsFileURL("file", "", "/home/user/file.js")  => true
    //   IsFileURL("file", "localhost", "/tmp/a.txt")  => true
    //   IsFileURL("http", "", "/index.html")          => false
    //   IsFileURL("file", "example.com", "/a.txt")    => false
    bool IsFileURL(std::string_view scheme, std::string_view host, std::string_view path)
    {
        return scheme == "file" &&
            (host.empty() || host == "localhost") &&
            !path.empty() && path[0] == '/';
    }

    // Converts a native file system path to a file URL string.
    //
    // Backslashes are normalised to forward slashes, and a leading
    // slash is inserted after "file://" if the path does not already
    // start with one (e.g. on Windows where paths typically begin with
    // a drive letter like "C:/...").
    //
    // The function does NOT percent-encode special characters in the
    // path - callers must handle that separately if needed.
    //
    // Example:
    //   FileURLFromFilePath("/home/user/file.js")
    //   => "file:///home/user/file.js"
    //
    //   FileURLFromFilePath("C:\\Users\\test\\a.txt")
    //   => "file:///C:/Users/test/a.txt"
    std::string FileURLFromFilePath(std::string_view filePath)
    {
        std::string result;
        result.reserve(24 + filePath.size());

        result = "file://";

        for (auto c : filePath) {
            if (c == '\\') {
                result.push_back('/');
            } else {
                result.push_back(c);
            }
        }

        if (result.size() < 8 || result[7] != '/') {
            result.insert(result.begin() + 7, '/');
        }

        return result;
    }

    // Converts a file URL path component to a native file system path.
    //
    // When `cwd` is empty or does not start with '/' (i.e. we are on
    // Windows), the leading '/' of the URL path is stripped and forward
    // slashes are converted to backslashes so the result is a valid
    // Windows path.  On POSIX systems (cwd starts with '/') the path
    // is returned unchanged.
    //
    // Example:
    //   FilePathFromFileURL("/home/user/file.js", "/home/user")
    //   => "/home/user/file.js"  (POSIX, unchanged)
    //
    //   FilePathFromFileURL("/C:/Users/test/a.txt", "C:\\")
    //   => "C:/Users/test/a.txt"  (leading slash stripped)
    std::string FilePathFromFileURL(std::string_view urlPath, std::string_view cwd)
    {
        std::string path(urlPath);

        if (cwd.empty() || cwd[0] != '/') {
            if (!path.empty() && path[0] == '/') {
                path.erase(path.begin());
            }
            for (auto& c : path) {
                if (c == '/') {
                    c = '\\';
                }
            }
        }

        return path;
    }

    std::vector<std::string> SplitPathSegments(std::string_view path)
    {
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos <= path.size()) {
            const size_t end = path.find('/', pos);
            const std::string_view seg =
                path.substr(pos, end == std::string_view::npos ? path.size() - pos
                                                               : end - pos);
            if (!seg.empty() && seg != ".") {
                parts.emplace_back(seg);
            }
            if (end == std::string_view::npos) {
                break;
            }
            pos = end + 1;
        }
        return parts;
    }

    std::string MakeRelativePath(std::string_view from_file, std::string_view to_file)
    {
        std::vector<std::string> from = SplitPathSegments(from_file);
        std::vector<std::string> to = SplitPathSegments(to_file);
        // The "from" file contributes its directory (drop the file name).
        if (!from.empty()) {
            from.pop_back();
        }
        // Drop the common directory prefix.
        size_t common = 0;
        while (common < from.size() && common < to.size() && from[common] == to[common]) {
            common++;
        }
        std::string result;
        for (size_t i = common; i < from.size(); i++) {
            result += "../";
        }
        for (size_t i = common; i < to.size(); i++) {
            if (i != common) {
                result += '/';
            }
            result += to[i];
        }
        if (result.empty()) {
            result = ".";
        }
        return result;
    }

    std::string AddDotSlashPrefix(std::string_view rel_path)
    {
        if (rel_path.rfind("../", 0) == 0 || rel_path == ".." || rel_path == "." ||
            (!rel_path.empty() && rel_path[0] == '/')) {
            return std::string(rel_path);
        }
        return std::string("./") + std::string(rel_path);
    }

    bool IsPublicPathConfigured(std::string_view public_path)
    {
        return !public_path.empty() && public_path != "./" && public_path != ".";
    }

    std::string JoinPublicPath(std::string_view public_path, std::string_view rel_path)
    {
        if (!IsPublicPathConfigured(public_path)) {
            return std::string(rel_path);
        }
        std::string rel(rel_path);
        if (rel.size() >= 2 && rel[0] == '.' && rel[1] == '/') {
            rel = rel.substr(2);
            while (true) {
                if (!rel.empty() && rel[0] == '/') {
                    rel = rel.substr(1);
                } else if (rel.size() >= 2 && rel[0] == '.' && rel[1] == '/') {
                    rel = rel.substr(2);
                } else {
                    break;
                }
            }
        }
        const std::string_view sep = public_path.ends_with('/') ? "" : "/";
        return std::string(public_path) + std::string(sep) + rel;
    }

}