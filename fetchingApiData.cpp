#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <memory>
#include "json.hpp"

using json = nlohmann::json;

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

using Stream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;


struct tagTickerPrice
{
    int nClosePrice;
    std::string symbol;
    long long timestamp; // Using long long for timestamp to accommodate milliseconds
};

// Report a failure
void fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

class session : public std::enable_shared_from_this<session>
{
    tcp::resolver resolver_;
    Stream ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    std::string message_text_;

public:
    explicit session(net::io_context& ioc, ssl::context& ctx)
        : resolver_(net::make_strand(ioc))
        , ws_(net::make_strand(ioc), ctx)
    {
    }

    void run(char const* host, char const* port, const json& message)
    {
        host_ = host;
        message_text_ = message.dump();

        resolver_.async_resolve(host, port,
            beast::bind_front_handler(&session::on_resolve, shared_from_this()));
    }

    void on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if(ec)
            return fail(ec, "resolve");

        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

        beast::get_lowest_layer(ws_).async_connect(
            results,
            beast::bind_front_handler(&session::on_connect, shared_from_this()));
    }

    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if(ec)
            return fail(ec, "connect");

        ws_.next_layer().async_handshake(ssl::stream_base::client,
            beast::bind_front_handler(&session::on_ssl_handshake, shared_from_this()));
    }

    void on_ssl_handshake(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "ssl_handshake");

        beast::get_lowest_layer(ws_).expires_never();

        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

        ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req)
            {
                req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-async");
            }));

        ws_.async_handshake(host_, "/ws",
            beast::bind_front_handler(&session::on_handshake, shared_from_this()));
    }

    void on_handshake(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "handshake");

        std::cout << "Sending: " << message_text_ << std::endl;
        ws_.async_write(net::buffer(message_text_),
            beast::bind_front_handler(&session::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        if(ec)
            return fail(ec, "write");

        ws_.async_read(buffer_,
            beast::bind_front_handler(&session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    if(ec)
        return fail(ec, "read");
        
    // Parse the received data as JSON 
    json data = json::parse(beast::buffers_to_string(buffer_.data()));
   // std::cout << "Received: " << std::setw(4) << data << std::endl;
    
    // Check if the necessary fields are present and not null
    if (data.contains("c") && !data["c"].is_null() &&
        data.contains("E") && !data["E"].is_null() &&
        data.contains("s") && !data["s"].is_null())
    {
        try
        {
            // Create a new tagTicketPrice object and populate it
            tagTickerPrice* lpstTicketPrice = new tagTickerPrice();
            lpstTicketPrice->nClosePrice = std::stoi(data["c"].get<std::string>());
            lpstTicketPrice->timestamp = data["E"].get<long long>();
            lpstTicketPrice->symbol = data["s"];

            // Use the parsed data as needed
            std::cout << "Close Price: " << lpstTicketPrice->nClosePrice << std::endl;
            std::cout << "Timestamp: " << lpstTicketPrice->timestamp << std::endl;
            std::cout << "Symbol: " <<  lpstTicketPrice->symbol << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    else
    {
        std::cout << "No valid data found." << std::endl;
    }

    // Clear the buffer
    buffer_.clear();

    // Continue reading
    ws_.async_read(buffer_,
        beast::bind_front_handler(&session::on_read, shared_from_this()));
}

    // on_close(beast::error_code ec)
    // {
    //     if(ec)
    //         return fail(ec, "close");

    //     // If we get here then the connection is closed gracefully

    //     // The make_printable() function helps print a ConstBufferSequence
    //    // std::cout << beast::make_printable(buffer_.data()) << std::endl;
    // }
};

int main()
{
    // Read JSON data from file
    std::ifstream file("details.json");
    json actualjson;
    file >> actualjson;

    net::io_context ioc;
    ssl::context ctx(ssl::context::tlsv12_client);
    ctx.set_verify_mode(ssl::verify_peer);
    ctx.set_default_verify_paths();

    // Launch the asynchronous operation
    std::make_shared<session>(ioc, ctx)->run("stream.binance.com", "9443", actualjson);

    // Run the I/O service. The call will return when the socket is closed.
    ioc.run();

    return 0;
}
