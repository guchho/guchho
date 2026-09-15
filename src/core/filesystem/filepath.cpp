#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>

namespace guchho::filesystem {

    namespace {

        // Returns true when "c" is a forward slash or backslash.  Both are
        // recognised as path separators so that the same logic can be shared
        // across Unix and Windows without per-platform branching.
        bool isSlash(char c)
        {
            return c == '\\' || c == '/';
        }

        // Table of Windows reserved file names.  These names (case-
        // insensitively) cannot be used as file or directory names, with or
        // without an extension, on any Windows volume.  The list is taken
        // from the Windows file-naming rules and includes the serial-port
        // and parallel-port device names COM1–COM9 and LPT1–LPT9.
        const char* kReservedNames[] = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        };

        // Checks whether "path" matches any entry in kReservedNames using a
        // case-insensitive ASCII comparison.  An empty string is never
        // considered reserved.
        //
        // Example:
        //   isReservedName("CON")   => true
        //   isReservedName("con")   => true
        //   isReservedName("CON2")  => false
        //   isReservedName("")      => false
        bool isReservedName(std::string_view path)
        {
            if (path.empty()) {
                return false;
            }
            for (const char* reserved : kReservedNames) {
                if (helpers::EqualFoldASCII(path, reserved)) {
                    return true;
                }
            }
            return false;
        }

        // A growable string buffer that supports appending individual bytes
        // and reading back previously written bytes.  "w" tracks the logical
        // write cursor; bytes beyond w are stale but still present in the
        // underlying string so backtracking is free.  "sep" is the platform
        // path separator and is used by callers when they need to append a
        // separator before a new element.
        struct LazyBuf {
            char        sep;
            std::string buf;
            size_t      w = 0;

            // Appends one byte to the buffer, extending it when w has not yet
            // reached the current string length.
            void Append(char c)
            {
                if (w == buf.size()) {
                    buf.push_back(c);
                } else {
                    buf[w] = c;
                }
                w++;
            }

            // Returns the byte at logical index "i".  The index must refer to
            // a byte that was previously written.
            char Index(size_t i) const
            {
                return buf[i];
            }
        };

        // Joins elements from "elem" starting at index "first" into a single
        // string separated by "separator".  The separator is only inserted
        // between elements, never before the first or after the last.
        //
        // Example:
        //   JoinWithSeparator('/', {"a","b","c"}, 1) => "b/c"
        //   JoinWithSeparator('/', {"x"}, 0)         => "x"
        std::string JoinWithSeparator(char separator, const std::vector<std::string>& elem, size_t first = 0)
        {
            std::string joined;
            for (size_t i = first; i < elem.size(); i++) {
                if (i > first) {
                    joined += separator;
                }
                joined += elem[i];
            }
            return joined;
        }

        // Returns true when "path" is a UNC (Universal Naming Convention)
        // path, i.e. its volume prefix is longer than two characters.
        // UNC paths look like "\\server\share\..." and their volume prefix
        // includes both the server and share components.
        bool isUNC(const GoFilepath& fp, std::string_view path)
        {
            return fp.VolumeNameLen(path) > 2;
        }

        // Joins path elements assuming that "elem[0]" is non-empty.  This is
        // the Windows fast-path that must avoid accidentally creating a UNC
        // path when neither input is UNC.  For example joining "C:" and
        // "\\server\share" must not produce "\\server\share" as a bare
        // drive-relative path — the result is kept relative to the C: drive.
        //
        // Example (Windows):
        //   joinNonEmpty(fp, {"C:", "", "foo", "bar"}) => "C:foo\\bar"
        //   joinNonEmpty(fp, {"C:\\", "foo", "bar"})   => "C:\\foo\\bar"
        std::string joinNonEmpty(const GoFilepath& fp, const std::vector<std::string>& elem)
        {
            if (elem[0].size() == 2 && elem[0][1] == ':') {
                // First element is drive letter without terminating slash.
                // Keep path relative to current directory on that drive.
                // Skip empty elements.
                size_t i = 1;
                for (; i < elem.size(); i++) {
                    if (!elem[i].empty()) {
                        break;
                    }
                }
                return fp.Clean(elem[0] + JoinWithSeparator(fp.path_separator, elem, i));
            }
            // The following logic prevents Join from inadvertently creating a
            // UNC path on Windows. Unless the first element is a UNC path, Join
            // shouldn't create a UNC path. See golang.org/issue/9167.
            std::string p = fp.Clean(JoinWithSeparator(fp.path_separator, elem));
            if (!isUNC(fp, p)) {
                return p;
            }
            // p == UNC only allowed when the first element is a UNC path.
            std::string head = fp.Clean(elem[0]);
            if (isUNC(fp, head)) {
                return p;
            }
            // head + tail == UNC, but joining two non-UNC paths should not result
            // in a UNC path. Undo creation of UNC path.
            std::string tail = fp.Clean(JoinWithSeparator(fp.path_separator, elem, 1));
            if (!head.empty() && head.back() == fp.path_separator) {
                return head + tail;
            }
            return head + fp.path_separator + tail;
        }

    } // namespace

    ////////////////////////////////////////////////////////////////////////////////

    // Returns true when "path" is an absolute path.  On Unix an absolute path
    // starts with '/'.  On Windows it may also start with a drive letter
    // ("C:\") or a UNC prefix ("\\server\share").  Reserved names like "CON"
    // are treated as absolute because Windows treats them as device paths.
    //
    // Example:
    //   IsAbs("/src/main.cpp")   => true   (Unix mode)
    //   IsAbs("src/main.cpp")    => false  (Unix mode)
    //   IsAbs("C:\\Windows")     => true   (Windows mode)
    //   IsAbs("C:foo")           => false  (Windows mode, relative to C:)
    bool GoFilepath::IsAbs(std::string_view path) const
    {
        if (!is_windows) {
            return !path.empty() && path.front() == '/';
        }
        if (isReservedName(path)) {
            return true;
        }
        int l = VolumeNameLen(path);
        if (l == 0) {
            return false;
        }
        if (size_t(l) >= path.size()) {
            return true;
        }
        path = path.substr(size_t(l));
        return isSlash(path[0]);
    }

    // Returns an absolute form of "path".  If "path" is already absolute it
    // is normalised via Clean() and returned.  Otherwise it is joined with
    // the stored working directory.  The result is not guaranteed to be
    // unique (e.g. two different relative paths may resolve to the same
    // absolute path).
    //
    // Example:
    //   cwd = "/project"
    //   Abs("src/main.cpp")  => "/project/src/main.cpp"
    //   Abs("/etc/passwd")   => "/etc/passwd"
    std::string GoFilepath::Abs(std::string_view path) const
    {
        if (IsAbs(path)) {
            return Clean(path);
        }
        return Join({std::string(cwd), std::string(path)});
    }

    // Returns true when "c" is a path separator for the current platform.
    // On Unix only '/' qualifies.  On Windows both '/' and '\\' are
    // recognised, because Windows APIs accept either form.
    bool GoFilepath::IsPathSeparator(char c) const
    {
        return c == '/' || (is_windows && c == '\\');
    }

    // Returns the number of bytes that form the volume prefix at the start
    // of "path".  On Unix this is always 0 because Unix has no concept of
    // volume prefixes.  On Windows two forms are recognised:
    //
    //   1. Drive-letter prefix: "C:" => length 2.
    //   2. UNC prefix: "\\server\share" => length includes the server and
    //      share components (e.g. 16 for "\\server\share").
    //
    // A trailing slash immediately after the volume prefix is not part of
    // the prefix and is not counted.
    //
    // Example:
    //   VolumeNameLen("C:\\src\\main.cpp") => 2
    //   VolumeNameLen("\\\\server\\share") => 16
    //   VolumeNameLen("/usr/bin")          => 0
    int GoFilepath::VolumeNameLen(std::string_view path) const
    {
        if (!is_windows) {
            return 0;
        }
        if (path.size() < 2) {
            return 0;
        }
        // with drive letter
        char c = path[0];
        if (path[1] == ':' && (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z'))) {
            return 2;
        }
        // is it UNC? https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
        size_t l = path.size();
        if (l >= 5 && isSlash(path[0]) && isSlash(path[1]) &&
            !isSlash(path[2]) && path[2] != '.') {
            // first, leading `\\` and next shouldn't be `\`. its server name.
            for (size_t n = 3; n < l - 1; n++) {
                // second, next '\' shouldn't be repeated.
                if (isSlash(path[n])) {
                    n++;
                    // third, following something characters. its share name.
                    if (!isSlash(path[n])) {
                        if (path[n] == '.') {
                            break;
                        }
                        for (; n < l; n++) {
                            if (isSlash(path[n])) {
                                break;
                            }
                        }
                        return int(n);
                    }
                    break;
                }
            }
        }
        return 0;
    }

    // Resolves every symbolic link in "path" and returns the canonical
    // real path.  If the path is relative, the result is relative to the
    // working directory unless an intermediate component is an absolute
    // symlink.  The result is normalised via Clean().
    //
    // Returns std::nullopt when:
    //   - Any component of the path does not exist.
    //   - A symlink loop is detected (more than 255 links walked).
    //   - A symlink target cannot be read.
    //
    // Example:
    //   /usr/local/bin/gcc => /usr/bin/gcc-12
    //   EvalSymlinks("/usr/local/bin/gcc") => "/usr/bin/gcc-12"
    std::optional<std::string> GoFilepath::EvalSymlinks(std::string_view path_input) const
    {
        std::string path(path_input);
        int         vol_len = VolumeNameLen(path);

        if (vol_len < int(path.size()) && IsPathSeparator(path[size_t(vol_len)])) {
            vol_len++;
        }
        std::string vol  = path.substr(0, size_t(vol_len));
        std::string dest = vol;
        int         links_walked = 0;

        for (int start = vol_len, end = vol_len; start < int(path.size()); start = end) {
            while (start < int(path.size()) && IsPathSeparator(path[size_t(start)])) {
                start++;
            }
            end = start;
            while (end < int(path.size()) && !IsPathSeparator(path[size_t(end)])) {
                end++;
            }

            // On Windows, "." can be a symlink.
            // We look it up, and use the value if it is absolute.
            // If not, we just return ".".
            bool is_windows_dot = is_windows && path.substr(size_t(VolumeNameLen(path))) == ".";

            // The next path component is in path[start:end].
            if (end == start) {
                // No more path components.
                break;
            }

            std::string component = path.substr(size_t(start), size_t(end - start));
            if (component == "." && !is_windows_dot) {
                // Ignore path component ".".
                continue;
            }
            if (component == "..") {
                // Back up to previous component if possible.
                // Note that volLen includes any leading slash.

                // Set r to the index of the last slash in dest,
                // after the volume.
                int r = int(dest.size()) - 1;
                for (; r >= vol_len; r--) {
                    if (IsPathSeparator(dest[size_t(r)])) {
                        break;
                    }
                }
                if (r < vol_len || dest.substr(size_t(r) + 1) == "..") {
                    // Either path has no slashes
                    // (it's empty or just "C:")
                    // or it ends in a ".." we had to keep.
                    // Either way, keep this "..".
                    if (dest.size() > size_t(vol_len)) {
                        dest += path_separator;
                    }
                    dest += "..";
                } else {
                    // Discard everything since the last slash.
                    dest.resize(size_t(r));
                }
                continue;
            }

            // Ordinary path component. Add it to result.

            if (dest.size() > size_t(VolumeNameLen(dest)) && !IsPathSeparator(dest.back())) {
                dest += path_separator;
            }

            dest += component;

            // Resolve symlink.

            std::error_code ec;
            auto            status = std::filesystem::symlink_status(PathFromUTF8(dest), ec);
            if (ec) {
                return std::nullopt;
            }

            if (status.type() != std::filesystem::file_type::symlink) {
                bool is_dir = status.type() == std::filesystem::file_type::directory;
                if (!is_dir && end < int(path.size())) {
                    return std::nullopt; // syscall.ENOTDIR
                }
                continue;
            }

            // Found symlink.

            links_walked++;
            if (links_walked > 255) {
                return std::nullopt; // "EvalSymlinks: too many links"
            }

            std::filesystem::path link_target = std::filesystem::read_symlink(PathFromUTF8(dest), ec);
            if (ec) {
                return std::nullopt;
            }
            std::string link = PathToUTF8(link_target);

            if (is_windows_dot && !IsAbs(link)) {
                // On Windows, if "." is a relative symlink,
                // just return ".".
                break;
            }

            path = link + path.substr(size_t(end));

            int v = VolumeNameLen(link);
            if (v > 0) {
                // Symlink to drive name is an absolute path.
                if (v < int(link.size()) && IsPathSeparator(link[size_t(v)])) {
                    v++;
                }
                vol  = link.substr(0, size_t(v));
                dest = vol;
                end  = int(vol.size());
            } else if (!link.empty() && IsPathSeparator(link[0])) {
                // Symlink to absolute path.
                dest = link.substr(0, 1);
                end  = 1;
            } else {
                // Symlink to relative path; replace last
                // path component in dest.
                int r = int(dest.size()) - 1;
                for (; r >= vol_len; r--) {
                    if (IsPathSeparator(dest[size_t(r)])) {
                        break;
                    }
                }
                if (r < vol_len) {
                    dest = vol;
                } else {
                    dest.resize(size_t(r));
                }
                end = 0;
            }
        }

        return Clean(dest);
    }

    // Returns the shortest path name lexically equivalent to "path" by
    // purely textual processing — no file-system calls are made.  The
    // following rules are applied iteratively until a fixed point:
    //
    //   1. Replace multiple consecutive separators with a single one.
    //   2. Eliminate each "." element (the current directory).
    //   3. Eliminate each inner ".." element along with the non-".."
    //      element that immediately precedes it.
    //   4. Eliminate ".." elements that begin a rooted path, i.e.
    //      replace leading "/.." with "/".
    //
    // The returned path ends in a separator only if it represents a root
    // directory (e.g. "/" on Unix or "C:\\" on Windows).  If the result
    // would be empty, "." is returned.
    //
    // Example:
    //   Clean("/a/b/../c/./d") => "/a/c/d"
    //   Clean("a//b")          => "a/b"
    //   Clean("../../a")       => "../../a"
    //   Clean("/")             => "/"
    std::string GoFilepath::Clean(std::string_view original_path) const
    {
        int         vol_len = VolumeNameLen(original_path);
        std::string vol(original_path.substr(0, size_t(vol_len)));
        std::string_view path = original_path.substr(size_t(vol_len));

        if (path.empty()) {
            if (vol_len > 1 && original_path[1] != ':') {
                // should be UNC
                return FromSlash(std::string(original_path));
            }
            return std::string(original_path) + ".";
        }

        bool   rooted = IsPathSeparator(path[0]);
        size_t n      = path.size();

        // Invariants:
        //	reading from path; r is index of next byte to process.
        //	writing to buf; w is index of next byte to write.
        //	dotdot is index in buf where .. must stop, either because
        //		it is the leading slash or it is a leading ../../.. prefix.
        LazyBuf out{path_separator};
        size_t  r = 0, dotdot = 0;
        if (rooted) {
            out.Append(path_separator);
            r = dotdot = 1;
        }

        while (r < n) {
            if (IsPathSeparator(path[r])) {
                // empty path element
                r++;
            } else if (path[r] == '.' && (r + 1 == n || IsPathSeparator(path[r + 1]))) {
                // . element
                r++;
            } else if (path[r] == '.' && path[r + 1] == '.' && (r + 2 == n || IsPathSeparator(path[r + 2]))) {
                // .. element: remove to last separator
                r += 2;
                if (out.w > dotdot) {
                    // can backtrack
                    out.w--;
                    while (out.w > dotdot && !IsPathSeparator(out.Index(out.w))) {
                        out.w--;
                    }
                } else if (!rooted) {
                    // cannot backtrack, but not rooted, so append .. element.
                    if (out.w > 0) {
                        out.Append(path_separator);
                    }
                    out.Append('.');
                    out.Append('.');
                    dotdot = out.w;
                }
            } else {
                // real path element.
                // add slash if needed
                if ((rooted && out.w != 1) || (!rooted && out.w != 0)) {
                    out.Append(path_separator);
                }
                // copy element
                for (; r < n && !IsPathSeparator(path[r]); r++) {
                    out.Append(path[r]);
                }
            }
        }

        // Turn empty string into "."
        if (out.w == 0) {
            out.Append('.');
        }

        return FromSlash(vol + out.buf.substr(0, out.w));
    }

    // Returns the volume prefix of "path".  On Unix this is always empty.
    // On Windows it includes the drive letter or UNC server-and-share.
    //
    // Example:
    //   VolumeName("C:\\foo\\bar")      => "C:"
    //   VolumeName("\\\\host\\share")   => "\\\\host\\share"
    //   VolumeName("/usr/bin")          => ""
    std::string GoFilepath::VolumeName(std::string_view path) const
    {
        return std::string(path.substr(0, size_t(VolumeNameLen(path))));
    }

    // Returns the last element of "path" (the file name or final directory
    // component).  Trailing separators are stripped first.  If the path is
    // empty, "." is returned.  If the path consists entirely of separators,
    // a single separator is returned.  The volume prefix is stripped before
    // extracting the last element.
    //
    // Example:
    //   Base("/src/main.cpp") => "main.cpp"
    //   Base("/usr/bin/")     => "bin"
    //   Base("C:\\")          => "\\"
    //   Base("")              => "."
    std::string GoFilepath::Base(std::string_view path_input) const
    {
        if (path_input.empty()) {
            return ".";
        }
        std::string_view path = path_input;
        // Strip trailing slashes.
        while (!path.empty() && IsPathSeparator(path.back())) {
            path.remove_suffix(1);
        }
        // Throw away volume name
        path.remove_prefix(VolumeName(path).size());
        // Find the last element
        size_t i = path.size();
        while (i > 0 && !IsPathSeparator(path[i - 1])) {
            i--;
        }
        if (i > 0) {
            path = path.substr(i);
        }
        // If empty now, it had only slashes.
        if (path.empty()) {
            return std::string(1, path_separator);
        }
        return std::string(path);
    }

    // Returns all but the last element of "path", typically the parent
    // directory.  The result is normalised via Clean(), so trailing
    // separators are removed.  If the path is empty, "." is returned.
    // If the path consists entirely of separators, a single separator is
    // returned.  The result never ends in a separator unless it is the
    // root directory itself.
    //
    // Example:
    //   Dir("/src/main.cpp")  => "/src"
    //   Dir("/a/b/c")         => "/a/b"
    //   Dir("/")              => "/"
    //   Dir("C:\\")           => "C:\\"
    std::string GoFilepath::Dir(std::string_view path) const
    {
        std::string vol = VolumeName(path);
        int         i   = int(path.size()) - 1;
        for (; i >= int(vol.size()) && !IsPathSeparator(path[size_t(i)]); i--) {
        }
        std::string dir = Clean(path.substr(vol.size(), size_t(i + 1) - vol.size()));
        if (dir == "." && vol.size() > 2) {
            // must be UNC
            return vol;
        }
        return vol + dir;
    }

    // Returns the file extension of "path", including the leading dot.
    // The extension is the suffix starting at the final dot in the final
    // path component.  If there is no dot in the final component, an
    // empty string is returned.
    //
    // Example:
    //   Ext("/src/main.cpp")  => ".cpp"
    //   Ext("/src/Makefile")  => ""
    //   Ext("archive.tar.gz") => ".gz"
    //   Ext("/a.b/c")         => ""
    std::string GoFilepath::Ext(std::string_view path) const
    {
        for (size_t i = path.size(); i > 0;) {
            i--;
            if (IsPathSeparator(path[i])) {
                break;
            }
            if (path[i] == '.') {
                return std::string(path.substr(i));
            }
        }
        return "";
    }

    // Joins path elements with the platform separator and normalises the
    // result via Clean().  Empty elements are silently skipped.  If every
    // element is empty (or the list is empty), an empty string is returned.
    //
    // On Windows the result is only a UNC path when the first non-empty
    // element is already UNC — this prevents accidental UNC path creation.
    //
    // Example:
    //   Join({"", "src", "main.cpp"}) => "src/main.cpp"    (Unix)
    //   Join({"C:", "foo", "bar"})    => "C:foo\\bar"       (Windows)
    //   Join({""})                    => ""
    std::string GoFilepath::Join(const std::vector<std::string>& elem) const
    {
        for (size_t i = 0; i < elem.size(); i++) {
            if (!elem[i].empty()) {
                if (is_windows) {
                    return joinNonEmpty(*this, std::vector<std::string>(elem.begin() + ptrdiff_t(i), elem.end()));
                }
                return Clean(JoinWithSeparator(path_separator, elem, i));
            }
        }
        return "";
    }

    // Computes the relative path from "basepath" to "targpath" such that
    // Join(basepath, Rel(basepath, targpath)) is lexically equivalent to
    // targpath.  On success the result is always relative to "basepath",
    // even when the two paths share no common ancestor.
    //
    // Returns std::nullopt when the paths cannot be made relative —
    // typically because they live on different volumes (e.g. C: vs D:) or
    // because one is absolute and the other is not.
    //
    // Example:
    //   Rel("/a/b", "/a/b/c/d") => "c/d"
    //   Rel("/a/b", "/a/b")     => "."
    //   Rel("/a/b", "/x/y")     => "../../x/y"
    //   Rel("C:\\a", "D:\\b")   => std::nullopt  (different volumes)
    std::optional<std::string> GoFilepath::Rel(std::string_view basepath, std::string_view targpath) const
    {
        std::string base_vol = VolumeName(basepath);
        std::string targ_vol = VolumeName(targpath);
        std::string base     = Clean(basepath);
        std::string targ     = Clean(targpath);
        if (SameWord(targ, base)) {
            return ".";
        }
        base = base.substr(base_vol.size());
        targ = targ.substr(targ_vol.size());
        if (base == ".") {
            base.clear();
        }
        // Can't use IsAbs - `\a` and `a` are both relative in Windows.
        bool base_slashed = !base.empty() && base[0] == path_separator;
        bool targ_slashed = !targ.empty() && targ[0] == path_separator;
        if (base_slashed != targ_slashed || !SameWord(base_vol, targ_vol)) {
            return std::nullopt;
        }
        // Position base[b0:bi] and targ[t0:ti] at the first differing elements.
        size_t bl = base.size();
        size_t tl = targ.size();
        size_t b0 = 0, bi = 0, t0 = 0, ti = 0;
        while (true) {
            while (bi < bl && base[bi] != path_separator) {
                bi++;
            }
            while (ti < tl && targ[ti] != path_separator) {
                ti++;
            }
            if (!SameWord(targ.substr(t0, ti - t0), base.substr(b0, bi - b0))) {
                break;
            }
            if (bi < bl) {
                bi++;
            }
            if (ti < tl) {
                ti++;
            }
            b0 = bi;
            t0 = ti;
        }
        if (base.substr(b0, bi - b0) == "..") {
            return std::nullopt;
        }
        if (b0 != bl) {
            // Base elements left. Must go up before going down.
            size_t seps = size_t(std::count(base.begin() + ptrdiff_t(b0), base.end(), path_separator));
            std::string buf = "..";
            for (size_t i = 0; i < seps; i++) {
                buf += path_separator;
                buf += "..";
            }
            if (t0 != tl) {
                buf += path_separator;
                buf.append(targ, t0, tl - t0);
            }
            return buf;
        }
        return targ.substr(t0);
    }

    // Returns true when "a" and "b" are the same path under the platform's
    // case rules.  On Unix this is an exact byte-for-byte comparison.  On
    // Windows it is a case-insensitive ASCII comparison.
    //
    // Example:
    //   SameWord("README.md", "readme.md") => true   (Windows mode)
    //   SameWord("README.md", "readme.md") => false  (Unix mode)
    //   SameWord("/a/b", "/a/b")           => true   (both modes)
    bool GoFilepath::SameWord(std::string_view a, std::string_view b) const
    {
        if (!is_windows) {
            return a == b;
        }
        return helpers::EqualFoldASCII(a, b);
    }

    // Replaces every forward slash ('/') in "path" with the platform path
    // separator.  On Unix this is a no-op; on Windows forward slashes are
    // converted to backslashes.  Multiple consecutive slashes are each
    // replaced individually.
    //
    // Example:
    //   FromSlash("src/main.cpp")           => "src\\main.cpp"  (Windows)
    //   FromSlash("src/main.cpp")           => "src/main.cpp"   (Unix)
    std::string GoFilepath::FromSlash(std::string_view path) const
    {
        if (!is_windows) {
            return std::string(path);
        }
        std::string result(path);
        std::replace(result.begin(), result.end(), '/', '\\');
        return result;
    }
}
