#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <map>

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;   // from <boost/beast/http.hpp>
namespace net = boost::asio;    // from <boost/asio.hpp>
using tcp = net::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

std::map<std::string, std::string> shortened_to_original;

std::string custom_prefix = "AAAA";

void increment_custom_prefix() {
    for (int i = custom_prefix.size() - 1; i > -1; --i) {
        if (custom_prefix[i] == 'Z') {
            custom_prefix[i] = 'A';
        }
        else {
            custom_prefix[i]++;
            return;
        }
    }
}

std::string shorten_url(std::string request_url) {
    std::string response_url = "localhost:8080/" + custom_prefix;
    increment_custom_prefix();
    shortened_to_original[response_url] = request_url;
    return response_url;
}

// this function produces an HTTP response for the given request
http::response<http::string_body> handle_request(http::request<http::string_body> const& req) {
    if (req.method() == http::verb::get) {
        // respond to GET request with "Hello, World!"
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "Beast");
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = "Hello, World!";
        res.prepare_payload();
        return res;
    }
    else if (req.method() == http::verb::post) {
        // respond to POST request with a URL
        auto json_request = nlohmann::json::parse(req.body());
        std::string request_url = json_request.dump();
        request_url = request_url.substr(1, request_url.size() - 2); // removing double qoutes
        std::cout << "received url is: " << request_url << std::endl;

        // if this is a shortened url, it will have the corresponding original url mapped, else we shorten it
        std::string response_url;
        if (shortened_to_original.find(request_url) == shortened_to_original.end()) {
            response_url = shorten_url(request_url);
        }
        else {
            response_url = shortened_to_original[request_url];
        }

        nlohmann::json json_reponse = {{"shortened url", response_url}};
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "Beast");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
        res.body() = json_reponse.dump();
        res.prepare_payload();
        return res;
    }

    // default response for unsupported methods
    return http::response<http::string_body>{http::status::bad_request, req.version()};
}

// this class handles an HTTP server connection
class Session : public std::enable_shared_from_this<Session> {
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;

    public:
        explicit Session(tcp::socket socket) : socket_(std::move(socket)) {}

        void run() {
            do_read();
        }

    private:
        void do_read() {
            auto self(shared_from_this());
            http::async_read(socket_, buffer_, req_, [this, self](beast::error_code ec, std::size_t) {
                if (!ec) {
                    do_write(handle_request(req_));
                }
            });
        }

        void do_write(http::response<http::string_body> res) {
            auto self(shared_from_this());
            auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
            http::async_write(socket_, *sp, [this, self, sp](beast::error_code ec, std::size_t) {
                socket_.shutdown(tcp::socket::shutdown_send, ec);
            });
        }
};

// this class accepts incoming connections and launches the sessions
class Listener : public std::enable_shared_from_this<Listener> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

    public:
        Listener(net::io_context& ioc, tcp::endpoint endpoint) : ioc_(ioc), acceptor_(net::make_strand(ioc)) {
            beast::error_code ec;

            // open the acceptor
            acceptor_.open(endpoint.protocol(), ec);
            if (ec) {
                std::cerr << "open error: " << ec.message() << std::endl;
                return;
            }

            // allow address reuse
            acceptor_.set_option(net::socket_base::reuse_address(true), ec);
            if (ec) {
                std::cerr << "set option error: " << ec.message() << std::endl;
                return;
            }

            // bind to the server address
            acceptor_.bind(endpoint, ec);
            if (ec) {
                std::cerr << "bind error: " << ec.message() << std::endl;
                return;
            }

            // start listening for connections
            acceptor_.listen(net::socket_base::max_listen_connections, ec);
            if (ec) {
                std::cerr << "listen error: " << ec.message() << std::endl;
                return;
            }

            do_accept();
        }

    private:
        void do_accept() {
            acceptor_.async_accept(net::make_strand(ioc_), [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Session>(std::move(socket))->run();
                }
                do_accept();
            });
        }
};

int main() {
    try {
        auto const address = net::ip::make_address("0.0.0.0");
        unsigned short port = 8080;

        net::io_context ioc{1};

        auto listener = std::make_shared<Listener>(ioc, tcp::endpoint{address, port});

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
    }
}