#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace json = boost::json;
using tcp = net::ip::tcp;

// Simple HTTPS session
class Session : public std::enable_shared_from_this<Session> {
    private:
        beast::ssl_stream<beast::tcp_stream> stream_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;

        // Simple in-memory data store
        static inline json::object users_ = {
            {"1", {{"id", 1}, {"name", "Alice"}}},
            {"2", {{"id", 2}, {"name", "Bob"}}}
        };

        json::object handle_get_users() {
            json::object response;
            response["users"] = json::array();
            
            for (auto& [id, user] : users_) {
                response["users"].as_array().push_back(user);
            }
            
            return response;
        }

        json::object handle_get_user(const std::string& id) {
            if (users_.contains(id)) {
                return users_[id].as_object();
            }
            
            json::object error;
            error["error"] = "User not found";
            return error;
        }

        json::object handle_create_user(const std::string& body) {
            json::value jv = json::parse(body);
            json::object user = jv.as_object();
            
            // Generate new ID
            std::string new_id = std::to_string(users_.size() + 1);
            user["id"] = std::stoi(new_id);
            
            users_[new_id] = user;
            
            json::object response;
            response["message"] = "User created";
            response["user"] = user;
            return response;
        }

        void route_request() {
            std::string method(req_.method_string());
            std::string target(req_.target());
            
            http::response<http::string_body> res{http::status::ok, req_.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req_.keep_alive());
            
            try {
                // GET /api/users - List all users
                if (method == "GET" && target == "/api/users") {
                    res.body() = json::serialize(handle_get_users());
                }
                // GET /api/users/:id - Get specific user
                else if (method == "GET" && target.starts_with("/api/users/")) {
                    std::string id = target.substr(11); // Skip "/api/users/"
                    res.body() = json::serialize(handle_get_user(id));
                }
                // POST /api/users - Create new user
                else if (method == "POST" && target == "/api/users") {
                    res.body() = json::serialize(handle_create_user(req_.body()));
                    res.result(http::status::created);
                }
                // 404 Not Found
                else {
                    res.result(http::status::not_found);
                    json::object error;
                    error["error"] = "Endpoint not found";
                    res.body() = json::serialize(error);
                }
            } catch (std::exception const& e) {
                res.result(http::status::bad_request);
                json::object error;
                error["error"] = e.what();
                res.body() = json::serialize(error);
            }
            
            res.prepare_payload();
            write_response(std::move(res));
        }

        void write_response(http::response<http::string_body>&& res) {
            auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
            auto self = shared_from_this();
            
            http::async_write(stream_, *sp,
                [self, sp](beast::error_code ec, std::size_t) {
                    self->stream_.async_shutdown([](beast::error_code){});
                });
        }

        void read_request() {
            auto self = shared_from_this();
            
            http::async_read(stream_, buffer_, req_,
                [self](beast::error_code ec, std::size_t) {
                    if (!ec) {
                        self->route_request();
                    }
                });
        }

        void ssl_handshake() {
            auto self = shared_from_this();
            
            stream_.async_handshake(ssl::stream_base::server,
                [self](beast::error_code ec) {
                    if (!ec) {
                        self->read_request();
                    }
                });
        }

    public:
        Session(tcp::socket&& socket, ssl::context& ctx)
            : stream_(std::move(socket), ctx) {}

        void start() {
            ssl_handshake();
        }
};

// Simple HTTPS Server
class RestApiServer {
    private:
        net::io_context ioc_;
        ssl::context ssl_ctx_;
        tcp::acceptor acceptor_;

        void load_certificates() {
            ssl_ctx_.set_options(
                ssl::context::default_workarounds |
                ssl::context::no_sslv2 |
                ssl::context::single_dh_use);

            ssl_ctx_.use_certificate_chain_file("cert.pem");
            ssl_ctx_.use_private_key_file("key.pem", ssl::context::pem);
        }

        void accept_connection() {
            acceptor_.async_accept(
                [this](beast::error_code ec, tcp::socket socket) {
                    if (!ec) {
                        std::make_shared<Session>(std::move(socket), ssl_ctx_)->start();
                    }
                    accept_connection();
                });
        }

    public:
        RestApiServer(unsigned short port)
            : ssl_ctx_(ssl::context::tlsv12_server),
            acceptor_(ioc_, tcp::endpoint(tcp::v4(), port)) {
            load_certificates();
        }

        void run() {
            std::cout << "REST API running on https://localhost:" 
                    << acceptor_.local_endpoint().port() << std::endl;
            std::cout << "\nEndpoints:" << std::endl;
            std::cout << "  GET    /api/users     - List all users" << std::endl;
            std::cout << "  GET    /api/users/:id - Get user by ID" << std::endl;
            std::cout << "  POST   /api/users     - Create new user" << std::endl;
            
            accept_connection();
            ioc_.run();
        }
};

int main() {
    try {
        RestApiServer server(8443);
        server.run();
    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
