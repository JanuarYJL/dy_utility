#ifndef DY_NET_ACCEPTOR_H
#define DY_NET_ACCEPTOR_H

#include "net/session.h"

namespace dy
{
namespace utility
{
/**
 * @brief 接收器
 */
class acceptor
{
public:
    using acceptor_type = asio::ip::tcp::acceptor;
    using socket_type = asio::ip::tcp::socket;
    using resolver_type = asio::ip::tcp::resolver;
    using endpoint_type = asio::ip::tcp::endpoint;
    using func_accept_cb_type = std::function<void(socket_type socket)>;

private:
    acceptor_type acceptor_;
    std::string host_;
    std::string port_;
    func_accept_cb_type func_accept_callback_;
    
public:
    DISABLE_COPY_ASSIGN(acceptor);
    explicit acceptor(asio::io_context &ioc, const std::string &host, const std::string &port, func_accept_cb_type accept_callback)
        : acceptor_(ioc), host_(host), port_(port), func_accept_callback_(accept_callback)
    {
    }

    void start()
    {
        resolver_type resolver(acceptor_.get_executor());
        endpoint_type endpoint = *resolver.resolve(host_, port_).begin();
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        handle_accept();
    }

    void stop()
    {
        if (acceptor_.is_open())
        {
            acceptor_.close();
        }
    }

private:
    void handle_accept()
    {
        acceptor_.async_accept([this](std::error_code ec, socket_type socket) {
            if (!acceptor_.is_open())
            {
                return;
            }

            if (!ec)
            {
                if (func_accept_callback_)
                {
                    func_accept_callback_(std::move(socket));
                }
            }
            else
            {
                // TODO:
            }

            // 继续接收
            handle_accept();
        });
    }
};

} // namespace utility
} // namespace dy

#endif