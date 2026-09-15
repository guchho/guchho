#include "guchho/logger.hpp"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <windows.h>
    #include <io.h>

namespace guchho::logger {

    // Converts a C runtime file descriptor (0=stdin, 1=stdout, 2=stderr) to
    // a native Win32 HANDLE. The handle is obtained via _get_osfhandle, which
    // returns INVALID_HANDLE_VALUE if the descriptor is not associated with a
    // valid file. This is used by both GetTerminalInfo and WriteStringWithColor.
    static HANDLE HandleFromFD(int file_descriptor)
    {
        return reinterpret_cast<HANDLE>(_get_osfhandle(file_descriptor));
    }

    // Probes the Win32 console attached to `file_descriptor` and returns a
    // TerminalInfo describing its capabilities. The detection proceeds in
    // three steps:
    //
    //   1. GetConsoleMode() is called to check whether the handle points to
    //      a console. If it succeeds, is_tty is set to true.
    //   2. GetConsoleScreenBufferInfo() queries the console buffer dimensions
    //      to populate width and height. The trailing column/row is subtracted
    //      because the buffer size includes the scrollback region.
    //   3. use_color_escapes is set to true only when the handle is a TTY and
    //      the NO_COLOR environment variable is not set (see <https://no-color.org/>).
    //
    // If the handle is not a console (e.g. output redirected to a pipe or
    // file), all fields remain at their zero-initialized defaults and no
    // error is raised.
    //
    //   GetTerminalInfo(1)  // stdout
    //   -> TerminalInfo{is_tty: true, use_color_escapes: true, width: 120, height: 30}
    //
    //   GetTerminalInfo(1)  // stdout redirected to a file
    //   -> TerminalInfo{is_tty: false, use_color_escapes: false, width: 0, height: 0}
    TerminalInfo GetTerminalInfo(int file_descriptor)
    {
        TerminalInfo info{};

        HANDLE handle = HandleFromFD(file_descriptor);

        if (handle != INVALID_HANDLE_VALUE) {
            DWORD unused = 0;
            info.is_tty = (GetConsoleMode(handle, &unused) != FALSE);

            CONSOLE_SCREEN_BUFFER_INFO csbi{};
            if (GetConsoleScreenBufferInfo(handle, &csbi)) {
                info.width = static_cast<int>(csbi.dwSize.X) - 1;
                info.height = static_cast<int>(csbi.dwSize.Y) - 1;
            }
        }

        info.use_color_escapes =
            info.is_tty && !HasEnvironmentVariableValue("NO_COLOR");

        return info;
    }

} 

#elif defined(__APPLE__)
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <termios.h>

namespace guchho::logger {
    // Probes the terminal attached to `file_descriptor` on macOS. Uses
    // ioctl(TIOCGETA) to test whether the descriptor refers to a terminal;
    // if it does, is_tty is set and the window size is obtained via
    // ioctl(TIOCGWINSZ). Color escape support requires both TTY status
    // and the absence of the NO_COLOR environment variable.
    //
    // On failure (e.g. pipe or file descriptor), all fields stay at zero.
    //
    //   GetTerminalInfo(1)  // stdout on macOS
    //   -> TerminalInfo{is_tty: true, use_color_escapes: true, width: 80, height: 24}
    TerminalInfo GetTerminalInfo(int file_descriptor) {
        TerminalInfo info{};

        struct termios t;
        if (ioctl(file_descriptor, TIOCGETA, &t) == 0) {
            info.is_tty = true;

            info.use_color_escapes =
                info.is_tty && !HasEnvironmentVariableValue("NO_COLOR");

            struct winsize ws;
            if (ioctl(file_descriptor, TIOCGWINSZ, &ws) == 0) {
                info.width  = ws.ws_col;
                info.height = ws.ws_row;
            }
        }

        return info;
    }
}

#elif defined(__linux__)
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <termios.h>

namespace guchho::logger {

    // Probes the terminal attached to `file_descriptor` on Linux. Uses
    // ioctl(TCGETS) to test whether the descriptor refers to a terminal;
    // if it does, is_tty is set and the window size is obtained via
    // ioctl(TIOCGWINSZ). Color escape support requires both TTY status
    // and the absence of the NO_COLOR environment variable.
    //
    // The Linux variant uses TCGETS (not TIOCGETA as on macOS) because
    // that is the correct ioctl for Linux terminal interrogation.
    //
    //   GetTerminalInfo(2)  // stderr on Linux
    //   -> TerminalInfo{is_tty: true, use_color_escapes: true, width: 120, height: 40}
    TerminalInfo GetTerminalInfo(int file_descriptor) {
        TerminalInfo info{};

        struct termios t;
        if (ioctl(file_descriptor, TCGETS, &t) == 0) {
            info.is_tty = true;
            info.use_color_escapes =
                    info.is_tty && !HasEnvironmentVariableValue("NO_COLOR");

            struct winsize ws;
            if (ioctl(file_descriptor, TIOCGWINSZ, &ws) == 0) {
                info.width  = ws.ws_col;
                info.height = ws.ws_row;
            }
        }

        return info;
    }

}

#else
    #include <unistd.h>

namespace guchho::logger {

    // Fallback for unsupported platforms. Returns a default-constructed
    // TerminalInfo with all fields zeroed, meaning no TTY detection and
    // no color support. This ensures the logger degrades gracefully
    // rather than failing to compile on niche platforms.
    TerminalInfo GetTerminalInfo(int /*fd*/) {
        return TerminalInfo{};
    }

}
#endif

#ifdef _WIN32
namespace guchho::logger {

    namespace win32 {

        // Win32 console color attribute constants. These correspond to the
        // FOREGROUND_* and BACKGROUND_* flags used by SetConsoleTextAttribute().
        // Each constant is a single bit in the 16-bit attribute word.
        constexpr uint8_t kForegroundBlue = 1 << 0;
        constexpr uint8_t kForegroundGreen = 1 << 1;
        constexpr uint8_t kForegroundRed = 1 << 2;
        constexpr uint8_t kForegroundIntensity = 1 << 3;
        constexpr uint8_t kBackgroundBlue = 1 << 4;
        constexpr uint8_t kBackgroundGreen = 1 << 5;
        constexpr uint8_t kBackgroundRed = 1 << 6;

        // Maps an ANSI escape sequence to the corresponding Win32 console
        // attribute word. Each entry pairs the raw escape string (e.g.
        // "\033[31m") with the foreground/background bit combination that
        // SetConsoleTextAttribute() expects.
        struct EscapeEntry {
            std::string_view escape;
            uint16_t attributes;
        };

        // Complete mapping of every ANSI escape sequence used by Guchho's
        // terminal output to its Win32 console attribute equivalent. The
        // table is scanned linearly on each escape, but the total number of
        // entries is small (21) so the overhead is negligible.
        static const EscapeEntry kEscapeMap[] = {
            {"\033[0m",    kForegroundRed | kForegroundGreen | kForegroundBlue},
            {"\033[37m",   kForegroundRed | kForegroundGreen | kForegroundBlue},
            {"\033[1m",    kForegroundRed | kForegroundGreen | kForegroundBlue | kForegroundIntensity},
            {"\033[4m",    kForegroundRed | kForegroundGreen | kForegroundBlue},

            {"\033[31m",   kForegroundRed},
            {"\033[32m",   kForegroundGreen},
            {"\033[34m",   kForegroundBlue},

            {"\033[36m",   kForegroundGreen | kForegroundBlue},
            {"\033[35m",   kForegroundRed | kForegroundBlue},
            {"\033[33m",   kForegroundRed | kForegroundGreen},

            {"\033[41;31m",  kForegroundRed | kBackgroundRed},
            {"\033[41;97m",  kForegroundRed | kForegroundGreen | kForegroundBlue | kBackgroundRed},
            {"\033[42;32m",  kForegroundGreen | kBackgroundGreen},
            {"\033[42;97m",  kForegroundRed | kForegroundGreen | kForegroundBlue | kBackgroundGreen},
            {"\033[44;34m",  kForegroundBlue | kBackgroundBlue},
            {"\033[44;97m",  kForegroundRed | kForegroundGreen | kForegroundBlue | kBackgroundBlue},

            {"\033[46;36m",  kForegroundGreen | kForegroundBlue | kBackgroundGreen | kBackgroundBlue},
            {"\033[46;30m",  kBackgroundGreen | kBackgroundBlue},
            {"\033[45;35m",  kForegroundRed | kForegroundBlue | kBackgroundRed | kBackgroundBlue},
            {"\033[45;30m",  kBackgroundRed | kBackgroundBlue},
            {"\033[43;33m",  kForegroundRed | kForegroundGreen | kBackgroundRed | kBackgroundGreen},
            {"\033[43;30m",  kBackgroundRed | kBackgroundGreen},
        };

        // Looks up an ANSI escape sequence in kEscapeMap and returns the
        // corresponding Win32 console attribute. Returns 0 if the escape is
        // not recognized. The reset sequence ("\033[0m") maps to the default
        // white-on-black attributes and is handled as a special case by the
        // caller.
        //
        //   AttributeForEscape("\033[31m")  ->  kForegroundRed (4)
        //   AttributeForEscape("\033[99m")  ->  0  (unknown escape)
        uint16_t AttributeForEscape(std::string_view escape) {
            for (const auto& entry : kEscapeMap) {
                if (entry.escape == escape) {
                    return entry.attributes;
                }
            }
            return 0;
        }

    }

    // Writes `text` to `file_descriptor`, interpreting embedded ANSI escape
    // sequences and converting them to Win32 console attribute changes via
    // SetConsoleTextAttribute(). Non-escape text is written directly with
    // WriteFile().
    //
    // The parsing loop scans for '\033' characters, extracts each escape up
    // to the terminating 'm', looks up the corresponding Win32 attribute,
    // and writes any preceding plain text before applying the color change.
    // Unrecognized escapes (or escapes that don't end with 'm' within 8
    // bytes) are skipped and the character after '\033' is tried next.
    //
    //   WriteStringWithColor(1, "\033[31merror:\033[0m done")
    //   // writes "error:" in red, then "done" in default color
    void WriteStringWithColor(int file_descriptor, const std::string& text) 
    {
        HANDLE handle = HandleFromFD(file_descriptor);
        std::string_view remaining = text;

        while (true) {
            size_t pos = remaining.find('\033');
            if (pos == std::string_view::npos) {
                break;
            }

            std::string_view window = remaining.substr(pos);
            if (window.size() > 8) {
                window = window.substr(0, 8);
            }
            size_t m = window.find('m');
            if (m == std::string_view::npos) {
                remaining = remaining.substr(pos + 1);
                continue;
            }
            m += pos + 1;

            std::string_view escape = remaining.substr(pos, m - pos);
            uint16_t attributes = win32::AttributeForEscape(escape);
            if (attributes == 0 && escape != "\033[0m") {
                remaining = remaining.substr(pos + 1);
                continue;
            }

            std::string_view before = remaining.substr(0, pos);
            if (!before.empty()) {
                DWORD written = 0;
                WriteFile(handle, before.data(), static_cast<DWORD>(before.size()), &written, nullptr);
            }

            SetConsoleTextAttribute(handle, attributes);
            remaining = remaining.substr(m);
        }

        if (!remaining.empty()) {
            DWORD written = 0;
            WriteFile(handle, remaining.data(), static_cast<DWORD>(remaining.size()), &written, nullptr);
        }
    }

}
#else
namespace guchho::logger {

    // On non-Win32 platforms (macOS, Linux, BSDs), the terminal natively
    // understands ANSI escape sequences, so the text is written directly
    // with write() without any translation. This is the fast path used by
    // the vast majority of Guchho installations.
    //
    //   WriteStringWithColor(1, "\033[32msuccess\033[0m")
    //   // writes green "success" followed by a reset, interpreted natively
    void WriteStringWithColor(int file_descriptor, const std::string& text) {
        if (!text.empty()) {
            write(file_descriptor, text.data(), text.size());
        }
    }

}

#endif

namespace guchho::logger {

    // Checks whether the named environment variable exists and has a
    // non-empty value. Returns false if the variable is unset, empty, or
    // if the platform-specific lookup fails.
    //
    // On Windows, _dupenv_s is used because std::getenv is deprecated in
    // the Microsoft CRT. The allocated buffer is freed immediately after
    // the check.
    //
    // On non-Win32 platforms, std::getenv is used directly.
    //
    //   setenv("NO_COLOR", "1", 1);
    //   HasEnvironmentVariableValue("NO_COLOR")  ->  true
    //
    //   HasEnvironmentVariableValue("NONEXISTENT_VAR")  ->  false
    bool HasEnvironmentVariableValue(std::string_view name)
    {
    #ifdef _WIN32
        std::string key(name);

        char* value = nullptr;
        size_t size = 0;

        if (_dupenv_s(&value, &size, key.c_str()) != 0 || value == nullptr) {
            return false;
        }

        bool result = value[0] != '\0';
        free(value);

        return result;

    #else
        std::string key(name);
        const char* value = std::getenv(key.c_str());

        return value != nullptr && value[0] != '\0';
    #endif
    }


    // Heuristic detection of the Windows Command Prompt (cmd.exe) as
    // opposed to Windows Terminal (wt.exe) or other modern terminals.
    //
    // The check works by looking for the WT_SESSION environment variable,
    // which Windows Terminal sets automatically. If WT_SESSION is absent,
    // the function assumes we are running in the legacy Command Prompt
    // (or another terminal that does not set this variable).
    //
    // The result is cached in a static local so the check only runs once
    // per process. This is safe because the terminal type cannot change
    // during execution.
    //
    // On non-Windows platforms, this always returns false.
    //
    //   // Inside Windows Terminal:
    //   IsProbablyWindowsCommandPrompt()  ->  false
    //
    //   // Inside cmd.exe:
    //   IsProbablyWindowsCommandPrompt()  ->  true
    bool IsProbablyWindowsCommandPrompt()
    {
    #ifdef _WIN32
        static const bool result = !HasEnvironmentVariableValue("WT_SESSION");
        return result;
    #else
        return false;
    #endif
    }

}
