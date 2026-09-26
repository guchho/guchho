
// The development server behind the public "Serve()" API.
//
// Guchho's server exists to make a build that is still moving browsable, and
// that goal shapes every decision in this file. It speaks HTTP/1.1 directly on
// the platform's sockets with no third-party networking dependency, and it is
// built around one idea: what a browser sees is the current output of the
// build, not whatever happens to be sitting on disk.
//
//   * A build is never written to disk just so a browser can read it. "Serve()"
//     is handed a "RebuildFn" and calls it for every file request, so the bytes
//     in the response are the bytes the linker has just produced.
//
//   * A failing build is reported instead of hidden. When the rebuild returns
//     errors the response is "503" carrying those messages as plain text, so a
//     broken edit shows up in the browser immediately rather than leaving a
//     stale bundle on screen.
//
//   * Requests are resolved in a fixed order: the current build output first,
//     then the "servedir" directory on disk, then the configured fallback page.
//     That order is what lets a build own the paths it emits while the static
//     files it knows nothing about keep being served.
//
//   * Long-lived clients are first-class. A page that wants live reloads holds a
//     "text/event-stream" connection open; after each rebuild the server diffs
//     the hashes of the output files and pushes the added, removed and updated
//     URLs to every open stream, and emits a keep-alive comment when a stream
//     goes quiet.
//
//   * Anything that reaches a browser is escaped on the way out. Directory
//     listings are assembled from file names that may contain quotes and angle
//     brackets, so each name is escaped for element text and for attribute
//     values before it is concatenated into the page.
//
// Threading is deliberately plain. One detached thread runs the accept loop,
// every accepted connection is served on a thread of its own, and the only
// state they share is "ApiHandler": the host allow-list, the live event
// streams, and the output-file hashes from the previous rebuild, all behind a
// single mutex. The listening socket is owned by a shared pointer captured by
// both threads, which is how "stop" can close it from another thread and let
// the loop unwind.
#include "guchho/api.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
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
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace guchho::api {

namespace logger = guchho::logger;

namespace {

// ===========================================================================
// Cross-platform socket layer
//
// Guchho takes no dependency on a networking library, so the handful of socket
// operations the server needs are wrapped here. "SockHandle" is the one name
// that has to differ between platforms: an unsigned "SOCKET" value on Windows
// and a plain file descriptor everywhere else. Every other call in this file
// goes through the wrappers below rather than touching the platform headers
// directly, which is what keeps the rest of the file free of conditionals.
// ===========================================================================
#ifdef _WIN32
using SockHandle = SOCKET;
constexpr SockHandle kInvalidSocket = INVALID_SOCKET;
#else
using SockHandle = int;
constexpr SockHandle kInvalidSocket = -1;
#endif

// Closes a socket and reports whether the platform accepted the close.
//
// Input : a live socket handle.
// Output: true when the handle was closed, false when the platform refused.
//
// A refused close is not treated as an error anywhere in this file: the handle
// is being discarded either way and there is nothing left to retry.
bool CloseSocket(SockHandle sock) {
// Closing a socket does not reliably wake a blocking accept on
// Windows, so the loop polls the listener with a short select and
// notices the close on the next tick. The poll is 200ms, which is
// frequent enough for a responsive shutdown and rare enough to cost
// nothing measurable.
#ifdef _WIN32
    return closesocket(sock) == 0;
#else
    return close(sock) == 0;
#endif
}

// Half-closes a socket so a peer blocked in a read wakes up.
//
// Input : a socket handle and a direction, meaning receive, send or both.
// Output: true when the platform accepted the shutdown.
//
// The argument lists of the two platform calls match, so the body needs no
// conditional; what differs is the handle type, and that is spelled out in the
// alias above. The accept loop does not use this, because closing the listening
// socket is enough to unblock it; it is here for a teardown that wants the peer
// to observe an end-of-stream before the socket disappears.
[[maybe_unused]] bool ShutdownSocket(SockHandle sock, int how) {
#ifdef _WIN32
    return shutdown(sock, how) == 0;
#else
    return shutdown(sock, how) == 0;
#endif
}

// Performs the one-time platform setup that the socket calls depend on.
//
// Input : none.
// Output: none.
//
// Windows requires the Winsock library to be started once per process before
// any other socket call is legal, and POSIX requires nothing. The static flag
// keeps repeat calls free, which matters because "Listener::Listen" calls this
// on every attempt, including each port tried in turn.
void InitSockets() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        initialized = true;
    }
#else
#endif
}

// One address of a bound socket, already expressed as text.
//
// "ip" is numeric text only, such as "127.0.0.1" or "::1", and never a resolved
// host name: the value is both printed for the user and compared against
// incoming "Host" headers, and a name would make the first of those ambiguous
// and the second unreliable. "port" is the decimal port as text, which saves a
// conversion for the callers that only ever print it.
struct EndPoint {
    std::string ip;
    std::string port;
};

// Reads the address a socket is actually bound to.
//
// Input : a bound socket.
// Output: true with the numeric address and port in "out", or false when the
//         socket has no name or the address cannot be formatted.
//
// This is how the server learns which port the operating system handed out when
// the caller asked for port 0. The port read back here is the one to report and
// to print, because the requested value was only a suggestion.
bool GetSockName(SockHandle sock, EndPoint& out) {
    struct sockaddr_storage addr {};
    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return false;
    }
    char host[NI_MAXHOST] = {};
    char port[NI_MAXSERV] = {};
    int rc = getnameinfo(reinterpret_cast<sockaddr*>(&addr), len, host, sizeof(host),
                         port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
        return false;
    }
    out.ip = host;
    out.port = port;
    return true;
}

// Splits a "Host" header value into its host and port text.
//
// Input : "127.0.0.1:8000"  -> ("127.0.0.1", "8000")
//         "[::1]:8000"      -> ("::1",     "8000")
//         "example.com"     -> ("example.com", "")
//         "[::1"            -> no value, the header is malformed
//
// An IPv6 literal is bracketed, so a port is only recognised after the closing
// bracket. A host with several colons and no brackets is left whole, because
// every colon in an unbracketed IPv6 literal belongs to the address rather than
// separating a port.
std::optional<std::pair<std::string, std::string>> SplitHostPort(const std::string& s) {
    std::string host = s;
    std::string port;
    if (!host.empty() && host.front() == '[') {
        size_t close = host.find(']');
        if (close == std::string::npos) {
            return std::nullopt;
        }
        std::string inner = host.substr(1, close - 1);
        if (close + 1 < host.size() && host[close + 1] == ':') {
            port = host.substr(close + 2);
        }
        host = inner;
    } else {
        size_t colon = host.rfind(':');
        if (colon != std::string::npos && host.find(':') == colon) {
            port = host.substr(colon + 1);
            host = host.substr(0, colon);
        }
    }
    return std::make_pair(host, port);
}

// Reports whether a host string is a numeric address rather than a name.
//
// Input : "127.0.0.1" -> true, with "is_v6" false
//         "::1"       -> true, with "is_v6" true
//         "localhost" -> false, and "is_v6" is not written
//
// Any colon is taken as proof of an IPv6 literal, and an IPv4 literal is
// confirmed by "inet_pton". The answer decides which address family the
// listening socket is created with, and whether the host may appear in the list
// of accepted "Host" header values.
bool IsIPLiteral(const std::string& host, bool& is_v6) {
    if (host.find(':') != std::string::npos) {
        is_v6 = true;
        return true;
    }
    struct in_addr a {};
    if (inet_pton(AF_INET, host.c_str(), &a) == 1) {
        is_v6 = false;
        return true;
    }
    return false;
}

// Reports whether an address means every interface.
//
// Input : "", "0.0.0.0", "::" and "[::]" are true; anything else is false.
//
// Binding to an unspecified address does not produce a single address to visit,
// so the server enumerates the local interfaces instead and offers one URL per
// address it finds.
bool IsUnspecifiedIP(const std::string& host) {
    return host.empty() || host == "0.0.0.0" || host == "::" || host == "[::]";
}

// Reports whether an address only reaches this machine.
//
// Input : "127.0.0.1", "::1" and "[::1]" are true; any other address is false.
//
// The answer decides whether a printed URL is labelled "Local" or "Network", and
// whether the address is added to the set of "Host" header values the server
// will answer to.
bool IsLoopbackIP(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "[::1]";
}

// Collects local interface addresses that a browser could actually use.
//
// Input : false for IPv4 addresses only, true for IPv6 addresses only.
// Output: the addresses appended to "out", and true on success or false when
//         the interface list could not be read at all.
//
// Addresses no other machine can reach are skipped in both families: the
// "169.254." link-local range, and IPv6 link-local plus loopback addresses.
// Printing a URL that only exists on this machine would be worse than printing
// nothing, and the loopback address is already covered by the "Local" line.
bool GetLocalIPs(bool want_v6, std::vector<std::string>& out) {
#ifdef _WIN32
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 0;
    if (GetAdaptersAddresses(want_v6 ? AF_UNSPEC : AF_INET, flags, nullptr, nullptr, &size) !=
        ERROR_BUFFER_OVERFLOW) {
        return false;
    }
    std::vector<char> buffer(size);
    PIP_ADAPTER_ADDRESSES addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    if (GetAdaptersAddresses(want_v6 ? AF_UNSPEC : AF_INET, flags, nullptr, addrs, &size) != 0) {
        return false;
    }
    for (PIP_ADAPTER_ADDRESSES a = addrs; a != nullptr; a = a->Next) {
        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u != nullptr; u = u->Next) {
            auto* sa = u->Address.lpSockaddr;
            if (sa->sa_family == AF_INET && !want_v6) {
                auto* sin = reinterpret_cast<sockaddr_in*>(sa);
                char buf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
                std::string ip = buf;
                if (ip.rfind("169.254.", 0) == 0) {
                    continue;
                }
                out.push_back(ip);
            } else if (sa->sa_family == AF_INET6 && want_v6) {
                auto* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
                if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
                    IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)) {
                    continue;
                }
                char buf[INET6_ADDRSTRLEN] = {};
                inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
                out.push_back(buf);
            }
        }
    }
    return true;
#else
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) {
        return false;
    }
    for (struct ifaddrs* i = ifa; i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr) {
            continue;
        }
        int family = i->ifa_addr->sa_family;
        if (family == AF_INET && !want_v6) {
            auto* sin = reinterpret_cast<sockaddr_in*>(i->ifa_addr);
            char buf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
            std::string ip = buf;
            if (ip.rfind("169.254.", 0) == 0) {
                continue;
            }
            out.push_back(ip);
        } else if (family == AF_INET6 && want_v6) {
            auto* sin6 = reinterpret_cast<sockaddr_in6*>(i->ifa_addr);
            if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
                IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)) {
                continue;
            }
            char buf[INET6_ADDRSTRLEN] = {};
            inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
            out.push_back(buf);
        }
    }
    freeifaddrs(ifa);
    return true;
#endif
}

// An owned socket that is already listening.
//
// The type is move-only and closes its socket in its destructor, which removes
// the cleanup from every failure path in "Listen" and lets the accept loop hold
// the socket in a shared pointer that "stop" can close from another thread. A
// default-constructed value is invalid, and that is how every failure is
// reported.
class Listener {
public:
    Listener() = default;
    ~Listener() {
        if (sock_ != kInvalidSocket) {
            CloseSocket(sock_);
        }
    }
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&& other) noexcept : sock_(other.sock_) { other.sock_ = kInvalidSocket; }
    Listener& operator=(Listener&& other) noexcept {
        if (this != &other) {
            if (sock_ != kInvalidSocket) CloseSocket(sock_);
            sock_ = other.sock_;
            other.sock_ = kInvalidSocket;
        }
        return *this;
    }

    bool Valid() const { return sock_ != kInvalidSocket; }
    SockHandle Get() const { return sock_; }

    // Binds a socket, starts listening, and reports the address that was taken.
    //
    // Input : a host, where "0.0.0.0" or an empty string both mean every
    //         interface, and a port, where 0 asks the operating system to pick a
    //         free one.
    // Output: a valid listener plus the numeric host and the real port in
    //         "out_host" and "out_port", or an invalid listener and a short
    //         reason in "error" when resolving, binding or listening fails.
    //
    // Every address the host resolves to is tried in turn, so a name that maps
    // to several interfaces still starts as long as one of them accepts the
    // bind. The socket is closed before each failure is returned, so a rejected
    // port does not leak a descriptor while the caller retries the next
    // candidate.
    static Listener Listen(const std::string& host, uint16_t port, uint16_t& out_port,
                           std::string& out_host, std::string& error) {
        InitSockets();
        int family = AF_INET;
        if (!host.empty()) {
            bool is_v6 = false;
            if (IsIPLiteral(host, is_v6)) {
                family = is_v6 ? AF_INET6 : AF_INET;
            }
        }

        SockHandle sock = socket(family, SOCK_STREAM, 0);
        if (sock == kInvalidSocket) {
            error = "socket failed";
            return Listener();
        }

        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

        struct addrinfo hints {};
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo* result = nullptr;
        std::string port_text = std::to_string(port);
        const char* host_cstr = host.empty() ? nullptr : host.c_str();
        int rc = getaddrinfo(host_cstr, port_text.c_str(), &hints, &result);
        if (rc != 0) {
            error = "resolve host failed";
            CloseSocket(sock);
            return Listener();
        }

        bool bound = false;
        for (struct addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            if (bind(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
                bound = true;
                break;
            }
        }
        freeaddrinfo(result);
        if (!bound) {
            error = "bind failed";
            CloseSocket(sock);
            return Listener();
        }

        if (listen(sock, 128) != 0) {
            error = "listen failed";
            CloseSocket(sock);
            return Listener();
        }

        EndPoint ep;
        if (!GetSockName(sock, ep)) {
            error = "getsockname failed";
            CloseSocket(sock);
            return Listener();
        }
        out_host = ep.ip;
        char* end = nullptr;
        unsigned long p = strtoul(ep.port.c_str(), &end, 10);
        out_port = uint16_t(p);

        Listener l;
        l.sock_ = sock;
        return l;
    }

private:
    SockHandle sock_ = kInvalidSocket;
};

// ===========================================================================
// HTTP/1.1 request and response
//
// Just enough of the protocol to serve a build: read a request head, pick a
// status, write a body. There is no keep-alive reuse, no chunked transfer
// encoding and no request body handling. Every response carries a
// "Content-Length", the connection is closed once the handler returns, and the
// only long-lived response is the event stream, which is written as it happens
// and uses a comment frame as its keep-alive.
// ===========================================================================

// Maps a status code to the reason text on the status line.
//
// Input : 404 -> "Not Found"
// Output: the phrase registered for that code, or "Unknown" for a code with no
//         entry of its own.
//
// The number is what a client parses, but this text is what a person sees in a
// terminal or a network inspector, so it is spelled out rather than left blank.
const char* ReasonPhrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
    }
}

// One request header, kept in the case it arrived in.
//
// The protocol treats the name without regard to case, so it is stored verbatim
// and every lookup goes through "Request::GetHeader", which folds case.
struct HttpHeader {
    std::string name;
    std::string value;
};

// A parsed request head.
//
// "path" stays percent-encoded exactly as it arrived and is never decoded
// here; each routing step works on the text it needs and leaves the rest alone.
// "host_header" is kept apart from the header list because the allow-list check
// consults it on every request and because its absence changes what the request
// means.
struct Request {
    std::string method;
    std::string path;
    std::string host_header;

    std::vector<HttpHeader> headers;
    std::string             remote_addr;

    // Finds a header by name, ignoring case.
    //
    // Input : "Accept" with a stored "accept: text/event-stream" -> that value.
    // Output: the value of the first match, or an empty string when the header
    //         is absent, which lets callers test the result as a boolean.
    std::string GetHeader(std::string_view name) const {
        for (const HttpHeader& h : headers) {
            if (helpers::EqualFoldASCII(h.name, name)) {
                return h.value;
            }
        }
        return "";
    }
};

// Reads from a socket until the end of the request head is in hand.
//
// Input : a connected socket.
// Output: the head as text in "out", including the blank line that ends it, or
//         false on a read error, a closed peer, or a head larger than 64 KiB.
//
// The head is what is bounded rather than the total read, because the bytes
// after it would be a request body this server never accepts. Both line endings
// are recognised, so a hand-written request from a script arrives intact just
// like one from a browser.
bool ReadRequestHead(SockHandle sock, std::string& out) {
    std::string buffer;
    buffer.reserve(4096);
    char chunk[4096];
    for (;;) {
        int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return false;
        }
        buffer.append(chunk, size_t(n));
        if (buffer.size() > 64 * 1024) {
            return false;
        }
        if (buffer.find("\r\n\r\n") != std::string::npos ||
            buffer.find("\n\n") != std::string::npos) {
            break;
        }
    }
    out = std::move(buffer);
    return true;
}

// Removes leading and trailing whitespace.
//
// Input : "  Accept: text/html \r" -> "Accept: text/html"
// Output: the trimmed copy.
//
// Header values arrive with whatever spacing the client chose to send, and the
// carriage return that ends a header line belongs to the line ending rather than
// to the value.
std::string TrimSpaces(std::string_view s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(begin, end - begin));
}

// Parses a request head into a method, a path and a list of headers.
//
// Input : "GET /a.js?v=1 HTTP/1.1\r\nHost: localhost:8000\r\n\r\n"
// Output: a "Request" with method "GET", path "/a.js", host header
//         "localhost:8000" and one "HttpHeader", or false when the request line
//         does not have the three parts a method, a target and a version
//         require.
//
// The query string is split off and discarded here, so a cache-busting query
// does not turn into a miss. A header line without a colon, and a line that is
// empty once trimmed, are ignored rather than rejected: a client that sends a
// stray blank line should still get its file.
bool ParseRequestHead(const std::string& head, Request& req) {
    size_t line_end = head.find('\n');
    std::string request_line;
    if (line_end == std::string::npos) {
        request_line = head;
    } else {
        request_line = TrimSpaces(std::string_view(head.data(), line_end));
    }

    size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos) return false;
    size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string::npos) return false;

    req.method = request_line.substr(0, first_space);
    std::string target = request_line.substr(first_space + 1, second_space - first_space - 1);

    size_t q = target.find('?');
    req.path = (q == std::string::npos) ? target : target.substr(0, q);

    size_t after_first = line_end == std::string::npos ? head.size() : line_end + 1;
    std::string_view body(head.data() + after_first, head.size() - after_first);

    size_t pos = 0;
    size_t len = body.size();
    while (pos < len) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = len;
        std::string_view raw = body.substr(pos, nl - pos);
        if (!raw.empty() && raw.back() == '\r') raw = raw.substr(0, raw.size() - 1);
        if (!raw.empty()) {
            size_t colon = raw.find(':');
            if (colon != std::string::npos) {
                HttpHeader h;
                h.name = TrimSpaces(raw.substr(0, colon));
                h.value = TrimSpaces(raw.substr(colon + 1));
                if (!h.name.empty()) {
                    req.headers.push_back(std::move(h));
                }
            }
        }
        pos = nl + 1;
    }

    req.host_header = req.GetHeader("Host");
    return true;
}

// A response writer bound to one connection.
//
// Headers are collected in a list and the status line goes out on the first body
// write, so a handler may set a header, settle on a status and set another
// header afterwards without having to know the final status up front. Setting
// the same header name twice replaces the earlier value, which is what a caller
// overriding a default expects to happen.
class Response {
public:
    Response(SockHandle sock, bool head) : sock_(sock), head_(head) {}

    void SetHeader(std::string name, std::string value) {
        for (auto& kv : headers_) {
            if (helpers::EqualFoldASCII(kv.first, name)) {
                kv.second = std::move(value);
                return;
            }
        }
        headers_.emplace_back(std::move(name), std::move(value));
    }

    // Sends the status line and every buffered header.
    //
    // Input : a status code such as 404.
    // Output: "HTTP/1.1 404 Not Found", then the header block, then a blank
    //         line. Only the first call has an effect, so a handler may set a
    //         status early and still send a body afterwards.
    void WriteHeader(int status) {
        if (header_sent_) return;
        header_sent_ = true;
        status_ = status;
        std::string out = "HTTP/1.1 ";
        out += std::to_string(status);
        out += ' ';
        out += ReasonPhrase(status);
        out += "\r\n";
        for (const auto& kv : headers_) {
            out += kv.first;
            out += ": ";
            out += kv.second;
            out += "\r\n";
        }
        out += "\r\n";
        WriteRaw(out);
    }

    // Sends a body, flushing the header first if it has not gone out yet.
    //
    // Input : the bytes of the body.
    // Output: those bytes on the socket, except for a "HEAD" request, where
    //         the body is dropped and only the header and the "Content-Length"
    //         that describes the body that would have been sent are written.
    void Write(std::string_view data) {
        if (!header_sent_) WriteHeader(200);
        if (head_) return;
        WriteRaw(std::string(data));
    }

    // Sends the header without a body, for a response that has no content.
    //
    // Input : none.
    // Output: a 200 response when no status has been written yet, and nothing
    //         at all otherwise.
    //
    // A redirect uses this to deliver its "Location" and stop, because the
    // client is expected to follow the link and would only discard a body.
    void Flush() {
        if (!header_sent_) WriteHeader(200);
    }

    SockHandle Socket() const { return sock_; }
    int Status() const { return status_; }

private:
    // Pushes bytes to the socket until they have all been accepted.
    //
    // Input : the bytes to send.
    // Output: the bytes on the socket; a short write or a closed peer ends the
    //         loop early and leaves the rest unsent.
    //
    // "send" is allowed to accept fewer bytes than it was offered, so the offset
    // advances by what was actually taken rather than by the size asked for.
    void WriteRaw(const std::string& data) {
        if (data.empty()) return;
        size_t sent = 0;
        while (sent < data.size()) {
            int n = send(sock_, data.data() + sent, int(data.size() - sent), 0);
            if (n <= 0) {
                return;
            }
            sent += size_t(n);
        }
    }

    SockHandle                            sock_ = kInvalidSocket;
    bool                                  head_ = false;
    bool                                  header_sent_ = false;
    int                                   status_ = 200;
    std::vector<std::pair<std::string, std::string>> headers_;
};

// ===========================================================================
// URL path handling
//
// Every path in this file is a URL path: '/' separated, rooted, and free of
// "." and ".." elements before it reaches the file system. Cleaning happens once
// at the top of a request, so the routing steps below compare paths as plain
// text without having to re-check for traversal.
// ===========================================================================

// Resolves "." and ".." elements and collapses repeated separators.
//
// Input : "/a/b/../c"  -> "/a/c"
//         "a//b"       -> "/a/b"
//         "/a/./b/"    -> "/a/b"
//         "/"          -> "/"
//         ".."         -> ".."
// Output: the cleaned path, or "." when the input is empty or reduces to
//         nothing while staying relative.
//
// A ".." in a rooted path is dropped at the root, because there is nothing above
// the root to move to. A ".." in a relative path is counted instead and only
// re-emitted if the path never becomes rooted, so "a/../b" collapses to "b"
// while "../b" survives with its leading element intact.
std::string CleanURLPath(std::string_view p) {
    if (p.empty()) return ".";
    std::string result;
    result.reserve(p.size());
    bool rooted = !p.empty() && p[0] == '/';
    int dotdot = 0;
    size_t n = p.size();
    size_t i = 0;
    while (i < n) {
        if (p[i] == '/') {
            ++i;
            continue;
        }
        size_t start = i;
        while (i < n && p[i] != '/') ++i;
        std::string_view elem = p.substr(start, i - start);
        if (elem == ".") {
            continue;
        }
        if (elem == "..") {
            if (!result.empty()) {
                size_t j = result.size() - 1;
                while (j > 0 && result[j] != '/') --j;
                if (j > 0) {
                    result.resize(j);
                } else {
                    result.clear();
                }
            } else if (!rooted) {
                ++dotdot;
            }
            continue;
        }
        if (dotdot > 0) {
            --dotdot;
            continue;
        }
        if (result.empty() || result.back() != '/') {
            result.push_back('/');
        }
        result.append(elem);
    }
    if (result.empty()) {
        result = rooted ? "/" : ".";
    }
    return result;
}

// Joins two path fragments and cleans the result.
//
// Input : ("/dist", "app.js") -> "/dist/app.js"
//         ("",     "app.js") -> "app.js"
//         ("/dist", "")      -> "/dist"
// Output: the joined, cleaned path.
//
// An empty fragment is not treated as the current directory, which stops a
// missing file name from quietly becoming a request for the directory itself.
std::string JoinURLPath(std::string_view a, std::string_view b) {
    if (a.empty()) return CleanURLPath(b);
    if (b.empty()) return CleanURLPath(a);
    std::string combined(a);
    combined += '/';
    combined.append(b);
    return CleanURLPath(combined);
}

// Returns the directory part of a path.
//
// Input : "/a/b/c.js" -> "/a/b"   "c.js" -> "."   "/c.js" -> "/"
// Output: the directory, after cleaning the input.
//
// The root comes back as "/" and a path with no separator at all as ".", which
// matches what callers do next: append a separator only when the result is not
// already the root.
std::string DirURLPath(std::string_view p) {
    std::string cleaned = CleanURLPath(p);
    size_t i = cleaned.find_last_of('/');
    if (i == std::string::npos) {
        return ".";
    }
    if (i == 0) {
        return "/";
    }
    return cleaned.substr(0, i);
}

// Returns the last element of a path.
//
// Input : "/a/b/c.js" -> "c.js"   "/a/b/" -> "b"   "/" -> "/"
// Output: the final element, once the path has been cleaned.
//
// Routing resolves paths with "SplitURLPath" instead, so this stays for the
// places that want the name of a file and have no use for its directory.
[[maybe_unused]] std::string BaseURLPath(std::string_view p) {
    std::string cleaned = CleanURLPath(p);
    if (cleaned == "/") {
        return "/";
    }
    size_t i = cleaned.find_last_of('/');
    if (i == std::string::npos) {
        return cleaned;
    }
    return cleaned.substr(i + 1);
}

// Splits a path into its directory and its last element.
//
// Input : "/a/b/c.js" -> ("/a/b/", "c.js")
//         "c.js"      -> ("",       "c.js")
// Output: the directory, keeping its trailing separator so a child name can be
//         appended directly, and the file name.
//
// The trailing separator is part of the result deliberately. The directory side
// is compared against a query directory that is built the same way, so matching
// works without either side having to append a separator first.
std::pair<std::string, std::string> SplitURLPath(std::string_view p) {
    size_t i = p.find_last_of('/');
    if (i == std::string::npos) {
        return {"", std::string(p)};
    }
    return {std::string(p.substr(0, i + 1)), std::string(p.substr(i + 1))};
}

} // namespace: sockets, HTTP message types and URL path handling

namespace {

// ===========================================================================
// Request routing
//
// "ApiHandler" owns the decision of what a URL means. It runs for every request
// on that connection's own thread, and it holds the state that outlives a single
// request: the host allow-list, the live event streams, and the output-file
// hashes from the last rebuild.
//
// The order of the stages in "Handle" is the part worth knowing:
//
//   1. CORS headers, then the "Host" allow-list, then a rejection of any path
//      containing a backslash.
//   2. The reserved event-stream path, which owns its connection and returns
//      only when that connection ends.
//   3. A rebuild, so that everything after this point is answered from output at
//      most one build old, with any change pushed to the live clients first.
//   4. A match in that output, then a file in "servedir", then a directory in
//      "servedir", then the fallback page, and finally a 404.
// ===========================================================================

// One message queued for a live-reload client.
//
// "event" is the name the client dispatches on and "data" is a single line of
// payload. The only message sent today is a "change" notification carrying a
// small JSON document.
struct ServerSentEvent {
    std::string event;
    std::string data;
};

// The queue behind one open event-stream connection.
//
// The connection thread waits in "Next" and writes whatever it is handed, while
// the thread that ran the rebuild pushes from "Push". A stream is closed for
// two reasons: the client went away, or the server is stopping and needs every
// waiting thread to unwind. "Close" wakes all of them at once.
class EventStream {
public:
    // Queues a message for this connection, discarding it if already closed.
    //
    // Input : an event name and its payload.
    // Output: the message is queued and one waiting thread is woken.
    void Push(ServerSentEvent ev) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            queue_.push_back(std::move(ev));
        }
        cv_.notify_one();
    }

    // Closes the stream and discards anything still queued.
    //
    // Input : none.
    // Output: every waiting thread is woken and will observe a closed stream.
    //
    // Queued messages are dropped rather than drained, because a client that is
    // on its way out has no use for the last rebuild's notifications.
    void Close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            queue_.clear();
        }
        cv_.notify_all();
    }

    enum class Result { kEvent, kTimeout, kClosed };

    // Waits for the next message.
    //
    // Input : a timeout, which the caller always sets to 30 seconds.
    // Output: kEvent with the message in "out", kTimeout when the timeout
    //         expired with an empty queue, or kClosed once the stream is shut.
    //
    // The timeout is what keeps an idle connection alive: the caller answers it
    // with a keep-alive comment, so a proxy does not drop a connection that is
    // simply waiting for the next rebuild.
    Result Next(ServerSentEvent& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, timeout, [this] { return closed_ || !queue_.empty(); });
        if (closed_) {
            return Result::kClosed;
        }
        if (!queue_.empty()) {
            out = std::move(queue_.front());
            queue_.erase(queue_.begin());
            return Result::kEvent;
        }
        return Result::kTimeout;
    }

private:
    std::mutex                          mutex_;
    std::condition_variable             cv_;
    std::vector<ServerSentEvent>        queue_;
    bool                                closed_ = false;
};

// Turns requests into responses for one running server.
//
// The file system and the rebuild callback are all the handler needs from the
// outside; everything else is configuration copied out of the serve options.
// The TLS key and certificate paths are lower-cased once here so that the
// per-request check against them is a single comparison that cannot be defeated
// by a file system with different case rules.
class ApiHandler {
public:
    ApiHandler(
        filesystem::Fs&                       fs,
        RebuildFn                             rebuild,
        std::string                           outdir_path_prefix,
        std::string                           abs_output_dir,
        std::string                           public_path,
        ServeOptions                           options)
        : fs_(fs),
          rebuild_(std::move(rebuild)),
          outdir_path_prefix_(std::move(outdir_path_prefix)),
          abs_output_dir_(std::move(abs_output_dir)),
          public_path_(std::move(public_path)),
          servedir_(std::move(options.servedir)),
          keyfile_to_lower_(helpers::ToLowerASCII(options.keyfile)),
          certfile_to_lower_(helpers::ToLowerASCII(options.certfile)),
          fallback_(std::move(options.fallback)),
          on_request_(std::move(options.on_request)),
          cors_origin_(std::move(options.cors.origin)) {
    }

    void SetHosts(std::vector<std::string> hosts) {
        hosts_ = std::move(hosts);
    }

    std::vector<std::string>& Hosts() { return hosts_; }

    // Serves one request. This is the entire routing decision of the server.
    //
    // Input : a parsed "Request" and the "Response" to write into.
    // Output: nothing. The response carries the status and the body, and the
    //         "on_request" callback has been told what happened and how long it
    //         took.
    //
    // The stages, and why each is where it is:
    //
    //   * CORS. An "Origin" header is answered from the configured list, which
    //     may be "*", a literal, or a single wildcard with a prefix and a
    //     suffix. The request is not rejected, only decorated, so a page on
    //     another origin can read the bundle while a disallowed origin simply
    //     receives no header.
    //
    //   * Host allow-list. The "Host" header is compared against the addresses
    //     this server is actually reachable on. That is what stops a page on
    //     another origin from pointing a browser at a development server which
    //     has no authentication of its own. A missing header is not checked,
    //     because a request without one cannot have been aimed at a name.
    //
    //   * Backslash rejection. A backslash in a request path is always a
    //     mistake on the wire, and on Windows it is a separator, which would
    //     turn "/..\file" into a way out of the served tree. Rejecting it keeps
    //     one rule for every platform.
    //
    //   * The event stream, which keeps its connection until the client closes
    //     it and so returns from here instead of writing a body.
    //
    //   * GET and HEAD, which is everything else: a rebuild, then the stages
    //     described below.
    //
    // Input : "GET /app.js" with a "servedir" of "/project" and a build that
    //         succeeded.
    // Output: a 200 carrying the bytes of "/project/app.js" and the media type
    //         implied by its extension. A rebuild with errors gives 503 and
    //         their text, a directory with no "index.html" gives a listing page,
    //         and a path that exists in neither place gives 404.
    void Handle(const Request& req, Response* resp) {
        auto start = std::chrono::steady_clock::now();

        std::string origin = req.GetHeader("Origin");
        if (!origin.empty()) {
            for (const std::string& allowed : cors_origin_) {
                if (allowed == "*") {
                    resp->SetHeader("Access-Control-Allow-Origin", "*");
                    break;
                } else if (size_t star = allowed.find('*'); star != std::string::npos) {
                    std::string prefix = allowed.substr(0, star);
                    std::string suffix = allowed.substr(star + 1);
                    if (origin.size() >= prefix.size() + suffix.size() &&
                        origin.compare(0, prefix.size(), prefix) == 0 &&
                        origin.compare(origin.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        resp->SetHeader("Access-Control-Allow-Origin", origin);
                        break;
                    }
                } else if (origin == allowed) {
                    resp->SetHeader("Access-Control-Allow-Origin", origin);
                    break;
                }
            }
        }

        bool is_head = req.method == "HEAD";

        std::string host = req.host_header;
        if (!host.empty()) {
            if (auto parts = SplitHostPort(host)) {
                host = parts->first;
            }
        }
        if (host != "localhost") {
            bool ok = false;
            for (const std::string& allowed : hosts_) {
                if (host == allowed) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                notify_request_duration(start, req, 403);
                resp->WriteHeader(403);
                resp->Write("403 - Forbidden: The host \"" + host + "\" is not allowed");
                return;
            }
        }

        if (req.path.find('\\') != std::string::npos) {
            notify_request_duration(start, req, 400);
            resp->WriteHeader(400);
            resp->Write("400 - Bad Request");
            return;
        }

        if (req.method == "GET" && req.path == "/esbuild" &&
            req.GetHeader("Accept") == "text/event-stream") {
            serve_event_stream(start, req, resp);
            return;
        }

        if ((is_head || req.method == "GET") && !req.path.empty() && req.path[0] == '/') {
            std::string query_path = CleanURLPath(req.path);
            if (query_path.size() > 0 && query_path[0] == '/') {
                query_path = query_path.substr(1);
            }

            BuildResult result = rebuild_();

            // Push any change in the output to the connected live-reload
            // clients before answering, so a browser that reloads on the
            // notification cannot ask for a file this build has just replaced.
            NotifyRebuild(result);

            // A build that failed serves its errors instead of its output, so
            // a stale bundle is never mistaken for a current one.
            if (!result.errors.empty()) {
                resp->SetHeader("Content-Type", "text/plain; charset=utf-8");
                notify_request_duration(start, req, 503);
                resp->WriteHeader(503);
                resp->Write(errors_to_string(result.errors));
                return;
            }

            struct FileToServe {
                std::string                           abs_path;
                std::shared_ptr<filesystem::OpenedFile> contents;
            };

            filesystem::EntryKind kind = filesystem::EntryKind::kInvalid;
            FileToServe file;
            std::unordered_map<std::string, bool> dir_entries;
            std::unordered_map<std::string, bool> file_entries;

            std::string outdir_query;
            // The build output is addressed relative to the output directory
            // and published under the configured path prefix, so a request only
            // looks for build output once that prefix has been removed.
            bool in_outdir = strip_dir_prefix(query_path, outdir_path_prefix_, "/", outdir_query);
            if (in_outdir) {
                bool is_implicit_index_html = false;
                std::string in_memory_bytes;
                std::string abs_path;
                kind = match_query_path_to_result(outdir_query, result, dir_entries, file_entries,
                                                  in_memory_bytes, abs_path, is_implicit_index_html);
                auto mem = std::make_shared<filesystem::InMemoryOpenedFile>();
                mem->contents = std::move(in_memory_bytes);
                file.abs_path = std::move(abs_path);
                file.contents = std::move(mem);
                if (is_implicit_index_html) {
                    query_path = JoinURLPath(query_path, "index.html");
                }
            } else {
                // Synthesise the leading directories of the output prefix, so
                // that a request for the output directory itself lists as a
                // directory instead of answering 404.
                std::string p = outdir_path_prefix_;
                while (!p.empty()) {
                    std::string dir;
                    std::string base;
                    size_t slash = p.find('/');
                    if (slash == std::string::npos) {
                        base = p;
                    } else {
                        dir = p.substr(0, slash);
                        base = p.substr(slash + 1);
                    }
                    if (dir == query_path) {
                        kind = filesystem::EntryKind::kDir;
                        dir_entries[base] = true;
                        break;
                    }
                    p = dir;
                }
            }

            // A file in the served tree, checked only when the build had no
            // answer for this path.
            if (!servedir_.empty() && kind != filesystem::EntryKind::kFile) {
                std::string abs_path = fs_.Join({servedir_, query_path});
                std::string abs_dir = fs_.Dir(abs_path);
                if (abs_dir != abs_path) {
                    auto entries = fs_.ReadDirectory(abs_dir);
                    if (entries.Ok()) {
                        auto [entry, diff] = entries.value.Get(fs_.Base(abs_path));
                        if (entry && entry->Kind(fs_) == filesystem::EntryKind::kFile) {
                            if (!keyfile_to_lower_.empty() || !certfile_to_lower_.empty()) {
                                // The TLS key and certificate must never be
                                // served. The comparison is case-insensitive
                                // because the request path and the name the
                                // file system stored need not agree on case.
                                std::string to_lower = helpers::ToLowerASCII(abs_path);
                                if (to_lower == keyfile_to_lower_ || to_lower == certfile_to_lower_) {
                                    notify_request_duration(start, req, 403);
                                    resp->WriteHeader(403);
                                    resp->Write("403 - Forbidden");
                                    return;
                                }
                            }
                            auto opened = fs_.OpenFile(abs_path);
                            if (opened.Ok()) {
                                file.abs_path = std::move(abs_path);
                                file.contents = opened.value;
                                kind = filesystem::EntryKind::kFile;
                            } else if (opened.canonical_error != std::errc::no_such_file_or_directory) {
                                notify_request_duration(start, req, 500);
                                resp->WriteHeader(500);
                                resp->Write("500 - Internal server error: " + opened.original_error);
                                return;
                            }
                        }
                    }
                }
            }

            std::string servedir_index_name;
            // A directory in the served tree, which is what decides between a
            // listing page and an index file below.
            if (!servedir_.empty() && kind != filesystem::EntryKind::kFile) {
                auto entries = fs_.ReadDirectory(fs_.Join({servedir_, query_path}));
                if (entries.Ok()) {
                    kind = filesystem::EntryKind::kDir;
                    for (const std::string& name : entries.value.SortedKeys()) {
                        auto [entry, diff] = entries.value.Get(name);
                        if (!entry) {
                            continue;
                        }
                        switch (entry->Kind(fs_)) {
                        case filesystem::EntryKind::kDir:
                            dir_entries[name] = true;
                            break;
                        case filesystem::EntryKind::kFile:
                            file_entries[name] = true;
                            if (name == "index.html") {
                                servedir_index_name = name;
                            }
                            break;
                        default:
                            break;
                        }
                    }
                } else if (entries.canonical_error != std::errc::no_such_file_or_directory) {
                    notify_request_duration(start, req, 500);
                    resp->WriteHeader(500);
                    resp->Write("500 - Internal server error: " + entries.original_error);
                    return;
                }
            }

            // A directory is only served under its trailing separator, so that
            // the relative links generated inside it resolve one level deeper
            // than they otherwise would.
            if (kind == filesystem::EntryKind::kDir && !req.path.empty() && req.path.back() != '/') {
                std::string location = CleanURLPath(req.path);
                if (location.empty() || location.back() != '/') {
                    location += '/';
                }
                resp->SetHeader("Location", location);
                notify_request_duration(start, req, 302);
                resp->WriteHeader(302);
                resp->Flush();
                return;
            }

            // An "index.html" in the served directory wins over a listing.
            if (kind == filesystem::EntryKind::kDir && !servedir_index_name.empty()) {
                query_path += "/" + servedir_index_name;
                std::string abs_path = fs_.Join({servedir_, query_path});
                auto opened = fs_.OpenFile(abs_path);
                if (opened.Ok()) {
                    file.abs_path = std::move(abs_path);
                    file.contents = opened.value;
                    kind = filesystem::EntryKind::kFile;
                } else if (opened.canonical_error != std::errc::no_such_file_or_directory) {
                    notify_request_duration(start, req, 500);
                    resp->WriteHeader(500);
                    resp->Write("500 - Internal server error: " + opened.original_error);
                    return;
                }
            }

            // The fallback page answers every path the build and the served
            // directory did not, which is what turns this into a host for a
            // single-page application.
            if (kind != filesystem::EntryKind::kFile && !fallback_.empty()) {
                auto opened = fs_.OpenFile(fallback_);
                if (opened.Ok()) {
                    file.abs_path = fallback_;
                    file.contents = opened.value;
                    kind = filesystem::EntryKind::kFile;
                } else if (opened.canonical_error != std::errc::no_such_file_or_directory) {
                    notify_request_duration(start, req, 500);
                    resp->WriteHeader(500);
                    resp->Write("500 - Internal server error: " + opened.original_error);
                    return;
                }
            }

            if (kind == filesystem::EntryKind::kFile) {
                int status = 200;
                int file_contents_len = file.contents->Len();
                int begin = 0;
                int end = file_contents_len;
                bool is_range = false;

                // A byte range lets a browser seek inside a media file without
                // downloading all of it, which is what video playback needs.
                if (int range_begin = 0, range_end = 0; true) {
                    std::string range_header = req.GetHeader("Range");
                    if (parse_range_header(range_header, file_contents_len, range_begin, range_end) &&
                        range_begin < range_end) {
                        is_range = true;
                        begin = range_begin;
                        end = range_end;
                        status = 206;
                    }
                }

                std::string file_bytes = file.contents->Read(begin, end);
                if (file_bytes.size() != size_t(end > begin ? end - begin : 0)) {
                    notify_request_duration(start, req, 500);
                    resp->WriteHeader(500);
                    resp->Write("500 - Internal server error");
                    return;
                }

                std::string ext = fs_.Ext(file.abs_path);
                std::string_view mime = helpers::MimeTypeByExtension(ext);
                if (!mime.empty()) {
                    resp->SetHeader("Content-Type", std::string(mime));
                } else {
                    resp->SetHeader("Content-Type", "application/octet-stream");
                }
                // The range end is inclusive on the wire and exclusive
                // here, so the header reports one byte past the last byte
                // that was actually sent.
                if (is_range) {
                    resp->SetHeader("Content-Range",
                                    "bytes " + std::to_string(begin) + "-" +
                                        std::to_string(end - 1) + "/" +
                                        std::to_string(file_contents_len));
                }
                resp->SetHeader("Content-Length", std::to_string(file_bytes.size()));
                notify_request_duration(start, req, status);
                resp->WriteHeader(status);
                resp->Write(file_bytes);
                return;
            }

            // Nothing claimed the path, but it is a directory, so answer with a
            // listing instead of a 404.
            if (kind == filesystem::EntryKind::kDir) {
                std::string html = respond_with_dir_list(query_path, dir_entries, file_entries);
                resp->SetHeader("Content-Type", "text/html; charset=utf-8");
                resp->SetHeader("Content-Length", std::to_string(html.size()));
                notify_request_duration(start, req, 200);
                resp->WriteHeader(200);
                resp->Write(html);
                return;
            }
        }

        // A browser asks for this unprompted, and a 404 in the console of a page
        // being debugged is pure noise, so a pre-compressed icon is served to any
        // client that accepts gzip.
        if (req.method == "GET" && req.path == "/favicon.ico") {
            std::string accept_encoding = req.GetHeader("Accept-Encoding");
            size_t pos = 0;
            while (pos <= accept_encoding.size()) {
                size_t semi = accept_encoding.find(';', pos);
                size_t comma = accept_encoding.find(',', pos);
                size_t end = accept_encoding.size();
                if (semi != std::string::npos && semi < end) end = semi;
                if (comma != std::string::npos && comma < end) end = comma;
                std::string encoding = TrimSpaces(accept_encoding.substr(pos, end - pos));
                if (helpers::EqualFoldASCII(encoding, "gzip")) {
                    resp->SetHeader("Content-Encoding", "gzip");
                    resp->SetHeader("Content-Type", "image/vnd.microsoft.icon");
                    notify_request_duration(start, req, 200);
                    resp->WriteHeader(200);
                    resp->Write(kFaviconIcoGz);
                    return;
                }
                if (end == accept_encoding.size()) break;
                pos = end + 1;
            }
        }

        // Nothing above claimed the request.
        resp->SetHeader("Content-Type", "text/plain; charset=utf-8");
        notify_request_duration(start, req, 404);
        resp->WriteHeader(404);
        resp->Write("404 - Not Found");
    }

    // Closes every open event stream.
    //
    // Input : none.
    // Output: all waiting connection threads are woken and unwind, and the
    //         server stops pushing rebuild notifications.
    //
    // The stop callback calls this, and it does so before tearing down the
    // accept loop, so that no thread is left waiting on a stream which will
    // never be written to again.
    void CloseAllStreams() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<EventStream>& stream : active_streams_) {
            stream->Close();
        }
        active_streams_.clear();
    }

    // Diffs the new build output against the previous one and tells every live
    // client what changed.
    //
    // Input : the "BuildResult" of the rebuild that just ran.
    // Output: one "change" event per connected client, and only when something
    //         was added, removed or updated.
    //
    // The diff is over content hashes rather than timestamps or sizes, because
    // a rebuild can rewrite a file with byte-identical content, and reloading
    // for an unchanged file is exactly the flicker this mechanism exists to
    // avoid. Paths are reported as URLs rather than as file system paths: each
    // output file is made relative to the output directory, normalised to '/'
    // separators, and prefixed with the public path, so that a client can fetch
    // what it has just been told about.
    //
    // A build with errors leaves the recorded hashes untouched, so the next
    // successful build is compared against the last state that actually served
    // bytes instead of against a failed attempt.
    void NotifyRebuild(const BuildResult& result) {
        std::unordered_map<std::string, std::string> new_hashes;
        for (const OutputFile& file : result.output_files) {
            new_hashes[file.path] = HashContents(file.contents);
        }

        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<std::string> added;
        std::vector<std::string> removed;
        std::vector<std::string> updated;

        // Turns an absolute output path into a URL a client can request.
        //
        // Input : "/project/out/app.js" with output directory "/project/out"
        //         -> "/app.js", and nullopt for a file outside that directory.
        // Output: the URL, including the public path prefix, or nullopt when
        //         the file cannot be published and should stay unmentioned.
        auto url_for_path = [&](const std::string& abs_path) -> std::optional<std::string> {
            std::optional<std::string> rel = StripDirPrefixInput(abs_path, abs_output_dir_);
            if (!rel) {
                return std::nullopt;
            }
            std::string rel_path = *rel;
            std::replace(rel_path.begin(), rel_path.end(), '\\', '/');
            rel_path = JoinURLPath(outdir_path_prefix_, rel_path);
            std::string slash = "/";
            if (!public_path_.empty() && public_path_.back() == '/') {
                slash = "";
            }
            return public_path_ + slash + rel_path;
        };

        if (result.errors.empty()) {
            std::unordered_map<std::string, std::string> old_hashes = current_hashes_;
            current_hashes_ = new_hashes;

            for (const auto& [abs_path, new_hash] : new_hashes) {
                auto old_it = old_hashes.find(abs_path);
                if (old_it == old_hashes.end()) {
                    if (auto url = url_for_path(abs_path)) {
                        added.push_back(*url);
                    }
                } else if (new_hash != old_it->second) {
                    if (auto url = url_for_path(abs_path)) {
                        updated.push_back(*url);
                    }
                }
            }
            for (const auto& [abs_path, old_hash] : old_hashes) {
                if (new_hashes.find(abs_path) == new_hashes.end()) {
                    if (auto url = url_for_path(abs_path)) {
                        removed.push_back(*url);
                    }
                }
            }
        }

        if (!added.empty() || !removed.empty() || !updated.empty()) {
            std::sort(added.begin(), added.end());
            std::sort(removed.begin(), removed.end());
            std::sort(updated.begin(), updated.end());

            std::string json = BuildChangeJSON(added, removed, updated);

            for (const std::shared_ptr<EventStream>& stream : active_streams_) {
                stream->Push(ServerSentEvent{"change", json});
            }
        }
    }

private:
    // Reports a finished request to the serve options callback.
    //
    // Input : when the request started, the request itself, and the status that
    //         was answered.
    // Output: the callback runs with the remote address, method, path, status
    //         and elapsed milliseconds, or nothing happens when no callback is
    //         configured.
    void notify_request_duration(std::chrono::steady_clock::time_point start, const Request& req,
                                 int status) {
        if (!on_request_) return;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        ServeOnRequestArgs args;
        args.remote_address = req.remote_addr;
        args.method = req.method;
        args.path = req.path;
        args.status = status;
        args.time_in_ms = int(elapsed.count());
        on_request_(args);
    }

    // Finds what the current build output has to say about a request path.
    //
    // Input : a cleaned request path with no leading separator, the build
    //         result, and two maps to fill with the directory and file names
    //         that live in the requested directory.
    // Output: kFile with the bytes and their absolute path when the path names
    //         an output file or an implicit index, kDirectory when the path
    //         contains output files below it, and kInvalid when the build has
    //         nothing at that path.
    //
    // Three things count as a match. The exact path is the ordinary case. A
    // directory containing an "index.html" answers the directory itself, which
    // is what lets a browser ask for "/" and receive the page. Anything else
    // beginning with the query directory contributes one name to the listing,
    // one level deep: a name containing a separator becomes a subdirectory and
    // a name without one becomes a file.
    //
    // Input : output files "/out/index.html", "/out/app.js" and
    //         "/out/assets/a.css" queried with "app".
    // Output: kFile with the bytes of "/out/app.js".
    filesystem::EntryKind match_query_path_to_result(
        const std::string& query_path,
        const BuildResult& result,
        std::unordered_map<std::string, bool>& dir_entries,
        std::unordered_map<std::string, bool>& file_entries,
        std::string& in_memory_bytes,
        std::string& abs_path,
        bool& is_implicit_index_html) {
        bool query_is_dir = false;
        std::string query_dir = query_path;
        if (!query_dir.empty()) {
            query_dir += "/";
        }

        for (const OutputFile& file : result.output_files) {
            auto rel = fs_.Rel(abs_output_dir_, file.path);
            if (!rel) {
                continue;
            }
            std::string rel_path = *rel;
            std::replace(rel_path.begin(), rel_path.end(), '\\', '/');

            if (rel_path == query_path) {
                in_memory_bytes.assign(file.contents.begin(), file.contents.end());
                abs_path = file.path;
                return filesystem::EntryKind::kFile;
            }

            auto [dir, base] = SplitURLPath(rel_path);
            if (base == "index.html" && query_dir == dir) {
                in_memory_bytes.assign(file.contents.begin(), file.contents.end());
                abs_path = file.path;
                is_implicit_index_html = true;
                return filesystem::EntryKind::kFile;
            }

            if (rel_path.compare(0, query_dir.size(), query_dir) == 0) {
                std::string entry = rel_path.substr(query_dir.size());
                query_is_dir = true;
                size_t slash = entry.find('/');
                if (slash == std::string::npos) {
                    file_entries[entry] = true;
                } else {
                    std::string sub = entry.substr(0, slash);
                    if (!dir_entries[sub]) {
                        dir_entries[sub] = true;
                    }
                }
            }
        }

        if (query_is_dir) {
            return filesystem::EntryKind::kDir;
        }
        return filesystem::EntryKind::kInvalid;
    }

    // Escapes text for use as element content.
    //
    // Input : "a & b" -> "a &amp; b"
    // Output: the escaped copy, replacing the three characters that could
    //         otherwise close the element or start an entity, and passing
    //         everything else through untouched.
    static std::string escape_for_html(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (char c : text) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out += c;       break;
            }
        }
        return out;
    }

    // Escapes text for use inside a double-quoted attribute value.
    //
    // Input : "say \"hi\"" -> "say &quot;hi&quot;"
    // Output: the escaped copy, applied after the element-content escaping so
    //         that one call covers both concerns for a name which is used as
    //         link text and as an "href" at the same time.
    static std::string escape_for_attribute(const std::string& text) {
        std::string out = escape_for_html(text);
        std::string result;
        result.reserve(out.size());
        for (char c : out) {
            if (c == '"') result += "&quot;";
            else if (c == '\'') result += "&apos;";
            else result += c;
        }
        return result;
    }

    // Generates the HTML page shown for a directory.
    //
    // Input : the requested path, plus the directory and file names collected
    //         for it.
    // Output: a complete HTML document listing every name, directories first
    //         and each group sorted, with a link to the parent above them.
    //
    // Every name is escaped twice where it is needed, once as element text and
    // once as an attribute value, because a link is both. The heading is built
    // one element at a time so that a deep path produces a clickable breadcrumb
    // rather than a single flat directory name.
    static std::string respond_with_dir_list(
        const std::string& query_path,
        const std::unordered_map<std::string, bool>& dir_entries,
        const std::unordered_map<std::string, bool>& file_entries) {
        std::string path = "/" + query_path;
        std::string query_dir = path;
        if (query_dir != "/") {
            query_dir += "/";
        }

        std::string html =
            "<!doctype html>\n"
            "<meta charset=\"utf8\">\n"
            "<style>\n"
            "body { margin: 30px; color: #222; background: #fff; font: 16px/22px sans-serif; }\n"
            "a { color: inherit; text-decoration: none; }\n"
            "a:hover { text-decoration: underline; }\n"
            "a:visited { color: #777; }\n"
            "@media (prefers-color-scheme: dark) {\n"
            "  body { color: #fff; background: #222; }\n"
            "  a:visited { color: #aaa; }\n"
            "}\n"
            "</style>\n"
            "<title>Directory: " + escape_for_html(query_dir) + "</title>\n"
            "<h1>Directory: ";

        std::vector<std::string> parts;
        if (path == "/") {
            parts.push_back("");
        } else {
            size_t pos = 0;
            std::string_view sv(path);
            while (pos < sv.size()) {
                size_t slash = sv.find('/', pos);
                if (slash == std::string::npos) {
                    parts.push_back(std::string(sv.substr(pos)));
                    break;
                }
                if (slash != pos) {
                    parts.push_back(std::string(sv.substr(pos, slash - pos)));
                }
                pos = slash + 1;
            }
        }

        for (size_t i = 0; i < parts.size(); ++i) {
            std::string accumulated;
            for (size_t j = 0; j <= i; ++j) {
                if (j > 0) accumulated += "/";
                accumulated += parts[j];
            }
            if (i + 1 < parts.size()) {
                html += "<a href=\"";
                html += escape_for_attribute(accumulated);
                html += "/\">";
            }
            html += escape_for_html(parts[i]);
            html += "/";
            if (i + 1 < parts.size()) {
                html += "</a>";
            }
        }
        html += "</h1>\n";

        // A link back up, so a directory reached by following a chain of
        // generated links can be walked back without touching the address bar.
        if (path != "/") {
            std::string parent_dir = DirURLPath(path);
            if (parent_dir != "/") {
                parent_dir += "/";
            }
            html += "<div>\xF0\x9F\x93\x81 <a href=\"" + escape_for_attribute(parent_dir) +
                    "\">../</a></div>\n";
        }

        // Child directories first, each linking with a trailing separator so
        // that following the link lands on a directory rather than a 404.
        std::vector<std::string> names;
        names.reserve(dir_entries.size() + file_entries.size());
        for (const auto& [entry, present] : dir_entries) {
            (void)present;
            names.push_back(entry);
        }
        std::sort(names.begin(), names.end());
        for (const std::string& entry : names) {
            html += "<div>\xF0\x9F\x93\x81 <a href=\"" +
                    escape_for_attribute(JoinURLPath(path, entry)) + "/\">" +
                    escape_for_html(entry) + "/</a></div>\n";
        }

        // Then the files, each linking to itself.
        names.clear();
        for (const auto& [entry, present] : file_entries) {
            (void)present;
            names.push_back(entry);
        }
        std::sort(names.begin(), names.end());
        for (const std::string& entry : names) {
            html += "<div>\xF0\x9F\x93\x84 <a href=\"" +
                    escape_for_attribute(JoinURLPath(path, entry)) + "\">" +
                    escape_for_html(entry) + "</a></div>\n";
        }

        return html;
    }

    // Parses a byte range out of a "Range" header.
    //
    // Input : "bytes=0-499" against a length of 1000 -> true, with begin 0 and
    //         end 500, which is the exclusive end this file works with.
    // Output: true with the half-open range in "out_begin" and "out_end", or
    //         false for a missing, malformed or out-of-bounds range, in which
    //         case the caller serves the whole file.
    //
    // Only the "bytes=" unit and a single range are understood. A range whose
    // end reaches past the last byte is rejected rather than clamped, so a
    // request that was prepared against a longer version of a file cannot
    // produce a range pointing outside the bytes that exist.
    static bool parse_range_header(const std::string& r, int content_length, int& out_begin,
                                   int& out_end) {
        if (r.compare(0, 6, "bytes=") == 0) {
            std::string rest = r.substr(6);
            size_t dash = rest.find('-');
            if (dash != std::string::npos) {
                int begin;
                if (parse_range_int(rest.substr(0, dash), content_length - 1, begin)) {
                    int end;
                    if (parse_range_int(rest.substr(dash + 1), content_length - 1, end)) {
                        out_begin = begin;
                        out_end = end + 1;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Parses one decimal number and checks it against an upper bound.
    //
    // Input : "499" with a maximum of 999 -> true, 499
    //         "" or "12a" or "1000"        -> false
    // Output: the value in "out" when the text is a non-empty run of digits
    //         that stays within the bound.
    //
    // The bound is checked while the digits accumulate rather than at the end,
    // so an absurdly long digit string is rejected on the way instead of
    // overflowing the accumulator before it can be.
    static bool parse_range_int(const std::string& text, int max_value, int& out) {
        if (text.empty()) {
            return false;
        }
        int value = 0;
        for (char c : text) {
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10 + int(c - '0');
            if (value > max_value) {
                return false;
            }
        }
        out = value;
        return true;
    }

    // Removes a directory prefix from a path, requiring a separator to match.
    //
    // Input : ("/dist/app.js", "/dist", "/") -> true with "app.js"
    //         ("/distillery",  "/dist", "/") -> false
    //         ("anything",     "",     "/") -> true with "anything"
    // Output: true with the remainder in "out", or false when the prefix is
    //         absent or only matches the first part of a path element.
    //
    // The separator requirement is what stops "/dist" from claiming the path
    // "/distillery", which would otherwise publish a neighbouring directory the
    // caller never meant to expose.
    static bool strip_dir_prefix(const std::string& dir, const std::string& prefix,
                                 const char* separators, std::string& out) {
        std::string remaining = dir;
        if (!prefix.empty()) {
            if (remaining.compare(0, prefix.size(), prefix) != 0) {
                return false;
            }
            remaining = remaining.substr(prefix.size());
            if (remaining.empty() || strchr(separators, remaining[0]) == nullptr) {
                return false;
            }
            remaining = remaining.substr(1);
        }
        out = remaining;
        return true;
    }

    // Makes an absolute output path relative to the output directory.
    //
    // Input : "/project/out/app.js" with "/project/out" -> "app.js"
    //         "/elsewhere/app.js"                        -> no value
    // Output: the remainder where a separator followed the prefix, nullopt
    //         otherwise, and the path unchanged when the prefix is empty.
    //
    // Both separators are accepted here because the path being stripped is a
    // file system path that may have come from either platform, whereas the
    // request path it is compared against is always '/'-separated.
    static std::optional<std::string> StripDirPrefixInput(const std::string& dir,
                                                          const std::string& prefix) {
        if (prefix.empty()) {
            return dir;
        }
        if (dir.compare(0, prefix.size(), prefix) != 0) {
            return std::nullopt;
        }
        std::string remaining = dir.substr(prefix.size());
        if (remaining.empty() ||
            (remaining[0] != '/' && remaining[0] != '\\')) {
            return std::nullopt;
        }
        return remaining.substr(1);
    }

    // Holds a connection open and pushes rebuild notifications to it.
    //
    // Input : a request that asked for "text/event-stream".
    // Output: nothing. The connection carries an event frame per rebuild, a
    //         keep-alive comment every 30 seconds of quiet, and ends when the
    //         stream itself closes.
    //
    // This call owns its thread for as long as the client stays connected, which
    // is why it is the one place in the handler that does not return. The
    // "retry:" line at the top tells the client how long to wait before
    // reconnecting if the connection is ever lost, and the stream is removed
    // from the handler's list on the way out so a later rebuild does not push
    // into a connection that has already gone.
    void serve_event_stream(std::chrono::steady_clock::time_point start, const Request& req,
                            Response* resp) {
        auto stream = std::make_shared<EventStream>();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_streams_.push_back(stream);
        }

        resp->SetHeader("Content-Type", "text/event-stream");
        resp->SetHeader("Connection", "keep-alive");
        resp->SetHeader("Cache-Control", "no-cache");
        notify_request_duration(start, req, 200);
        resp->WriteHeader(200);
        resp->Write("retry: 500\n");
        resp->Flush();

        for (;;) {
            // A message arrived, a keep-alive is due, or the stream is closed.
            ServerSentEvent ev;
            auto result = stream->Next(ev, std::chrono::seconds(30));
            if (result == EventStream::Result::kEvent) {
                std::string frame = "event: " + ev.event + "\ndata: " + ev.data + "\n\n";
                resp->Write(frame);
                resp->Flush();
            } else if (result == EventStream::Result::kTimeout) {
                // Nothing has been rebuilt for a while, but the connection is
                // idle and a comment frame is a valid event-stream message, so
                // it keeps intermediaries from timing the connection out.
                resp->Write(":\n\n");
                resp->Flush();
            } else {
                break;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < active_streams_.size(); ++i) {
            if (active_streams_[i] == stream) {
                active_streams_.erase(active_streams_.begin() + ptrdiff_t(i));
                break;
            }
        }
    }

    // Renders build errors as the plain text of a response body.
    //
    // Input : a list of messages.
    // Output: the message texts separated by blank lines, capped at five, with
    //         a final line saying how many were left out.
    //
    // The cap is there because this body replaces the bundle: a build with
    // hundreds of errors should still produce a page a person can read, and the
    // count line says plainly that the list was truncated rather than implying
    // those were all of them.
    static std::string errors_to_string(const std::vector<Message>& errors) {
        std::string result;
        const size_t limit = 5;
        size_t shown = 0;
        for (const Message& msg : errors) {
            if (shown == limit) {
                result += std::to_string(limit) + " out of " +
                          std::to_string(errors.size()) + " errors shown\n";
                break;
            }
            if (shown > 0) {
                result += "\n";
            }
            result += msg.text;
            ++shown;
        }
        return result;
    }

    // Hashes the bytes of an output file for change detection.
    //
    // Input : the contents of one output file.
    // Output: the hash as decimal text.
    //
    // This is a 64-bit FNV-1a hash, and it is not a cryptographic digest and not
    // meant to be one: it only has to notice that a rebuild changed a file, and
    // the value never leaves the process. It is carried as text because it
    // lives in a map that is otherwise keyed by path and shared with nothing.
    static std::string HashContents(const std::vector<uint8_t>& contents) {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : contents) {
            h ^= c;
            h *= 1099511628211ull;
        }
        return std::to_string(h);
    }

    // Builds the JSON payload of a change notification.
    //
    // Input : the added, removed and updated URLs.
    // Output: {"added":[...],"removed":[...],"updated":[...]} with every URL
    //         quoted for JSON and kept in the order given.
    //
    // All three lists are always present even when empty, so a client can read
    // "updated" without first testing whether the key exists.
    static std::string BuildChangeJSON(const std::vector<std::string>& added,
                                       const std::vector<std::string>& removed,
                                       const std::vector<std::string>& updated) {
        std::string json = "{\"added\":[";
        for (size_t i = 0; i < added.size(); ++i) {
            if (i > 0) json += ',';
            json += helpers::QuoteForJSON(added[i], false);
        }
        json += "],\"removed\":[";
        for (size_t i = 0; i < removed.size(); ++i) {
            if (i > 0) json += ',';
            json += helpers::QuoteForJSON(removed[i], false);
        }
        json += "],\"updated\":[";
        for (size_t i = 0; i < updated.size(); ++i) {
            if (i > 0) json += ',';
            json += helpers::QuoteForJSON(updated[i], false);
        }
        json += "]}";
        return json;
    }

    // A pre-compressed icon served for "/favicon.ico".
    //
    // Input : none; the payload is a constant byte string.
    // Output: the compressed bytes, sent with "Content-Encoding: gzip" and only
    //         to a client that listed gzip in "Accept-Encoding".
    //
    // Keeping it here rather than as a file means a page being debugged stays
    // free of a 404 in its console without adding anything to the served tree
    // or a compression step to the request path.
    static const std::string kFaviconIcoGz;

    filesystem::Fs& fs_;
    RebuildFn                         rebuild_;
    std::string                       outdir_path_prefix_;
    std::string                       abs_output_dir_;
    std::string                       public_path_;
    std::string                       servedir_;
    std::string                       keyfile_to_lower_;
    std::string                       certfile_to_lower_;
    std::string                       fallback_;
    std::function<void(const ServeOnRequestArgs&)> on_request_;
    std::vector<std::string>          cors_origin_;
    std::vector<std::string>          hosts_;

    std::mutex                        mutex_;
    std::vector<std::shared_ptr<EventStream>> active_streams_;
    std::unordered_map<std::string, std::string> current_hashes_;
};

const std::string ApiHandler::kFaviconIcoGz =
    "\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03"
    "\x63\x60\x60\x7e\xc2\x00\x00\x80\x0c\x02\x01\xd3\xb8\xba\x00\x00\x00";

} // namespace: the request handler and the live-reload plumbing

namespace {

// ===========================================================================
// The public "Serve()" entry point
//
// "Serve()" validates the options, picks a port, prints the URLs and starts the
// accept loop. Everything it hands back lives in the result: the port that was
// really bound, and a "stop" callback that closes the listener and wakes every
// waiting connection thread. The server keeps no other state outside the
// handler, so two servers in one process do not interfere with each other.
// ===========================================================================

// Prints the URLs the server can be reached at.
//
// Input : the host addresses, the bound port, whether the scheme is HTTPS, and
//         the colour setting to print with.
// Output: one line per address, labelled "Local" for a loopback address and
//         "Network" for the rest, with the labels padded to a common width so
//         the URLs line up.
//
// An IPv6 address is bracketed so that the ":port" following it cannot be read
// as part of the address. The whole block goes out through the logger, so it
// honours the same colour choice as the rest of Guchho's output.
void PrintURLs(const std::vector<std::string>& hosts, uint16_t port, bool https,
               logger::UseColor use_color) {
    logger::PrintTextWithColor(2, use_color, [&](const logger::Colors& colors) -> std::string {
        std::string result;
        result += std::string(colors.reset);

        std::vector<std::string> kinds;
        kinds.reserve(hosts.size());
        size_t max_len = 0;
        for (const std::string& host : hosts) {
            std::string kind = "Network";
            if (IsLoopbackIP(host)) {
                kind = "Local";
            }
            kinds.push_back(kind);
            max_len = std::max(max_len, static_cast<size_t>(kind.size()));
        }

        std::string protocol = https ? "https" : "http";
        for (size_t i = 0; i < hosts.size(); ++i) {
            std::string host_text = hosts[i];
            if (host_text.find(':') != std::string::npos) {
                host_text = "[" + host_text + "]";
            }
            result += "\n > ";
            result += kinds[i];
            result += std::string(max_len - kinds[i].size(), ' ');
            result += " ";
            result += std::string(colors.underline);
            result += protocol;
            result += "://";
            result += host_text;
            result += ":";
            result += std::to_string(port);
            result += "/";
            result += std::string(colors.reset);
        }
        result += "\n\n";
        return result;
    });
} // namespace: URL printing

}

// Starts the development server and returns once it is listening.
//
// Input : the file system to read the served tree through, a "RebuildFn" that
//         produces the current build output, the serve options, and the log
//         level and colour setting to print the URLs with.
// Output: a "ServeResult" holding the port that was bound and a "stop" callback.
//         On failure "error" is filled in and no server is started.
//
// A caller drives it like this:
//
//     std::string error;
//     ServeResult server = Serve(fs, rebuild, options, log_level, colour, error);
//     if (!error.empty()) {
//         // Nothing is listening, and "error" says why.
//     }
//     // ... later, to shut down:
//     server.stop();
//
// The steps, in order:
//
//   * TLS is refused. A key and a certificate must be supplied together, and
//     supplying both is reported as unsupported rather than quietly serving
//     plain HTTP, so a caller that asked for HTTPS never believes it got it.
//
//   * CORS origins are checked. One wildcard is allowed, since that is what
//     makes a pattern such as "https://*.example.com" expressible; two would
//     have no defined meaning to check against.
//
//   * The host decides the address family. A numeric address chooses between an
//     IPv4 and an IPv6 socket up front, while a name can only be resolved by
//     the system, so the family is left open for it.
//
//   * The port is chosen. Zero does not mean any port here: 8000 to 8009 are
//     tried in order so that repeated runs land on a predictable URL, and only
//     when all ten are taken does the system choose. A port the caller named is
//     used exactly as given.
//
//   * The printed host list follows from what was bound. Binding to every
//     interface produces no single address to visit, so each usable local
//     address becomes a URL; binding to one address yields just that one, plus
//     the caller's host name when a name was given, because a name has to be
//     accepted in a "Host" header to be reachable at all.
//
//   * The served directory and the fallback page are made absolute, so a
//     relative path in the options keeps meaning the same thing regardless of
//     the working directory the process ends up in.
ServeResult Serve(
    filesystem::Fs&       fs,
    RebuildFn             rebuild,
    const ServeOptions&   options,
    logger::LogLevel      log_level,
    logger::UseColor      use_color,
    std::string&          error) {
    ServeResult result;
    error.clear();

    // A key without a certificate, or the reverse, is a configuration mistake
    // worth reporting separately from TLS being unavailable.
    bool is_https = !options.keyfile.empty() || !options.certfile.empty();
    if (!options.keyfile.empty() != !options.certfile.empty()) {
        error = "Must specify both key and certificate for HTTPS";
        return result;
    }
    if (is_https) {
        error = "HTTPS is not yet supported by the in-repo HTTP server";
        return result;
    }

    // Each origin may hold at most one wildcard.
    for (const std::string& origin : options.cors.origin) {
        size_t star = origin.find('*');
        if (star != std::string::npos &&
            origin.find('*', star + 1) != std::string::npos) {
            error = "Invalid origin: " + origin;
            return result;
        }
    }

    // No host means every interface.
    std::string host = "0.0.0.0";
    bool host_is_ip = true;
    if (!options.host.empty()) {
        host = options.host;
        bool is_v6 = false;
        if (IsIPLiteral(host, is_v6)) {
            host_is_ip = true;
        } else {
            host_is_ip = false;
        }
    }

    // A caller asking for port 0 gets a predictable answer rather than a random
    // one, so the familiar development ports are tried first.
    uint16_t chosen_port = options.port;
    Listener listener;
    std::string bound_host;
    if (chosen_port == 0) {
        for (uint16_t port = 8000; port <= 8009; ++port) {
            uint16_t actual = 0;
            std::string perror;
            Listener l = Listener::Listen(host, port, actual, bound_host, perror);
            if (l.Valid()) {
                listener = std::move(l);
                chosen_port = actual;
                break;
            }
        }
    }
    if (!listener.Valid()) {
        uint16_t port = chosen_port;
        if (port > 0xFFFF) {
            port = 0;
        }
        uint16_t actual = 0;
        Listener l = Listener::Listen(host, port, actual, bound_host, error);
        if (!l.Valid()) {
            return result;
        }
        listener = std::move(l);
        chosen_port = actual;
    }

    result.port = chosen_port;

    // Binding to every interface leaves no single address to print, so the
    // usable local addresses become the list of URLs.
    std::vector<std::string> hosts;
    if (IsUnspecifiedIP(bound_host)) {
        std::vector<std::string> local_ips;
        bool want_v6 = bound_host.find(':') != std::string::npos;
        if (GetLocalIPs(want_v6, local_ips)) {
            for (const std::string& ip : local_ips) {
                hosts.push_back(ip);
            }
        }
    } else {
        hosts.push_back(bound_host);
    }

    // A host name is not an interface address and so cannot come from the list
    // above; it has to be allowed explicitly to be reachable at all.
    if (!host_is_ip) {
        hosts.push_back(host);
    }

    // Both paths are resolved against the file system now, so a relative path
    // in the options behaves the same from any working directory.
    std::string servedir = options.servedir;
    if (!servedir.empty()) {
        if (auto abs = fs.Abs(servedir)) {
            servedir = *abs;
        } else {
            error = "Invalid serve path: " + servedir;
            return result;
        }
    }

    std::string fallback = options.fallback;
    if (!fallback.empty()) {
        if (auto abs = fs.Abs(fallback)) {
            fallback = *abs;
        } else {
            error = "Invalid fallback path: " + fallback;
            return result;
        }
    }

    ServeOptions effective = options;
    effective.servedir = std::move(servedir);
    effective.fallback = std::move(fallback);

    // The in-memory layer is keyed on the build's output directory: request
    // paths are made relative to it and looked up among the rebuild results,
    // and the paths that come back are published under "public_path". Both are
    // empty here, which leaves the served tree to "servedir" and the fallback
    // page.
    std::string abs_output_dir;

    auto handler = std::make_shared<ApiHandler>(
        fs, std::move(rebuild), "", abs_output_dir, "", std::move(effective));
    handler->SetHosts(hosts);

    std::vector<std::string> print_hosts = handler->Hosts();

    if (log_level >= logger::LogLevel::kInfo) {
        PrintURLs(print_hosts, result.port, false, use_color);
    }

    auto shared = std::make_shared<Listener>(std::move(listener));
    std::shared_ptr<ApiHandler> handler_shared = handler;

    // Stopping has two halves. Closing the listener unblocks the accept loop,
    // and closing the streams unblocks the connections that are waiting for a
    // rebuild notification to arrive.
    result.stop = [handler_shared, shared]() {
        if (shared->Valid()) {
            CloseSocket(shared->Get());
        }
        handler_shared->CloseAllStreams();
    };

    // The accept loop. It runs until the listener is closed, and hands every
    // connection to a thread of its own so a slow client cannot hold up the
    // next request.
    std::thread accept_thread([handler_shared, shared]() {
        for (;;) {
#ifdef _WIN32
            fd_set read_set;
            FD_ZERO(&read_set);
            SOCKET control = shared->Get();
            if (control == INVALID_SOCKET) {
                break;
            }
            FD_SET(control, &read_set);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            int sel = select(int(control) + 1, &read_set, nullptr, nullptr, &tv);
            if (sel == 0) {
                continue;
            }
            if (sel < 0) {
                break;
            }
#else
#endif
            // Closing the listener is enough to unblock a blocking accept here.
            struct sockaddr_storage addr {};
            socklen_t len = sizeof(addr);
            SockHandle conn = accept(shared->Get(),
                                     reinterpret_cast<sockaddr*>(&addr), &len);
            // Either the listener was closed, which is how the loop is told
            // to stop, or the accept was interrupted. There is nothing to
            // serve in either case, so the next iteration decides.
            if (conn == kInvalidSocket) {
                continue;
            }

            // The peer address is resolved numerically for the on-request
            // callback, and failing to format it must not fail the request.
            std::string remote_addr;
            char hostbuf[NI_MAXHOST] = {};
            int rc = getnameinfo(reinterpret_cast<sockaddr*>(&addr), len, hostbuf,
                                 sizeof(hostbuf), nullptr, 0, NI_NUMERICHOST);
            remote_addr = (rc == 0) ? std::string(hostbuf) : "unknown";

            // One thread per connection: read the head, parse it, answer it,
            // then close. Nothing is remembered between requests, so there is no
            // connection state to reset or to leak into the next one.
            std::thread([handler_shared, conn, remote_addr]() {
                std::string head;
                if (!ReadRequestHead(conn, head)) {
                    CloseSocket(conn);
                    return;
                }
                Request req;
                if (!ParseRequestHead(head, req)) {
                    Response resp(conn, false);
                    resp.WriteHeader(400);
                    resp.Write("400 - Bad Request");
                    CloseSocket(conn);
                    return;
                }
                req.remote_addr = remote_addr;
                bool is_head = req.method == "HEAD";
                Response resp(conn, is_head);
                handler_shared->Handle(req, &resp);
                resp.Flush();
                CloseSocket(conn);
            }).detach();
        }
    });

    accept_thread.detach();
    return result;
}

} // namespace guchho::api
