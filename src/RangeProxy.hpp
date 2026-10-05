#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace dk2vr {

// Local HTTP server that lets libVLC play YouTube's googlevideo streams.
//
// Those URLs answer an open-ended "Range: bytes=N-" request (which is all
// VLC 3 ever sends) with 403 Forbidden and only serve bounded ranges of at
// most ~10 MB; yt-dlp reports this as downloader_options.http_chunk_size.
// VLC is pointed at http://127.0.0.1:<port>/<id> instead, and every request
// it makes, including seeks, is answered by fetching the upstream file in
// bounded chunks and relaying them in order.
class RangeProxy {
public:
    RangeProxy() = default;
    ~RangeProxy();

    RangeProxy(const RangeProxy&) = delete;
    RangeProxy& operator=(const RangeProxy&) = delete;

    // Starts listening on a free loopback port. Safe to call repeatedly.
    [[nodiscard]] bool start(std::string& error);
    // Closes the listener and every relay, and waits for their threads.
    void stop();

    // Registers an upstream URL and returns the local URL that serves it.
    // httpHeaders are sent upstream (yt-dlp's User-Agent etc.).
    [[nodiscard]] std::string serve(const std::string& upstreamUrl,
        const std::map<std::string, std::string>& httpHeaders, std::uint64_t chunkSize);
    // Forgets all registered URLs; relays already running finish on their own.
    void clear();

private:
    struct Source {
        std::string url;
        std::map<std::string, std::string> headers;
        std::uint64_t chunkSize {0};
        std::uint64_t length {0}; // 0 until known
        std::string contentType;
    };
    struct Client {
        std::uintptr_t socket {0};
        std::thread thread;
        std::atomic<bool> done {false};
    };

    void acceptLoop();
    void handleClient(Client& client);
    void reapFinishedClients();

    std::uintptr_t listenSocket_ {0};
    bool winsockStarted_ {false};
    unsigned short port_ {0};
    std::atomic<bool> stopping_ {false};
    std::thread acceptThread_;

    std::mutex mutex_;
    std::map<int, std::shared_ptr<Source>> sources_;
    int nextId_ {1};
    std::list<Client> clients_;
};

} // namespace dk2vr
