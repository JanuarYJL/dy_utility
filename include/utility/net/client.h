#ifndef DY_NET_CLIENT_H
#define DY_NET_CLIENT_H

#include <atomic>

#include "logger/logger.hpp"
#include "session.h"

namespace dy
{
namespace utility
{
/**
 * @brief Client
 */
template<typename TSession, typename TResolver>
class socket_client : public std::enable_shared_from_this<socket_client<TSession, TResolver>>
{
public:
    using session_type = TSession;
    using sessionid_type = typename session_type::sessionid_type;
    using session_ptr_type = std::shared_ptr<session_type>;
    using socket_type = typename TSession::socket_type;
    using socket_ptr_type = std::shared_ptr<socket_type>;
    using timer_type = typename session_type::timer_type;
    using resolver_type = TResolver;
    using resolver_iter_type = typename resolver_type::iterator;
    using mutex_type = typename TSession::mutex_type;
    using lock_guard_type = typename TSession::lock_guard_type;
    using size_type = typename TSession::size_type;
    using func_pack_parse_type = typename TSession::func_pack_parse_type;
    using func_receive_cb_type = typename TSession::func_receive_cb_type;
    using func_disconn_cb_type = typename TSession::func_disconn_cb_type;

private:
    mutex_type mutex_;
    asio::io_context& ioc_;
    std::string remote_host_;
    std::string remote_port_;
    std::string login_data_;
    std::string heart_data_;
    int heartbeat_interval_{10};
    int send_timeout_{30};
    int recv_timeout_{30};
    std::atomic_bool auto_reconnect_{false};
    std::atomic_int unique_ssid_{0};
    socket_type socket_;
    session_ptr_type session_ptr_{nullptr};
    func_pack_parse_type pack_parse_method_;
    func_receive_cb_type receive_callback_;
    func_disconn_cb_type disconnect_callback_;
    size_type send_queue_capacity_{8192u};

public:
    socket_client(asio::io_context& ioc) : ioc_(ioc), socket_(ioc_)
    {

    }
    ~socket_client()
    {
        close();
    }

    void set_endpoint(const std::string &host, const std::string &port)
    {
        lock_guard_type lk(mutex_);
        remote_host_ = host;
        remote_port_ = port;
    }

    void set_callback(func_pack_parse_type pack_parse_method, func_receive_cb_type receive_callback, func_disconn_cb_type disconnect_callback)
    {
        lock_guard_type lk(mutex_);
        pack_parse_method_ = pack_parse_method;
        receive_callback_ = receive_callback;
        disconnect_callback_ = disconnect_callback;
    }

    void set_options(const std::string &login_data, const bool& auto_reconnect = false,
                     const std::string &heartbeat_data = "", const int &heartbeat_interval = 10, 
                     const int &send_timeout = 30, const int &recv_timeout = 30)
    {
        lock_guard_type lk(mutex_);
        login_data_ = login_data;
        auto_reconnect_ = auto_reconnect;
        heart_data_ = heartbeat_data;
        heartbeat_interval_ = heartbeat_interval;
        send_timeout_ = send_timeout;
        recv_timeout_ = recv_timeout;
    }

    void connect()
    {
        lock_guard_type lk(mutex_);
        // 关闭原连接
        if (session_ptr_ && !session_ptr_->stopped())
        {
            session_ptr_->stop();
        }
        // 关闭原socket
        if (socket_.is_open())
        {
            boost::system::error_code ec;
            socket_.close(ec);
        }
        // 解析地址 进行连接
        resolver_type rsl(ioc_);
        resolver_iter_type endpoint_iter = rsl.resolve(remote_host_, remote_port_);
        if (endpoint_iter != resolver_iter_type())
        {
            socket_.async_connect(*endpoint_iter, std::bind(&socket_client<TSession, TResolver>::handle_connect, this->shared_from_this(), std::placeholders::_1, endpoint_iter));
        }
        else
        {
            UTILITY_LOGGER(info) << "connect failed, remote_addr:" << remote_host_ << "/" << remote_port_;
        }
    }

    void disconnect()
    {
        lock_guard_type lk(mutex_);

        if (session_ptr_ && !session_ptr_->stopped())
        {
            session_ptr_->stop();
        }
        if (socket_.is_open())
        {
            boost::system::error_code ec;
            socket_.close(ec);
        }
    }

    void close()
    {
        // 设置不重连
        auto_reconnect_ = false;
        // 断开当前连接
        disconnect();
    }

    int async_send(const char* data, const buffer::size_type& length)
    {
        lock_guard_type lk(mutex_);
        if (session_ptr_)
        {
            return session_ptr_->async_send(data, length);
        }
        else
        {
            return error_code::session_not_exist;
        }
    }

private:
    void handle_connect(std::error_code ec, resolver_iter_type endpoint_iter)
    {
        try
        {
            lock_guard_type lk(mutex_);
            // 连接成功
            if (!ec)
            {
                session_ptr_ = std::make_shared<session_type>(std::move(socket_), pack_parse_method_, receive_callback_,
                                                              std::bind(&socket_client<TSession, TResolver>::on_disconnect, this->shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                                                              send_queue_capacity_);
                session_ptr_->set_session_id(++unique_ssid_);
                session_ptr_->set_options(send_timeout_, recv_timeout_, heartbeat_interval_, heart_data_);
                session_ptr_->start();
                session_ptr_->async_send(login_data_.c_str(), login_data_.length());

                UTILITY_LOGGER(info) << "connect success, endpoint:" << endpoint_iter->endpoint();
            }
            // 继续连接下一个地址
            else if (++endpoint_iter != resolver_iter_type())
            {
                boost::system::error_code ec;
                socket_.close(ec);
                socket_.async_connect(*endpoint_iter, std::bind(&socket_client<TSession, TResolver>::handle_connect, this->shared_from_this(), std::placeholders::_1, endpoint_iter));
            }
            // 连接失败
            else
            {
                UTILITY_LOGGER(info) << "connect failed, error_code:" << ec;
                // 连接失败时延迟5s进行重连
                auto _self = this->shared_from_this();
                auto delay_timer = std::make_shared<timer_type>(ioc_);
                delay_timer->expires_after(asio::chrono::seconds(5));
                delay_timer->async_wait(std::bind([this, delay_timer, _self]() { connect(); }));
            }
        }
        catch (std::exception &ecp)
        {
            UTILITY_LOGGER(error) << __FUNCTION__ << " catch:" << ecp.what();
        }
    }

    void on_disconnect(const sessionid_type& session_id, const int& reason_code, const std::string& message)
    {
        disconnect_callback_(session_id, reason_code, message);
        if (auto_reconnect_)
        {
            // 断开连接时延迟2s进行重连
            auto _self = this->shared_from_this();
            auto delay_timer = std::make_shared<timer_type>(ioc_);
            delay_timer->expires_after(asio::chrono::seconds(2));
            delay_timer->async_wait(std::bind([this, delay_timer, _self]() { connect(); }));
        }
    }
};

// explicit class declaration
// TCP
using tcp_client = socket_client<tcp_session, asio::ip::tcp::resolver>;
// UDP
using udp_client = socket_client<udp_session, asio::ip::udp::resolver>;

} // namespace utility
} // namespace dy
#endif