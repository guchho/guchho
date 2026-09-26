// The executable's whole job is to turn what the operating system handed over
// into what the command line expects, and then get out of the way. Everything a
// person can type is understood by src/cli: which command it names, what the
// flags mean, when to print help, and what the exit code should be. Nothing
// about a command lives here, so a new command is a new file in src/cli and
// never a change to this one.
//
// The one job that does belong here is the form the arguments arrive in, and
// that differs by platform. Guchho is UTF-8 everywhere below this file: the
// file system layer turns a UTF-8 path into a real path
// (filesystem::PathFromUTF8), the JavaScript layer holds UTF-8 source, and the
// error messages are written in UTF-8. On POSIX the process arguments are
// already those bytes, so reading them is enough. On Windows a narrow argv is
// whatever the active code page happened to be, and the operating system
// replaces the characters it cannot express before this program ever runs, so
// no care taken afterwards can bring a path like "café/app.js" back. The wide
// entry point is therefore used on Windows, where the arguments arrive as the
// shell wrote them, and each one is converted to UTF-8 exactly once, here.

#include "guchho/cli.hpp"
#include "guchho/helpers.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

#ifdef _WIN32
namespace {

// Reads the process arguments the way the shell wrote them and returns them in
// the form the command line documents: UTF-8, in the order they were typed, and
// without the executable name, which is not something any command can act on.
// The block that the shell hands over is ours to give back, so the copy is
// released before returning rather than left for the process to clean up.
//
// input:  the arguments of the running process, as UTF-16 on Windows
// output: the same arguments as UTF-8, minus the executable name
std::vector<std::string> argsFromCommandLine() {
    std::vector<std::string> args;
    int                      argc  = 0;
    wchar_t**                wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wargv != nullptr) {
        // Skip the executable name at index 0; the first argument a person
        // typed is at index 1.
        args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view wide(wargv[i]);
            args.push_back(guchho::helpers::UTF16ToString(
                std::span<const char16_t>(reinterpret_cast<const char16_t*>(wide.data()),
                                          wide.size())));
        }
        LocalFree(wargv);
    }
    return args;
}

}  // namespace

int wmain(int /*argc*/, wchar_t** /*argv*/) {
    return guchho::cli::Run(argsFromCommandLine());
}
#else
int main(int argc, char* argv[]) {
    return guchho::cli::Run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
