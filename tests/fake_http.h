// yacoin-cpuminer: a tiny one-request-per-connection HTTP server for tests. MIT licence.
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Serves canned replies on 127.0.0.1:<ephemeral port>. The handler receives
// the raw request (headers + body) and returns {status, body}.
class FakeHttpServer {
public:
    using Handler = std::function<std::pair<int, std::string>(const std::string& request)>;

    explicit FakeHttpServer(Handler h) : handler_(std::move(h))
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
        socklen_t len = sizeof a;
        getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
        port_ = ntohs(a.sin_port);
        listen(fd_, 8);
        thread_ = std::thread([this] { loop(); });
    }

    ~FakeHttpServer()
    {
        stop_ = true;
        shutdown(fd_, SHUT_RDWR);
        close(fd_);
        thread_.join();
    }

    int port() const { return port_; }

    std::vector<std::string> requests()
    {
        std::lock_guard<std::mutex> l(mu_);
        return requests_;
    }

private:
    void loop()
    {
        while (!stop_) {
            int c = accept(fd_, nullptr, nullptr);
            if (c < 0) return;
            std::string req;
            char buf[4096];
            size_t want = std::string::npos;
            while (true) {
                ssize_t n = read(c, buf, sizeof buf);
                if (n <= 0) break;
                req.append(buf, static_cast<size_t>(n));
                size_t hdr_end = req.find("\r\n\r\n");
                if (hdr_end != std::string::npos && want == std::string::npos) {
                    size_t cl = req.find("Content-Length:");
                    if (cl == std::string::npos) cl = req.find("content-length:");
                    size_t body_len = cl == std::string::npos ? 0 : std::stoul(req.substr(cl + 15));
                    want = hdr_end + 4 + body_len;
                }
                if (want != std::string::npos && req.size() >= want) break;
            }
            {
                std::lock_guard<std::mutex> l(mu_);
                requests_.push_back(req);
            }
            auto [status, body] = handler_(req);
            std::string resp = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\n" +
                               "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            (void)!write(c, resp.data(), resp.size());
            close(c);
        }
    }

    Handler handler_;
    int fd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mu_;
    std::vector<std::string> requests_;
};
