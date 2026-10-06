#include "RangeProxy.hpp"

#include "Logger.hpp"
#include "Process.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <vector>

namespace dk2vr {
namespace {

constexpr std::uint64_t kDefaultChunkSize = 10ULL * 1024ULL * 1024ULL;
constexpr int kMaxUpstreamAttempts = 3;
constexpr std::size_t kMaxRequestHeaderBytes = 16 * 1024;

SOCKET toSocket(const std::uintptr_t value)
{
    return static_cast<SOCKET>(value);
}

bool sendAll(const SOCKET socket, const char* data, std::size_t size)
{
    while (size > 0) {
        const int chunk = static_cast<int>((std::min)(size, static_cast<std::size_t>(1 << 20)));
        const int sent = ::send(socket, data, chunk, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::string lowerCase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

// Value of a query parameter, with %XX escapes decoded.
std::string queryParameter(const std::string& url, const std::string& name)
{
    for (const char* prefix : {"?", "&"}) {
        const std::string key = prefix + name + "=";
        const std::size_t begin = url.find(key);
        if (begin == std::string::npos) {
            continue;
        }
        const std::size_t valueBegin = begin + key.size();
        const std::size_t valueEnd = url.find('&', valueBegin);
        const std::string raw = url.substr(valueBegin,
            valueEnd == std::string::npos ? std::string::npos : valueEnd - valueBegin);
        std::string decoded;
        for (std::size_t index = 0; index < raw.size(); ++index) {
            if (raw[index] == '%' && index + 2 < raw.size()) {
                decoded.push_back(static_cast<char>(std::strtol(raw.substr(index + 1, 2).c_str(), nullptr, 16)));
                index += 2;
            } else {
                decoded.push_back(raw[index]);
            }
        }
        return decoded;
    }
    return {};
}

// One upstream HTTPS connection, used for the sequential chunk requests of a
// single relay.
class Upstream {
public:
    ~Upstream()
    {
        if (connection_ != nullptr) {
            WinHttpCloseHandle(connection_);
        }
        if (session_ != nullptr) {
            WinHttpCloseHandle(session_);
        }
    }

    bool open(const std::string& url, const std::map<std::string, std::string>& headers)
    {
        const std::wstring wideUrl = utf8ToWide(url);
        URL_COMPONENTS parts {};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
            return false;
        }
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        path_.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        path_.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        secure_ = parts.nScheme == INTERNET_SCHEME_HTTPS;

        std::wstring userAgent = L"Mozilla/5.0";
        for (const auto& [name, value] : headers) {
            const std::string lowerName = lowerCase(name);
            if (lowerName == "user-agent") {
                userAgent = utf8ToWide(value);
            } else if (lowerName != "range" && lowerName != "host") {
                extraHeaders_ += utf8ToWide(name + ": " + value + "\r\n");
            }
        }

        session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_ == nullptr) {
            return false;
        }
        WinHttpSetTimeouts(session_, 10000, 10000, 10000, 10000);
        connection_ = WinHttpConnect(session_, host.c_str(), parts.nPort, 0);
        return connection_ != nullptr;
    }

    // Requests bytes [first, last] and hands each received block to sink.
    // Returns the number of bytes delivered; fewer than requested means the
    // transfer failed (or sink asked to stop by returning false).
    std::uint64_t fetch(const std::uint64_t first, const std::uint64_t last,
        const std::function<bool(const char*, std::size_t)>& sink, std::uint64_t* totalLength)
    {
        HINTERNET request = WinHttpOpenRequest(connection_, L"GET", path_.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0);
        if (request == nullptr) {
            return 0;
        }
        const std::wstring headers = L"Range: bytes=" + std::to_wstring(first) + L"-"
            + std::to_wstring(last) + L"\r\n" + extraHeaders_;
        std::uint64_t delivered = 0;
        if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (status == 206 || status == 200) {
                if (totalLength != nullptr) {
                    wchar_t contentRange[128] {};
                    DWORD rangeSize = sizeof(contentRange);
                    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Content-Range",
                            contentRange, &rangeSize, WINHTTP_NO_HEADER_INDEX)) {
                        if (const wchar_t* slash = std::wcschr(contentRange, L'/')) {
                            *totalLength = std::wcstoull(slash + 1, nullptr, 10);
                        }
                    } else if (status == 200) {
                        // No range support: the whole file, so its length.
                        wchar_t contentLength[32] {};
                        DWORD lengthSize = sizeof(contentLength);
                        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX, contentLength, &lengthSize,
                                WINHTTP_NO_HEADER_INDEX)) {
                            *totalLength = std::wcstoull(contentLength, nullptr, 10);
                        }
                    }
                }
                std::vector<char> buffer(64 * 1024);
                // A server without range support answers 200 with the file
                // from byte 0; skip up to the requested start so the relayed
                // bytes are still the right ones.
                std::uint64_t skip = status == 200 ? first : 0;
                while (skip > 0) {
                    DWORD read = 0;
                    const DWORD want = static_cast<DWORD>((std::min)(skip, static_cast<std::uint64_t>(buffer.size())));
                    if (!WinHttpReadData(request, buffer.data(), want, &read) || read == 0) {
                        break;
                    }
                    skip -= read;
                }
                const std::uint64_t wanted = skip == 0 ? last - first + 1 : 0;
                while (delivered < wanted) {
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)
                        || read == 0) {
                        break;
                    }
                    const std::size_t usable = static_cast<std::size_t>(
                        (std::min)(static_cast<std::uint64_t>(read), wanted - delivered));
                    if (!sink(buffer.data(), usable)) {
                        break;
                    }
                    delivered += usable;
                }
            } else {
                log::warning("RangeProxy: upstream HTTP " + std::to_string(status));
            }
        }
        WinHttpCloseHandle(request);
        return delivered;
    }

private:
    HINTERNET session_ {nullptr};
    HINTERNET connection_ {nullptr};
    std::wstring path_;
    std::wstring extraHeaders_;
    bool secure_ {true};
};

} // namespace

RangeProxy::~RangeProxy()
{
    stop();
}

bool RangeProxy::start(std::string& error)
{
    if (acceptThread_.joinable()) {
        return true;
    }
    WSADATA data {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "Winsock baslatilamadi.";
        return false;
    }
    winsockStarted_ = true;

    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        error = "Yerel YouTube proxy soketi acilamadi.";
        stop();
        return false;
    }
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0; // any free port
    int addressSize = sizeof(address);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(listener, SOMAXCONN) != 0
        || ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressSize) != 0) {
        ::closesocket(listener);
        error = "Yerel YouTube proxy portu acilamadi.";
        stop();
        return false;
    }
    listenSocket_ = static_cast<std::uintptr_t>(listener);
    port_ = ntohs(address.sin_port);
    stopping_ = false;
    acceptThread_ = std::thread(&RangeProxy::acceptLoop, this);
    log::info("RangeProxy: 127.0.0.1:" + std::to_string(port_) + " dinleniyor.");
    return true;
}

void RangeProxy::stop()
{
    stopping_ = true;
    if (listenSocket_ != 0) {
        // Unblocks accept() in the accept thread.
        ::closesocket(toSocket(listenSocket_));
        listenSocket_ = 0;
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Client& client : clients_) {
            // Unblocks recv()/send(); the relay thread closes its own socket.
            if (!client.done) {
                ::shutdown(toSocket(client.socket), SD_BOTH);
            }
        }
    }
    for (Client& client : clients_) {
        if (client.thread.joinable()) {
            client.thread.join();
        }
    }
    clients_.clear();
    sources_.clear();
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
    port_ = 0;
}

std::string RangeProxy::serve(const std::string& upstreamUrl,
    const std::map<std::string, std::string>& httpHeaders, const std::uint64_t chunkSize)
{
    auto source = std::make_shared<Source>();
    source->url = upstreamUrl;
    source->headers = httpHeaders;
    source->chunkSize = chunkSize > 0 ? chunkSize : kDefaultChunkSize;
    // googlevideo URLs carry the file size and MIME type; without them the
    // first relay learns the size from Content-Range.
    source->length = std::strtoull(queryParameter(upstreamUrl, "clen").c_str(), nullptr, 10);
    source->contentType = queryParameter(upstreamUrl, "mime");
    if (source->contentType.empty()) {
        source->contentType = "application/octet-stream";
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const int id = nextId_++;
    sources_[id] = std::move(source);
    return "http://127.0.0.1:" + std::to_string(port_) + "/" + std::to_string(id);
}

void RangeProxy::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.clear();
}

void RangeProxy::reapFinishedClients()
{
    for (auto iterator = clients_.begin(); iterator != clients_.end();) {
        if (iterator->done) {
            iterator->thread.join();
            iterator = clients_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void RangeProxy::acceptLoop()
{
    while (!stopping_) {
        const SOCKET socket = ::accept(toSocket(listenSocket_), nullptr, nullptr);
        if (socket == INVALID_SOCKET) {
            if (stopping_) {
                break;
            }
            continue;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        reapFinishedClients();
        Client& client = clients_.emplace_back();
        client.socket = static_cast<std::uintptr_t>(socket);
        client.thread = std::thread([this, &client] { handleClient(client); });
    }
}

void RangeProxy::handleClient(Client& client)
{
    const SOCKET socket = toSocket(client.socket);
    // Closing under the lock means stop() never shuts down a socket handle
    // that has already been closed (and possibly reused).
    const auto finish = [&] {
        std::lock_guard<std::mutex> lock(mutex_);
        ::closesocket(socket);
        client.done = true;
    };

    // Read the request head.
    std::string request;
    char buffer[2048];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxRequestHeaderBytes) {
        const int received = ::recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            finish();
            return;
        }
        request.append(buffer, static_cast<std::size_t>(received));
    }

    const std::size_t methodEnd = request.find(' ');
    const std::size_t pathEnd = methodEnd == std::string::npos ? std::string::npos
                                                               : request.find(' ', methodEnd + 1);
    if (pathEnd == std::string::npos) {
        finish();
        return;
    }
    const std::string method = request.substr(0, methodEnd);
    const std::string path = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
    const int id = path.size() > 1 ? std::atoi(path.c_str() + 1) : 0;

    std::shared_ptr<Source> source;
    std::uint64_t length = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sources_.find(id);
        if (found != sources_.end()) {
            source = found->second;
            length = source->length;
        }
    }
    if (source == nullptr || (method != "GET" && method != "HEAD")) {
        const std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(socket, response.data(), response.size());
        finish();
        return;
    }

    Upstream upstream;
    if (!upstream.open(source->url, source->headers)) {
        log::warning("RangeProxy: upstream adresi acilamadi.");
        finish();
        return;
    }

    if (length == 0) {
        // Probe with a one-byte request to learn the total size.
        std::uint64_t total = 0;
        upstream.fetch(0, 0, [](const char*, std::size_t) { return true; }, &total);
        length = total;
        std::lock_guard<std::mutex> lock(mutex_);
        source->length = total;
    }
    if (length == 0) {
        const std::string response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(socket, response.data(), response.size());
        finish();
        return;
    }

    // Range: bytes=first- or bytes=first-last. Anything else means the whole file.
    std::uint64_t first = 0;
    std::uint64_t last = length - 1;
    bool ranged = false;
    const std::string lowerRequest = lowerCase(request);
    const std::size_t rangeHeader = lowerRequest.find("\r\nrange: bytes=");
    if (rangeHeader != std::string::npos) {
        const char* spec = request.c_str() + rangeHeader + std::string("\r\nrange: bytes=").size();
        char* afterFirst = nullptr;
        first = std::strtoull(spec, &afterFirst, 10);
        if (afterFirst != spec && *afterFirst == '-') {
            ranged = true;
            if (std::isdigit(static_cast<unsigned char>(afterFirst[1])) != 0) {
                last = (std::min)(std::strtoull(afterFirst + 1, nullptr, 10), length - 1);
            }
        }
    }
    if (first >= length || first > last) {
        const std::string response = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */"
            + std::to_string(length) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(socket, response.data(), response.size());
        finish();
        return;
    }

    std::string head = ranged ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    head += "Content-Type: " + source->contentType + "\r\n";
    head += "Content-Length: " + std::to_string(last - first + 1) + "\r\n";
    if (ranged) {
        head += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/"
            + std::to_string(length) + "\r\n";
    }
    head += "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n";
    if (!sendAll(socket, head.data(), head.size()) || method == "HEAD") {
        finish();
        return;
    }

    // Relay the requested span in bounded upstream chunks. A failed chunk is
    // retried from where it stopped; the client closing the socket (VLC
    // seeking or stopping) ends the relay.
    bool clientGone = false;
    const auto sink = [&](const char* data, const std::size_t size) {
        if (stopping_ || !sendAll(socket, data, size)) {
            clientGone = true;
            return false;
        }
        return true;
    };
    std::uint64_t position = first;
    int failures = 0;
    while (position <= last && !clientGone && !stopping_) {
        const std::uint64_t chunkLast = (std::min)(position + source->chunkSize - 1, last);
        const std::uint64_t delivered = upstream.fetch(position, chunkLast, sink, nullptr);
        position += delivered;
        if (delivered < chunkLast - (position - delivered) + 1 && !clientGone) {
            if (++failures >= kMaxUpstreamAttempts) {
                log::warning("RangeProxy: upstream aktarimi " + std::to_string(position)
                    + ". baytta basarisiz oldu.");
                break;
            }
        } else {
            failures = 0;
        }
    }
    finish();
}

} // namespace dk2vr
