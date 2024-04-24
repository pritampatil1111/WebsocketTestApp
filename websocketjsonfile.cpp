#include <iostream>
#include <fstream> // Include for file handling
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include "json.hpp"

// Define an alias for nlohmann::json
using json = nlohmann::json;

namespace net       = boost::asio;
namespace ssl       = net::ssl;
namespace beast     = boost::beast;
namespace http      = beast::http;
namespace websocket = beast::websocket;

using tcp      = net::ip::tcp;
using Request  = http::request<http::string_body>;
using Stream   = beast::ssl_stream<beast::tcp_stream>;
using Response = http::response<http::dynamic_body>;

class Exchange {
  public:
    Exchange(std::string name, const std::string& http_host)
        : m_name(std::move(name))
    {
        init_http(http_host);
    }

    void init_http(std::string const& host)
    {
        const auto results{resolver.resolve(host, "443")};
        get_lowest_layer(stream).connect(results);
        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            boost::system::error_code ec{
                static_cast<int>(::ERR_get_error()),
                boost::asio::error::get_ssl_category()};
            throw boost::system::system_error{ec};
        }
        stream.handshake(ssl::stream_base::client);
    }

    void init_webSocket(std::string const& host, std::string const& port,
                        const char* p = "")
    {
        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                      host.c_str()))
            throw beast::system_error(
                beast::error_code(static_cast<int>(::ERR_get_error()),
                                  net::error::get_ssl_category()),
                "Failed to set SNI Hostname");
        auto const results = resolver_webSocket.resolve(host, port);
        net::connect(ws.next_layer().next_layer(), results.begin(),
                     results.end());
        ws.next_layer().handshake(ssl::stream_base::client);

        ws.handshake(host, p);
    }

    void read_Socket() { ws.read(buffer); }

    bool is_socket_open()
    {
        if (ws.is_open())
            return true;
        return false;
    }

    void write_Socket(const std::string& text) { ws.write(net::buffer(text)); }

    std::string get_socket_data()
    {
        return beast::buffers_to_string(buffer.data());
    }
    void buffer_clear() { buffer.clear(); }

    void webSocket_close() { ws.close(websocket::close_code::none); }

  private:
    // HTTP REQUEST SET //
    std::string     m_name;
    net::io_context ioc;
    ssl::context    ctx{ssl::context::tlsv12_client};
    tcp::resolver   resolver{ioc};
    Stream          stream{ioc, ctx};

    // WEB SOCKET SET //
    std::string        m_web_socket_host;
    std::string        m_web_socket_port;
    beast::flat_buffer buffer;
    net::io_context    ioc_webSocket;
    ssl::context       ctx_webSocket{ssl::context::tlsv12_client};
    tcp::resolver      resolver_webSocket{ioc_webSocket};
    websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc_webSocket,
                                                         ctx_webSocket};
};


int main() {
    Exchange binance{"binance", "api.binance.com"};

    try {
        binance.init_webSocket("stream.binance.com", "9443", "/ws");
        if (binance.is_socket_open())
            binance.write_Socket(
                R"({"method": "SUBSCRIBE","params": ["!ticker@arr"],"id": 1})");

        // Open a file for writing JSON data
        std::ofstream jsonFile("binance_data.json");

        while (true) {
            binance.read_Socket();
            
            // Parse the received data into a JSON object
            json data = json::parse(binance.get_socket_data());

            // Write the JSON data to the file
            jsonFile << std::setw(4) << data << std::endl;

            binance.buffer_clear();
        }

        // Close the WebSocket and file
        binance.webSocket_close();
        jsonFile.close();
    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
