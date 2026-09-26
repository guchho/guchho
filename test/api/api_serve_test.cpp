// Tests for Serve() in src/api/api_serve.cpp: the development server that
// answers HTTP requests from a build, a served directory and a fallback page.
//
// Everything below drives a real server over a real loopback socket, because
// that is the only way to reach the parts worth testing: the host allow-list,
// the media type a path comes back with, a range, a redirect, the headers a
// browser looks at, and the live-reload stream. None of that exists below the
// socket. What is served is nevertheless all in memory — the filesystem is a
// mock and the build is a function that counts its own calls — so no test
// writes to the disk and none of them needs a real bundler.
//
// Two choices keep the run predictable. The port is one the test picked itself
// rather than the server's "0", because a server asked for port 0 tries the
// familiar development ports first and a test must not take a port a developer
// may be serving on; and every read from a socket carries a deadline, so a
// server that answered nothing fails the test instead of hanging the run. Every
// server binds the loopback interface, which keeps the host allow-list to the
// one address a test can put in its "Host" header.

#include "test/helpers/filesystem_test.hpp"

#include "test/guchho_test.hpp"
#include "guchho/api.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace api        = guchho::api;
namespace filesystem = guchho::filesystem;
namespace helpers    = guchho::helpers;
namespace logger     = guchho::logger;

namespace {

using namespace std::chrono_literals;

// ===========================================================================
// A minimal HTTP client
// ===========================================================================

#ifdef _WIN32
using SockHandle   = SOCKET;
constexpr SockHandle kInvalidSock = INVALID_SOCKET;
#else
using SockHandle   = int;
constexpr SockHandle kInvalidSock = -1;
#endif

// Winsock has to be started once per process before any socket is created.
// The static means the first client in the run pays for it and the rest find
// it already done; nothing here ever shuts it down, which is deliberate, since
// a cleanup at exit would race the harness's own teardown.
void StartupSockets() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
#endif
}

void CloseSocket(SockHandle sock) {
#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif
}

// Gives "sock" a receive deadline, so a read that would otherwise wait for ever
// comes back as a short read instead. The value is per-socket rather than
// global because the two tests that hold a connection open read in a loop.
void SetReadDeadline(SockHandle sock, std::chrono::milliseconds timeout) {
#ifdef _WIN32
    DWORD millis = static_cast<DWORD>(timeout.count());
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&millis),
                 static_cast<int>(sizeof(millis)));
#else
    timeval tv;
    tv.tv_sec  = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// An owned socket, closed when it goes out of scope. Owning it is what lets the
// live-reload test close its end of a stream connection without calling
// anything the server has to hear about.
class Socket {
public:
    Socket() = default;
    explicit Socket(SockHandle handle)
        : handle_(handle) {}

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept
        : handle_(other.handle_) {
        other.handle_ = kInvalidSock;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            Close();
            handle_       = other.handle_;
            other.handle_ = kInvalidSock;
        }
        return *this;
    }

    ~Socket() {
        Close();
    }

    void Close() {
        if (handle_ != kInvalidSock) {
            CloseSocket(handle_);
            handle_ = kInvalidSock;
        }
    }

    bool valid() const {
        return handle_ != kInvalidSock;
    }

    SockHandle get() const {
        return handle_;
    }

private:
    SockHandle handle_{kInvalidSock};
};

// Opens a connection to the loopback interface on "port". A port nothing is
// listening on is refused, so an empty socket is how a test learns that a
// server has stopped.
Socket ConnectLoopback(uint16_t port) {
    StartupSockets();

    Socket sock(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.valid()) {
        return sock;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        sock.Close();
    }
    return sock;
}

// Sends a whole request. A short send is treated as a failure rather than
// retried, because the requests here are small enough to fit in the socket
// buffer and anything else means the connection is already gone.
bool SendAll(SockHandle sock, const std::string& request) {
    size_t sent = 0;
    while (sent < request.size()) {
        const int n = static_cast<int>(::send(sock, request.data() + sent,
                                              static_cast<int>(request.size() - sent), 0));
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// One answered request: the status, the headers, and whatever followed them.
struct Response {
    bool                                            connected = false;
    int                                             status     = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string                                     body;

    // Header lookup, case-insensitively: the server writes the canonical
    // spelling, but a test that asked for a header in another case has to find
    // it, and a client library is not what is being tested here.
    std::string Get(const std::string& name, const std::string& fallback = "") const {
        const std::string wanted = helpers::ToLowerASCII(name);
        for (const auto& [header_name, value] : headers) {
            if (helpers::ToLowerASCII(header_name) == wanted) {
                return value;
            }
        }
        return fallback;
    }

    bool Has(const std::string& name) const {
        const std::string wanted = helpers::ToLowerASCII(name);
        return std::any_of(headers.begin(), headers.end(), [&](const auto& header) {
            return helpers::ToLowerASCII(header.first) == wanted;
        });
    }
};

// Trims the spaces and the tab a header value is allowed to be written with.
std::string TrimHeaderValue(std::string_view value) {
    const size_t begin = value.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(" \t");
    return std::string(value.substr(begin, end - begin + 1));
}

// Splits an answered response into its parts. The server writes a
// "Content-Length" on everything and closes the connection when it is done, so
// reading to the end of the stream is the whole of the framing a client here
// needs.
Response ParseResponse(const std::string& raw) {
    Response response;
    response.connected = true;

    const size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string::npos) {
        return response;
    }

    std::istringstream head(raw.substr(0, head_end));
    std::string        line;
    if (!std::getline(head, line)) {
        return response;
    }
    // "HTTP/1.1 404 Not Found" — the version is fixed, so the code is the
    // second word and anything after it is the reason phrase.
    std::istringstream status_line(line.substr(std::string("HTTP/1.1 ").size()));
    status_line >> response.status;

    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string value = TrimHeaderValue(line.substr(colon + 1));
        response.headers.emplace_back(line.substr(0, colon), std::move(value));
    }

    response.body = raw.substr(head_end + 4);
    return response;
}

// Reads until the connection is closed, giving up after "timeout" so a server
// that never answers produces a failed assertion rather than a hung test.
std::string ReadToEnd(SockHandle sock, std::chrono::milliseconds timeout) {
    SetReadDeadline(sock, timeout);

    std::string received;
    char        buffer[4096];
    for (;;) {
        const int n = static_cast<int>(::recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0));
        if (n <= 0) {
            break;
        }
        received.append(buffer, static_cast<size_t>(n));
    }
    return received;
}

// Reads until "needle" has arrived or "timeout" has passed, and reports whether
// it arrived. Used for the one response that has no end: the live-reload stream
// keeps its connection open for as long as the client wants it.
bool ReadUntil(SockHandle                 sock,
               const std::string&         needle,
               std::string&               received,
               std::chrono::milliseconds timeout) {
    SetReadDeadline(sock, std::min(timeout, 200ms));

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + timeout;
    char buffer[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        if (received.find(needle) != std::string::npos) {
            return true;
        }
        const int n = static_cast<int>(::recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0));
        if (n > 0) {
            received.append(buffer, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        }
    }
    return received.find(needle) != std::string::npos;
}

// Waits for anything at all to arrive and reports whether it did. This is how
// "nothing was pushed" is asserted: the only honest way to say a stream stayed
// quiet is to wait past the point a frame would have come and then check that
// the socket is still silent.
bool WaitForData(SockHandle sock, std::chrono::milliseconds timeout) {
    SetReadDeadline(sock, timeout);

    char buffer[4096];
    return ::recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0) > 0;
}

// Asks "port" for "method path". The Host header is what the server checks
// against its allow-list, so it is written from the address the test connected
// to; a test that wants a different one passes it.
Response Request(uint16_t              port,
                 const std::string&     method,
                 const std::string&     path,
                 const std::vector<std::string>& headers = {},
                 const std::string&     host          = "") {
    Socket sock = ConnectLoopback(port);
    if (!sock.valid()) {
        return {};
    }

    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + (host.empty() ? "127.0.0.1:" + std::to_string(port) : host) + "\r\n";
    request += "Connection: close\r\n";
    for (const std::string& header : headers) {
        request += header + "\r\n";
    }
    request += "\r\n";

    if (!SendAll(sock.get(), request)) {
        return {};
    }
    return ParseResponse(ReadToEnd(sock.get(), 10s));
}

Response Get(uint16_t port, const std::string& path, const std::vector<std::string>& headers = {}) {
    return Request(port, "GET", path, headers);
}

// The request that opens the live-reload stream. It is written out here rather
// than in each test that needs it because it is the one request whose answer
// has no end, and the tests below go on reading the same socket afterwards.
std::string StreamRequest(uint16_t port) {
    return std::string("GET /guchho HTTP/1.1\r\n") + "Host: 127.0.0.1:" +
           std::to_string(port) + "\r\n" + "Accept: text/event-stream\r\n\r\n";
}

// Opens the live-reload stream and reads as far as the greeting that starts it,
// which is what tells a test the connection is up and the stream is owned by
// the server. The socket is returned still open, for whatever the test expects
// to be pushed into it; an invalid one means the greeting never arrived.
Socket OpenStream(uint16_t port, std::string& received) {
    Socket stream = ConnectLoopback(port);
    if (!stream.valid()) {
        return stream;
    }
    if (!SendAll(stream.get(), StreamRequest(port)) ||
        !ReadUntil(stream.get(), "retry: 500", received, 10s)) {
        stream.Close();
    }
    return stream;
}

// A port that was free a moment ago, found by letting the operating system
// hand one out. The window between closing it and the server binding it is
// small enough to ignore, and the consequence of losing the race is a visible
// failure to bind rather than a hang.
uint16_t PickFreePort() {
    StartupSockets();

    Socket sock(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.valid()) {
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(sock.get(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

// ===========================================================================
// A server to test
// ===========================================================================

// The tree every server below serves: a script, a file with an extension no
// media type is known for, a directory with no index to turn into a listing,
// and a directory with one to show the index winning over the listing.
std::unique_ptr<filesystem::Fs> MakeServeFs() {
    return guchho::test::MakeMockFS({
        {"/www/app.js", "export const x = 1;\n"},
        {"/www/data.bin", "0123456789"},
        {"/www/sub/other.js", "export const y = 2;\n"},
        {"/www/with-index/index.html", "<!doctype html><title>index</title>\n"},
    },
                                    filesystem::MockKind::kUnix, "/");
}

// A started server, holding everything it needs to stay up: the filesystem it
// reads through, and the callback that shuts it down. The stop runs from the
// destructor, because a failing assertion throws and would otherwise leave an
// accept thread and its socket behind for the rest of the run.
struct RunningServer {
    std::unique_ptr<filesystem::Fs> fs;
    api::ServeResult                result;
    std::string                     error;

    ~RunningServer() {
        if (result.stop) {
            result.stop();
        }
    }

    uint16_t Port() const {
        return result.port;
    }
};

// The options a server is started with unless a test changes something: the
// loopback interface, the served tree, and a port the test chose.
api::ServeOptions DefaultOptions() {
    api::ServeOptions options;
    options.port     = PickFreePort();
    options.host     = "127.0.0.1";
    options.servedir = "www";
    return options;
}

// Starts a server. The log level is the quietest there is, so a test run
// produces no URLs on stdout; a port that turned out to be taken between being
// picked and being bound is not worth failing over, so a few ports are tried
// before giving up.
std::unique_ptr<RunningServer> StartServer(
    const api::ServeOptions& options = DefaultOptions(),
    const api::RebuildFn&    rebuild = [] { return api::BuildResult{}; },
    std::unique_ptr<filesystem::Fs> fs = MakeServeFs()) {
    std::unique_ptr<RunningServer> server = std::make_unique<RunningServer>();
    server->fs                            = std::move(fs);

    api::ServeOptions effective = options;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (effective.port == 0) {
            effective.port = PickFreePort();
        }
        server->result = api::Serve(*server->fs, rebuild, effective, logger::LogLevel::kNone,
                                    logger::UseColor::kColorNever, server->error);
        if (server->result.stop) {
            return server;
        }
        effective.port = 0;
    }
    return server;
}

// Turns a string into the bytes an OutputFile holds.
std::vector<uint8_t> Bytes(std::string_view text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// One output file, as a build would report it. The fields are filled in rather
// than designated in one expression because a designated initialiser that skips
// the middle of a struct is a warning under the stricter compilers, and this is
// the only place a test invents output for itself.
api::OutputFile MakeOutputFile(std::string path, std::string_view text) {
    api::OutputFile file;
    file.path     = std::move(path);
    file.contents = Bytes(text);
    return file;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Configuration that is refused before anything is bound
// ---------------------------------------------------------------------------

// A certificate without its key is a configuration mistake, and it is reported
// as one rather than as the server being unable to do TLS: the two say
// different things about what to fix, so a caller can tell them apart. Nothing
// is bound, so there is no port and no way to stop a server that never started.
TEST(ApiServe, AKeyWithoutItsCertificateIsRefused) {
    std::unique_ptr<filesystem::Fs> fs = MakeServeFs();
    api::ServeOptions options;
    options.host    = "127.0.0.1";
    options.port    = PickFreePort();
    options.keyfile = "key.pem";

    api::ServeResult result;
    std::string      error;
    result = api::Serve(*fs, [] { return api::BuildResult{}; }, options,
                        logger::LogLevel::kSilent, logger::UseColor::kColorNever, error);

    EXPECT_EQ(error, "Must specify both key and certificate for HTTPS");
    EXPECT_EQ(result.port, 0);
    EXPECT_FALSE(static_cast<bool>(result.stop));
}

// The same mistake the other way round is the same message, because it is the
// same mistake; a test that only supplied a certificate would otherwise find
// the check untested from its own side.
TEST(ApiServe, ACertificateWithoutItsKeyIsRefused) {
    std::unique_ptr<filesystem::Fs> fs = MakeServeFs();
    api::ServeOptions options;
    options.host     = "127.0.0.1";
    options.port     = PickFreePort();
    options.certfile = "cert.pem";

    std::string error;
    api::ServeResult result = api::Serve(*fs, [] { return api::BuildResult{}; }, options,
                                         logger::LogLevel::kSilent, logger::UseColor::kColorNever,
                                         error);

    EXPECT_EQ(error, "Must specify both key and certificate for HTTPS");
    EXPECT_EQ(result.port, 0);
    EXPECT_FALSE(static_cast<bool>(result.stop));
}

// Both files present is a complete pair, so the pair check is passed and the
// request fails for the other reason: the in-repo server speaks plain HTTP
// only. Reporting that as a missing pair would send a caller looking for a file
// that is there.
TEST(ApiServe, ATlsPairIsRefusedAsUnsupported) {
    std::unique_ptr<filesystem::Fs> fs = MakeServeFs();
    api::ServeOptions options;
    options.host     = "127.0.0.1";
    options.port     = PickFreePort();
    options.keyfile  = "key.pem";
    options.certfile = "cert.pem";

    std::string      error;
    api::ServeResult result = api::Serve(*fs, [] { return api::BuildResult{}; }, options,
                                         logger::LogLevel::kSilent, logger::UseColor::kColorNever,
                                         error);

    EXPECT_EQ(error, "HTTPS is not yet supported by the in-repo HTTP server");
    EXPECT_EQ(result.port, 0);
    EXPECT_FALSE(static_cast<bool>(result.stop));
}

// An origin may hold one wildcard, which is what lets a caller write
// "http://localhost:*". Two of them cannot be matched, so the configuration is
// rejected while the options are read rather than quietly never matching.
TEST(ApiServe, AnOriginWithTwoWildcardsIsRefused) {
    std::unique_ptr<filesystem::Fs> fs = MakeServeFs();
    api::ServeOptions options = DefaultOptions();
    options.cors.origin       = {"http://*.example.*"};

    std::string      error;
    api::ServeResult result = api::Serve(*fs, [] { return api::BuildResult{}; }, options,
                                         logger::LogLevel::kSilent, logger::UseColor::kColorNever,
                                         error);

    EXPECT_EQ(error, "Invalid origin: http://*.example.*");
    EXPECT_EQ(result.port, 0);
    EXPECT_FALSE(static_cast<bool>(result.stop));
}

// ---------------------------------------------------------------------------
// Starting and stopping
// ---------------------------------------------------------------------------

// A server that started has a port and a way to stop it, and reported no
// error. The address it bound is not asserted on here: the port is the part a
// caller has to act on, and it is the part that can be checked without asking
// the question twice.
TEST(ApiServe, AStartedServerReportsAPortAndNoError) {
    std::unique_ptr<RunningServer> server = StartServer();

    ASSERT_TRUE(server->result.stop);
    EXPECT_TRUE(server->error.empty());
    EXPECT_NE(server->Port(), 0);
}

// The socket is bound before Serve returns, so the port in the result is final
// and the first request cannot lose a race with the listener coming up. That is
// the whole reason a test may use this port without waiting.
TEST(ApiServe, ThePortIsUsableAsSoonAsServeReturns) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js");
    EXPECT_TRUE(response.connected);
    EXPECT_EQ(response.status, 200);
}

// stop() is the only way to release the socket, and after it the port answers
// nothing. Calling it once is enough; a server that is still listening would
// leave the run holding a port the next test may pick.
TEST(ApiServe, StopReleasesThePort) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);
    const uint16_t port = server->Port();

    Socket before = ConnectLoopback(port);
    ASSERT_TRUE(before.valid());
    before.Close();

    server->result.stop();
    server->result.stop = nullptr;

    Socket after = ConnectLoopback(port);
    EXPECT_FALSE(after.valid());
}

// ---------------------------------------------------------------------------
// What a request is answered with
// ---------------------------------------------------------------------------

// The plain case: a path inside the served directory comes back with its bytes
// and the media type implied by its extension, and the length matches what was
// written so a client can size its buffers.
TEST(ApiServe, AFileInTheServedDirectoryIsServed) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js");
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "export const x = 1;\n");
    EXPECT_EQ(response.Get("content-type"), "text/javascript; charset=utf-8");
    EXPECT_EQ(response.Get("content-length"), std::to_string(response.body.size()));
}

// A path that is in neither the build's output, the served directory nor the
// fallback is simply not there, and the answer says so in the body as well as
// in the status: a 404 with a body is what a browser shows, and what a person
// reading a log can act on.
TEST(ApiServe, AMissingFileIsNotFound) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/missing.js");
    EXPECT_EQ(response.status, 404);
    EXPECT_EQ(response.body, "404 - Not Found");
    EXPECT_EQ(response.Get("content-type"), "text/plain; charset=utf-8");
}

// An extension no media type is known for is answered as bytes rather than
// guessed at, so a client is told the truth about not knowing.
TEST(ApiServe, AnUnknownExtensionIsServedAsOctetStream) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/data.bin");
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "0123456789");
    EXPECT_EQ(response.Get("content-type"), "application/octet-stream");
}

// A browser seeking inside a media file asks for a range rather than for the
// whole thing. The status says the answer is partial and the Content-Range
// says which part, which is what makes a second request able to continue.
TEST(ApiServe, AByteRangeIsHonoured) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/data.bin", {"Range: bytes=2-5"});
    EXPECT_EQ(response.status, 206);
    EXPECT_EQ(response.body, "2345");
    EXPECT_EQ(response.Get("content-range"), "bytes 2-5/10");
    EXPECT_EQ(response.Get("content-length"), "4");
}

// A HEAD is a GET without the body, and it is answered like a GET: the length
// the body would have had is the point of asking, so sending no bytes at all
// would leave the caller nothing.
TEST(ApiServe, AHeadRequestSendsHeadersOnly) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Request(server->Port(), "HEAD", "/app.js");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.Get("content-length"), std::to_string(std::string("export const x = 1;\n").size()));
    EXPECT_TRUE(response.body.empty());
}

// ---------------------------------------------------------------------------
// Directories
// ---------------------------------------------------------------------------

// A directory is only answered under its trailing separator, and says where to
// go instead. The redirect is what makes the relative links inside a listing
// resolve one level deeper than they otherwise would, so it is not a
// convenience.
TEST(ApiServe, ADirectoryWithoutATrailingSlashRedirects) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/sub");
    EXPECT_EQ(response.status, 302);
    EXPECT_EQ(response.Get("location"), "/sub/");
}

// A directory with no index becomes a listing, because an empty 404 for a
// directory that exists tells a person nothing about what is in it.
TEST(ApiServe, ADirectoryWithoutAnIndexBecomesAListing) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/sub/");
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.Get("content-type"), "text/html; charset=utf-8");
    EXPECT_TRUE(Contains(response.body, "Directory: /sub/"));
    EXPECT_TRUE(Contains(response.body, "other.js"));
    EXPECT_TRUE(Contains(response.body, "href=\"/sub/other.js\""));
}

// An index file is the directory's own answer, so it wins over the listing: a
// site that ships an index page is asking for that page, not for a table of
// what it contains.
TEST(ApiServe, AnIndexFileWinsOverTheListing) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/with-index/");
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "<!doctype html><title>index</title>\n");
}

// The root of the served tree lists too, and its title has no doubled separator.
TEST(ApiServe, TheServedRootListsWithoutADoubledSeparator) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/");
    ASSERT_EQ(response.status, 200);
    EXPECT_TRUE(Contains(response.body, "Directory: /<"));
    EXPECT_FALSE(Contains(response.body, "Directory: //"));
}

// ---------------------------------------------------------------------------
// The fallback page
// ---------------------------------------------------------------------------

// The fallback answers every path nothing else claimed, which is what lets one
// process serve a single-page application: a browser route that was never a
// file still gets the document that boots the router.
TEST(ApiServe, TheFallbackPageAnswersUnknownRoutes) {
    api::ServeOptions options  = DefaultOptions();
    options.fallback           = "www/with-index/index.html";
    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app/some/deep/route");
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "<!doctype html><title>index</title>\n");
}

// The fallback is the last place looked in, not the first: a file that exists
// is still served, and only a path with nothing behind it reaches the fallback.
TEST(ApiServe, TheFallbackDoesNotShadowARealFile) {
    api::ServeOptions options      = DefaultOptions();
    options.fallback               = "www/with-index/index.html";
    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js");
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "export const x = 1;\n");
}

// ---------------------------------------------------------------------------
// The build
// ---------------------------------------------------------------------------

// Every request runs the build first, which is what makes a reload show the
// current output without the caller having to say anything. Two requests mean
// two builds; a server that cached the result would answer the second from
// memory and save a developer the second build they asked for.
TEST(ApiServe, EveryRequestRunsTheBuild) {
    std::atomic<int> builds{0};
    auto rebuild = [&builds] {
        builds.fetch_add(1);
        return api::BuildResult{};
    };

    std::unique_ptr<RunningServer> server = StartServer(DefaultOptions(), rebuild);
    ASSERT_TRUE(server->result.stop);

    Get(server->Port(), "/app.js");
    EXPECT_EQ(builds.load(), 1);
    Get(server->Port(), "/app.js");
    EXPECT_EQ(builds.load(), 2);
}

// A build that failed answers with its errors instead of its output, so a
// mistake is shown rather than papered over with the last bundle that worked.
TEST(ApiServe, AFailedBuildIsServedAsAnError) {
    auto rebuild = [] {
        api::BuildResult result;
        api::Message     error;
        error.id   = "could-not-resolve";
        error.text = "Could not resolve \"src/missing.js\"";
        result.errors.push_back(std::move(error));
        return result;
    };

    std::unique_ptr<RunningServer> server = StartServer(DefaultOptions(), rebuild);
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js");
    EXPECT_EQ(response.status, 503);
    EXPECT_TRUE(Contains(response.body, "Could not resolve \"src/missing.js\""));
    EXPECT_EQ(response.Get("content-type"), "text/plain; charset=utf-8");
}

// A request that never reaches the build — a path the server refuses outright —
// must not run one, since the build is the expensive part of answering.
TEST(ApiServe, ARefusedRequestDoesNotRunTheBuild) {
    std::atomic<int> builds{0};
    auto rebuild = [&builds] {
        builds.fetch_add(1);
        return api::BuildResult{};
    };

    std::unique_ptr<RunningServer> server = StartServer(DefaultOptions(), rebuild);
    ASSERT_TRUE(server->result.stop);

    const Response refused = Request(server->Port(), "GET", "/app.js", {}, "evil.example");
    EXPECT_EQ(refused.status, 403);
    EXPECT_EQ(builds.load(), 0);
}

// ---------------------------------------------------------------------------
// What the server refuses
// ---------------------------------------------------------------------------

// The Host header is checked against the addresses the server bound, so a
// machine that can reach the socket cannot make it serve something by naming a
// host it likes. The refusal quotes the host, because a 403 with no reason is
// the kind of thing a person cannot act on.
TEST(ApiServe, AHostOutsideTheAllowListIsForbidden) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Request(server->Port(), "GET", "/app.js", {}, "evil.example");
    EXPECT_EQ(response.status, 403);
    EXPECT_TRUE(Contains(response.body, "evil.example"));
}

// "localhost" is accepted by name whatever was bound, because it is the address
// a developer types, and a server that answered a build on one interface and
// not on the name it was reached by would be baffling.
TEST(ApiServe, LocalhostIsAcceptedByName) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Request(server->Port(), "GET", "/app.js", {}, "localhost");
    EXPECT_EQ(response.status, 200);
}

// A separator the request line is not allowed to carry is refused rather than
// cleaned up: a path that was mangled on the way in cannot be answered with
// something the caller did not ask for.
TEST(ApiServe, ABackslashInThePathIsABadRequest) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Request(server->Port(), "GET", "/sub\\other.js");
    EXPECT_EQ(response.status, 400);
}

// Browsers ask for this unprompted on every page, and a 404 in the console of a
// page being debugged is noise. A client that accepts gzip is answered with the
// icon pre-compressed, since the server has it either way.
TEST(ApiServe, TheFaviconIsServedToAClientThatAcceptsGzip) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/favicon.ico", {"Accept-Encoding: gzip"});
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.Get("content-encoding"), "gzip");
    EXPECT_EQ(response.Get("content-type"), "image/vnd.microsoft.icon");
    EXPECT_FALSE(response.body.empty());
}

// A client that did not ask for a compressed body is not given one, even for
// the icon: "Content-Encoding" the client cannot decode is worse than the 404
// it replaced.
TEST(ApiServe, TheFaviconIsNotServedWithoutGzip) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/favicon.ico");
    EXPECT_EQ(response.status, 404);
}

// ---------------------------------------------------------------------------
// CORS
// ---------------------------------------------------------------------------

// An origin on the list is echoed back rather than replaced with the pattern,
// because that is the only value a browser will accept. A client that is not on
// the list gets no header at all, which is what makes the browser refuse the
// response rather than the server having to.
TEST(ApiServe, AnAllowedOriginIsEchoedAndAnUnknownOneIsNot) {
    api::ServeOptions options    = DefaultOptions();
    options.cors.origin          = {"http://localhost:1234"};
    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    const Response allowed = Get(server->Port(), "/app.js", {"Origin: http://localhost:1234"});
    ASSERT_EQ(allowed.status, 200);
    EXPECT_EQ(allowed.Get("access-control-allow-origin"), "http://localhost:1234");

    const Response refused = Get(server->Port(), "/app.js", {"Origin: http://localhost:4321"});
    EXPECT_FALSE(refused.Has("access-control-allow-origin"));
}

// A single wildcard inside an origin is a pattern, so a port may be left out of
// it and the origin it matched is echoed — the same value the browser sent, not
// the pattern it matched against.
TEST(ApiServe, AWildcardOriginMatchesAPortAndIsEchoed) {
    api::ServeOptions options    = DefaultOptions();
    options.cors.origin          = {"http://localhost:*"};
    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js", {"Origin: http://localhost:1234"});
    EXPECT_EQ(response.Get("access-control-allow-origin"), "http://localhost:1234");
}

// A bare "*" answers every origin with "*", which is the one pattern a browser
// accepts without a list of its own.
TEST(ApiServe, AStarOriginAnswersEveryOrigin) {
    api::ServeOptions options    = DefaultOptions();
    options.cors.origin          = {"*"};
    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js", {"Origin: http://anywhere.example"});
    EXPECT_EQ(response.Get("access-control-allow-origin"), "*");
}

// No list configured means no header, so a server that was not asked to be
// permissive is not permissive.
TEST(ApiServe, WithoutAConfiguredOriginNoHeaderIsSent) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    const Response response = Get(server->Port(), "/app.js", {"Origin: http://localhost:1234"});
    EXPECT_FALSE(response.Has("access-control-allow-origin"));
}

// ---------------------------------------------------------------------------
// The request callback
// ---------------------------------------------------------------------------

// The callback is the one place a host program can see what the server is
// doing, so it has to name the request and the answer: a count of requests
// alone cannot be matched against anything.
TEST(ApiServe, TheRequestCallbackNamesTheRequestAndTheAnswer) {
    std::mutex            mu;
    std::vector<api::ServeOnRequestArgs> reported;

    api::ServeOptions options = DefaultOptions();
    options.on_request        = [&mu, &reported](const api::ServeOnRequestArgs& args) {
        std::lock_guard<std::mutex> lock(mu);
        reported.push_back(args);
    };

    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    Get(server->Port(), "/app.js");

    std::vector<api::ServeOnRequestArgs> seen;
    {
        std::lock_guard<std::mutex> lock(mu);
        seen = reported;
    }
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen.front().method, "GET");
    EXPECT_EQ(seen.front().path, "/app.js");
    EXPECT_EQ(seen.front().status, 200);
    EXPECT_FALSE(seen.front().remote_address.empty());
}

// The status reported is the one that was answered, so a count of 404s built
// from this callback agrees with what a client was told.
TEST(ApiServe, TheRequestCallbackReportsTheStatusItAnswered) {
    std::mutex                          mu;
    std::vector<int>                    statuses;

    api::ServeOptions options = DefaultOptions();
    options.on_request        = [&mu, &statuses](const api::ServeOnRequestArgs& args) {
        std::lock_guard<std::mutex> lock(mu);
        statuses.push_back(args.status);
    };

    std::unique_ptr<RunningServer> server = StartServer(options);
    ASSERT_TRUE(server->result.stop);

    Get(server->Port(), "/missing.js");

    std::vector<int> seen;
    {
        std::lock_guard<std::mutex> lock(mu);
        seen = statuses;
    }
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen.front(), 404);
}

// ---------------------------------------------------------------------------
// Live reload
// ---------------------------------------------------------------------------

// The reserved stream is the one response with no end: it holds its connection
// and answers with a frame each time a rebuild changed something. The test
// opens it, waits for the head, then asks for a file twice with a build that
// returns different output each time — the first build is the baseline, the
// second is a change, and only a change is worth a frame.
TEST(ApiServe, ARebuildChangeIsPushedToTheLiveReloadStream) {
    std::atomic<int> builds{0};
    auto rebuild = [&builds] {
        api::BuildResult result;
        const int        n = builds.fetch_add(1) + 1;
        result.output_files.push_back(MakeOutputFile("/out/app.js", "build " + std::to_string(n)));
        return result;
    };

    std::unique_ptr<RunningServer> server = StartServer(DefaultOptions(), rebuild);
    ASSERT_TRUE(server->result.stop);

    std::string received;
    Socket       stream = OpenStream(server->Port(), received);
    ASSERT_TRUE(stream.valid());
    EXPECT_TRUE(Contains(received, "200 OK"));
    EXPECT_EQ(ParseResponse(received).Get("content-type"), "text/event-stream");

    // The first build has no predecessor, so everything it produced is new and
    // the stream is told about it; the second build is compared against it, and
    // only the file that changed is named. Both frames are read rather than
    // raced against, so the assertion cannot pass on a frame that had not been
    // written yet.
    Get(server->Port(), "/app.js");
    EXPECT_EQ(builds.load(), 1);
    ASSERT_TRUE(ReadUntil(stream.get(), "\"added\"", received, 10s));
    EXPECT_TRUE(Contains(received, "app.js"));

    received.clear();
    Get(server->Port(), "/app.js");
    EXPECT_EQ(builds.load(), 2);

    ASSERT_TRUE(ReadUntil(stream.get(), "event: change", received, 10s));
    EXPECT_TRUE(Contains(received, "\"updated\""));
    EXPECT_TRUE(Contains(received, "app.js"));
}

// A build whose output did not change must not make a browser reload: that is
// the whole point of the diff, so the stream goes quiet after the first build
// has told it what exists and stays quiet while the bytes stay the same.
TEST(ApiServe, AnUnchangedBuildPushesNothingToTheStream) {
    auto rebuild = [] {
        api::BuildResult result;
        result.output_files.push_back(MakeOutputFile("/out/app.js", "the same bytes"));
        return result;
    };

    std::unique_ptr<RunningServer> server = StartServer(DefaultOptions(), rebuild);
    ASSERT_TRUE(server->result.stop);

    std::string received;
    Socket       stream = OpenStream(server->Port(), received);
    ASSERT_TRUE(stream.valid());

    Get(server->Port(), "/app.js");
    ASSERT_TRUE(ReadUntil(stream.get(), "\"added\"", received, 10s));

    received.clear();
    Get(server->Port(), "/app.js");
    Get(server->Port(), "/app.js");

    // The frame for an unchanged build would have been written the moment the
    // build finished, so a socket still silent well after that has nothing left
    // to deliver.
    EXPECT_FALSE(WaitForData(stream.get(), 500ms));
}

// A stream left open must not outlive the server: stop() closes the open ones
// before it tears the accept loop down, so a process that shuts down with a
// browser still connected returns instead of waiting for a client that will
// never ask again.
TEST(ApiServe, StopClosesAnOpenStream) {
    std::unique_ptr<RunningServer> server = StartServer();
    ASSERT_TRUE(server->result.stop);

    std::string received;
    Socket       stream = OpenStream(server->Port(), received);
    ASSERT_TRUE(stream.valid());

    server->result.stop();
    server->result.stop = nullptr;

    // The server's end is gone, so the connection reads as finished instead of
    // waiting for a frame that will never come.
    std::string rest;
    while (ReadUntil(stream.get(), "\n", rest, 300ms)) {
        if (rest.size() > 4096) {
            break;
        }
    }
    EXPECT_TRUE(Contains(received, "retry: 500"));
}
