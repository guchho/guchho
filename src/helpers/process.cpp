#include "guchho/helpers.hpp"

#include <cstdlib>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace guchho::helpers {

#if !defined(_WIN32)

    namespace {

        // Reads everything the child writes to one pipe until the write end
        // goes away, appending each chunk straight into "output". The buffer
        // is reused across the whole loop, so this stays cheap no matter how
        // much text the child produces. The read call blocks until data is
        // available, and the loop ends the moment it reports EOF or an error,
        // which both mean the child has closed the pipe. Everything the child
        // wrote is delivered before EOF, so no output is ever lost.
        //
        // Input: a pipe read descriptor and an empty string to fill.
        // Output: the string now holds all of the child's bytes.
        void DrainPipe(int fd, std::string& output)
        {
            char buffer[8192];
            while (true) {
                ssize_t count = read(fd, buffer, sizeof(buffer));
                if (count <= 0) {
                    break;
                }
                output.append(buffer, static_cast<size_t>(count));
            }
        }

    } // namespace

#endif

    // Spawns an external program directly, without going through a shell or
    // interpreter, and does not return until it has exited. The arguments in
    // "argv" are passed to the child verbatim — argv[0] names the executable
    // to launch, which is located through the platform's normal search rules —
    // so file names, flags, and values with spaces never have to fight shell
    // quoting rules. "cwd" sets the child's working directory; when it is
    // empty, the child inherits guchho's own current directory. Standard out
    // and standard error are captured into dedicated pipes that are drained on
    // background threads while the main thread blocks on the child, so a child
    // that produces a lot of output cannot fill its pipe buffer and stall the
    // whole call. An empty argv (or an argv whose first entry is empty) is
    // rejected up front, and a failure to even start the child leaves the
    // started flag off and the exit code at its -1 default.
    //
    // Input: {"git", "rev-parse", "--abbrev-ref", "HEAD"}, "" ->
    // Output: ProcessResult{ started = true, exit_code = 0,
    //          stdout_data = "main\n", stderr_data = "" }.
    ProcessResult RunProcess(const std::vector<std::string>& argv, const std::string& cwd)
    {
        ProcessResult result;
        if (argv.empty() || argv[0].empty()) {
            return result;
        }

#if defined(_WIN32)

        // Turns one argument into a slice of a Windows command line. Windows
        // tokenizes command lines on spaces and tabs, so any argument holding
        // one of those must be wrapped in double quotes. Inside the quotes a
        // run of backslashes that immediately precedes the closing quote or
        // the end of the argument would otherwise be misread, so such runs are
        // doubled; backslashes elsewhere are copied through untouched. This is
        // the escape rule the child's own parser applies in reverse, so the
        // round trip stays lossless. Arguments free of spaces, tabs, and
        // quotes go straight through unquoted, which keeps the assembled
        // command line readable in the common case.
        //
        // Input: "C:\Program Files\Guchho\guchho run" ->
        // Output: "\"C:\\Program Files\\Guchho\\guchho run\"".
        auto quote = [](const std::string& arg) {
            if (arg.find_first_of(" \t\"") == std::string::npos) {
                return arg;
            }
            std::string out = "\"";
            size_t       i = 0;
            while (i < arg.size()) {
                size_t backslashes = 0;
                while (i < arg.size() && arg[i] == '\\') {
                    ++backslashes;
                    ++i;
                }
                if (i == arg.size()) {
                    out.append(backslashes * 2, '\\');
                    break;
                } else if (arg[i] == '\"') {
                    out.append(backslashes * 2 + 1, '\\');
                    out += '\"';
                } else {
                    out.append(backslashes, '\\');
                    out += arg[i];
                }
                ++i;
            }
            out += '\"';
            return out;
        };

        // Reassemble the individually quoted arguments into one command line,
        // separated by single spaces. This mirrors how CreateProcessW will
        // hand it to the child, where each quoted fragment becomes one
        // argument again when the child parses it back out.
        std::string command_line;
        for (size_t i = 0; i < argv.size(); ++i) {
            if (i > 0) command_line += ' ';
            command_line += quote(argv[i]);
        }

        HANDLE stdout_read  = nullptr;
        HANDLE stdout_write = nullptr;
        HANDLE stderr_read  = nullptr;
        HANDLE stderr_write = nullptr;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle       = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        // Create the two pipes that will carry the child's standard output and
        // standard error back to guchho. The security attributes allow the
        // write ends to be inherited by the child. Because each pipe creation
        // is checked separately, the failure paths below must release every
        // handle that already exists before bailing out; the early returns
        // walk that cleanup in order so nothing leaks even when only the
        // second pipe fails.
        if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
            return result;
        }
        if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            return result;
        }
        // Mark both read ends as non-inheritable. If they were inherited, the
        // child would keep its own copies of them open, and the read ends
        // would never see EOF after the child exits — the drain threads would
        // block forever waiting for output that can never arrive.
        SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

        // Point the child's standard output and standard error at the write
        // ends created above. Standard input is inherited from guchho so an
        // interactive child can still receive input. Inherited handles are
        // wired up through STARTUPINFOW because that is the only mechanism
        // CreateProcessW will honor for passing them along.
        STARTUPINFOW startup{};
        startup.cb              = sizeof(STARTUPINFOW);
        startup.dwFlags         = STARTF_USESTDHANDLES;
        startup.hStdOutput      = stdout_write;
        startup.hStdError       = stderr_write;
        startup.hStdInput       = GetStdHandle(STD_INPUT_HANDLE);

        // Convert the assembled command line and the requested working
        // directory from narrow strings into wide UTF-16 buffers for
        // CreateProcessW. An empty cwd stays null below so the child simply
        // inherits guchho's current directory.
        std::vector<wchar_t> command_line_wide(command_line.size() + 1);
        std::mbstowcs(command_line_wide.data(), command_line.c_str(), command_line.size());

        std::wstring cwd_wide;
        if (!cwd.empty()) {
            cwd_wide.resize(cwd.size() + 1);
            std::mbstowcs(cwd_wide.data(), cwd.c_str(), cwd.size());
        }

        // Launch the child with all of the decisions above locked in: the
        // standard handles come from STARTUPINFOW, no console window is
        // created for the child, and the executable is resolved through the
        // usual search rules. The process handle is kept so the caller can
        // wait for the child and read back its exit code.
        PROCESS_INFORMATION process{};
        BOOL ok = CreateProcessW(
            nullptr,
            command_line_wide.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            cwd.empty() ? nullptr : cwd_wide.c_str(),
            &startup,
            &process);

        // The write ends now belong to the child, so our copies are closed
        // immediately — regardless of whether the launch succeeded. Doing it
        // here is what lets the read ends hit EOF when the child eventually
        // dies, even if the child neglects to close them itself.
        CloseHandle(stdout_write);
        CloseHandle(stderr_write);

        // A failed launch still shows up as an error exit and an unstarted
        // result rather than a hang, so the read ends are released here too.
        if (!ok) {
            CloseHandle(stdout_read);
            CloseHandle(stderr_read);
            return result;
        }

        result.started = true;

        // Drain both pipes on separate threads while the main thread blocks
        // on the child. Without the threads, a child that fills a pipe buffer
        // would stall, never exit, and deadlock the wait below. The try/catch
        // makes a failed read harmless: closing the write end is what
        // terminates the wait, and that is unaffected by any read hiccup.
        std::thread stdout_thread(
            [&]() {
                try {
                    char  buffer[8192];
                    DWORD count = 0;
                    while (ReadFile(stdout_read, buffer, sizeof(buffer), &count, nullptr) && count > 0) {
                        result.stdout_data.append(buffer, count);
                    }
                } catch (...) {
                }
            });
        std::thread stderr_thread(
            [&]() {
                try {
                    char  buffer[8192];
                    DWORD count = 0;
                    while (ReadFile(stderr_read, buffer, sizeof(buffer), &count, nullptr) && count > 0) {
                        result.stderr_data.append(buffer, count);
                    }
                } catch (...) {
                }
            });

        // Block until the child process has exited, then pull its exit code.
        // Reading the code can still fail if the child was killed in a way
        // Windows cannot report, in which case the -1 default survives.
        WaitForSingleObject(process.hProcess, INFINITE);

        DWORD exit_code = 0;
        if (GetExitCodeProcess(process.hProcess, &exit_code)) {
            result.exit_code = static_cast<int>(exit_code);
        }

        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);

        // The child is gone and the pipes are about to report EOF. Joining
        // both threads guarantees every byte the child wrote is sitting in
        // these strings before the read ends are released, so the result is
        // complete when the caller receives it.
        stdout_thread.join();
        stderr_thread.join();
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);

#else

        // Create the two pipes the child will write to. Both are initialized
        // to -1 so the cleanup code can tell which ends were never created:
        // when the second pipe fails after the first succeeded, the first
        // pipe's ends still have to be closed before giving up.
        int stdout_pipe[2] = {-1, -1};
        int stderr_pipe[2] = {-1, -1};
        if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            if (stdout_pipe[0] != -1) {
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
            }
            if (stderr_pipe[0] != -1) {
                close(stderr_pipe[0]);
                close(stderr_pipe[1]);
            }
            return result;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child process. The write ends are spliced over the standard
            // descriptor numbers so everything the child prints flows into
            // the pipes, and the read ends are closed because the child must
            // never hold them. The working directory is switched next, and
            // then execvp replaces this process wholesale with the requested
            // program, searching PATH just as a human typing the command
            // would. A failed chdir or an unlaunchable program still ends
            // with a distinct exit code the parent can recognize: 126 when
            // the directory could not be reached, 127 when the executable
            // could not be found.
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            if (!cwd.empty()) {
                if (chdir(cwd.c_str()) != 0) {
                    _exit(126);
                }
            }
            std::vector<char*> argvp;
            argvp.reserve(argv.size() + 1);
            for (const std::string& arg : argv) {
                argvp.push_back(const_cast<char*>(arg.c_str()));
            }
            argvp.push_back(nullptr);
            execvp(argvp[0], argvp.data());
            _exit(127);
        }
        if (pid < 0) {
            // The fork never produced a child, so there is nothing to wait
            // on; the pipes are torn down and the call ends in failure.
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            return result;
        }

        result.started = true;

        // The child owns the write ends now, so our copies are closed at
        // once. This is what guarantees the read ends see EOF the moment the
        // child exits.
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Drain both pipes on separate threads while the main thread waits on
        // the child. The threads keep a chatty child from filling a pipe
        // buffer and stalling until guchho reads it, a stall that would
        // otherwise turn the wait below into a deadlock.
        std::thread stdout_thread([&]() { DrainPipe(stdout_pipe[0], result.stdout_data); });
        std::thread stderr_thread([&]() { DrainPipe(stderr_pipe[0], result.stderr_data); });

        // Block until the child dies, then translate the raw wait status into
        // a plain exit code. Only children that exited on their own
        // contribute a real code; anything else leaves the -1 default in
        // place.
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        }

        // The child is gone, so the pipes will report EOF once the remaining
        // data is read. Joining first keeps the result strings complete and
        // makes it safe to close the read ends afterwards.
        stdout_thread.join();
        stderr_thread.join();
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

#endif

        return result;
    }

} // namespace guchho::helpers