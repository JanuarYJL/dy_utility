#ifndef DY_NET_SESSION_H
#define DY_NET_SESSION_H

#include "common/common.h"

#include <queue>
#include <mutex>
#include <condition_variable>

#include <boost/asio.hpp>

#include "net/buffer.h"

namespace dy
{
namespace utility
{
namespace asio = boost::asio;

/**
 * @brief Session
 */
class session : public std::enable_shared_from_this<session>
{
public:
    using sessionid_type  = std::size_t;
    using size_type = std::size_t;
    using mutex_type = std::mutex;
    using condv_type = std::condition_variable;
    using lock_guard_type = std::lock_guard<mutex_type>;
    using timer_type = asio::steady_timer;
    using time_point_type = timer_type::time_point;

    enum parse_type { good, /*解析成功*/  bad, /*解析出错*/  less, /*缺少数据*/  indeterminate, /*尚未明确*/ };
    using func_pack_parse_type = std::function<std::tuple<parse_type /*parse type*/, buffer::size_type /*pack length*/, int /*pack type*/>(const buffer&)>;
    using func_receive_cb_type = std::function<void(const sessionid_type& /*session id*/, const int& /*pack type*/, const char* /*data buff*/, const buffer::size_type& /*length*/)>;
    using func_disconn_cb_type = std::function<void(const sessionid_type& /*session id*/, const int& /*reason code*/, const std::string& /*message*/)>;
    using func_log_type        = std::function<void(const int& /*type*/, const char* /*message*/)>;

protected:
    sessionid_type session_id_{0};
    std::atomic_bool disconnected_{false};

    func_pack_parse_type func_pack_parse_method_;
    func_receive_cb_type func_receive_callback_;
    func_disconn_cb_type func_disconnect_callback_;

public:
    DISABLE_COPY_ASSIGN(session);

    explicit session(func_pack_parse_type pack_parse_method, func_receive_cb_type receive_callback, func_disconn_cb_type disconnect_callback)
        : func_pack_parse_method_(pack_parse_method), func_receive_callback_(receive_callback), func_disconnect_callback_(disconnect_callback)
    {
    }

    virtual ~session()
    {
    }

    virtual void start() = 0;
    
    virtual void stop() = 0;
    
    virtual bool stopped() = 0;

    virtual const std::string local_endpoint() = 0;

    virtual const std::string remote_endpoint() = 0;

    const sessionid_type& session_id() const
    {
        return session_id_;
    }

    void set_session_id(const sessionid_type& session_id)
    {
        session_id_ = session_id;
    }
};

template<class TSocket, class TBuffer>
class socket_session : public session
{
public:
    using socket_type = TSocket;
    using buffer_type = TBuffer;
    using buff_sptr_type = std::shared_ptr<buffer_type>;
    using queue_type = std::queue<buff_sptr_type>;

private:
    mutex_type mutex_;              // Mutex
    socket_type socket_;            // Socket
    buffer_type recv_buffer_;       // 接收缓存
    queue_type send_queue_;         // 发送队列
    size_type send_queue_capacity_; // 发送队列容量(上限)
    buff_sptr_type send_buff_ptr_;  // 发送缓存

    size_type send_timeout_;        // 发送超时
    size_type recv_timeout_;        // 接收超时
    size_type heartbeat_interval_;  // 心跳间隔
    std::string heartbeat_data_;    // 心跳数据

    timer_type recv_deadline_;
    timer_type send_deadline_;
    timer_type heartbeat_timer_;
    timer_type non_empty_send_queue_;

public:
    explicit socket_session(socket_type socket,
                            func_pack_parse_type pack_parse_method,
                            func_receive_cb_type receive_callback,
                            func_disconn_cb_type disconnect_callback,
                            const size_type &send_queue_capacity = 0) noexcept
        : session(pack_parse_method, receive_callback, disconnect_callback),
          socket_(std::move(socket)),
          send_queue_capacity_(send_queue_capacity),
          recv_deadline_(socket_.get_executor()),
          send_deadline_(socket_.get_executor()),
          heartbeat_timer_(socket_.get_executor()),
          non_empty_send_queue_(socket_.get_executor())
    {
        recv_deadline_.expires_at(time_point_type::max());
        send_deadline_.expires_at(time_point_type::max());
        heartbeat_timer_.expires_at(time_point_type::max());
        non_empty_send_queue_.expires_at(time_point_type::max());
    }

    virtual ~socket_session()
    {
        stop();
    }

    void set_options(const int& send_timeout = 30, const int& recv_timeout = 30, const int& heartbeat_interval = 10, const std::string& heartbeat_data = "")
    {
        lock_guard_type lk(mutex_);
        send_timeout_ = send_timeout;
        recv_timeout_ = recv_timeout;
        heartbeat_interval_ = heartbeat_interval;
        heartbeat_data_ = heartbeat_data;
    }

    virtual void start() override
    {
        // 重置断开状态标识
        disconnected_ = false;
        // 启动接收/发送链
        handle_recv();
        if (recv_timeout_ > 0)
        {
            recv_deadline_.async_wait(std::bind(&socket_session<TSocket, TBuffer>::check_deadline, std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this()), std::ref(recv_deadline_)));
        }
        handle_send();
        if (send_timeout_ > 0)
        {
            send_deadline_.async_wait(std::bind(&socket_session<TSocket, TBuffer>::check_deadline, std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this()), std::ref(send_deadline_)));
        }
    }
    
    virtual void stop() override
    {
        lock_guard_type lk(mutex_);
        //handle_stop(error_code::normal_error, "active close");
        // 不直接调用handle_stop 关闭socket让连接自行释放
        if (socket_.is_open())
        {
            socket_.close();
        }
    }
    
    virtual bool stopped() override
    {
        return !socket_.is_open();
    }

    int async_send(const char* data, const buffer::size_type& length)
    {
        if (!data || length == 0 || length > buffer::constant::max_pack_size)
        {
            return error_code::normal_error;
        }

        lock_guard_type lk(mutex_);
        if (stopped())
        {
            return error_code::session_stopped;
        }
        if (send_queue_capacity_ == 0 || send_queue_.size() < send_queue_capacity_)
        {
            send_queue_.emplace(std::make_shared<buffer>(data, length));
            non_empty_send_queue_.expires_at(time_point_type::min());

            return error_code::ok;
        }
        else
        {
            return error_code::queue_full;
        }
    }

    const std::string local_endpoint() override
    {
        lock_guard_type lk(mutex_);
        if (socket_.is_open())
        {
            return socket_.local_endpoint().address().to_string();
        }
        else
        {
            return "";
        }
    }

    const std::string remote_endpoint() override
    {
        lock_guard_type lk(mutex_);
        if (socket_.is_open())
        {
            return socket_.remote_endpoint().address().to_string();
        }
        else
        {
            return "";
        }
    }

private:

    void handle_stop(const int& error, const std::string& message)
    {
        // 可能会在断开回调中进行重连 所以必须先响应回调
        // 否则全部停止后io_context没有任务就会直接结束运行
        if (!disconnected_)
        {
            disconnected_ = true;
            func_disconnect_callback_(session_id(), error, message);
        }
        {
            lock_guard_type lk(mutex_);
            if (socket_.is_open())
            {
                boost::system::error_code ec;
                socket_.close(ec);
            }
            // 终止计时器
            recv_deadline_.cancel();
            send_deadline_.cancel();
            non_empty_send_queue_.cancel();
            heartbeat_timer_.cancel();
            // 清空缓存
            recv_buffer_.clear();
            queue_type temp_queue;
            send_queue_.swap(temp_queue);
            send_buff_ptr_ = nullptr;
        }
    }

    void handle_recv()
    {
        lock_guard_type lk(mutex_);
        if (stopped())
        {
            return;
        }
        if (recv_timeout_ > 0)
        {
            recv_deadline_.expires_after(asio::chrono::seconds(recv_timeout_));
        }
        handle_async_recv();
    }

    void handle_async_recv()
    {
        auto self_ = std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this());
        socket_.async_receive(asio::buffer(recv_buffer_.writable_buff(), recv_buffer_.writable_size()), [this, self_](std::error_code ec, std::size_t bytes_transferred) {
            if (!ec)
            {
                // 更新接收缓存有效长度
                recv_buffer_.push_cache(bytes_transferred);

                while (true)
                {
                    // 解析接收缓存数据
                    parse_type result = parse_type::indeterminate;
                    int pack_type = 0;
                    int pack_size = 0;
                    std::tie(result, pack_size, pack_type) = func_pack_parse_method_(recv_buffer_);
                    if (result == parse_type::good)
                    {
                        // 将解析出的包回调给业务层
                        if (func_receive_callback_)
                        {
                            func_receive_callback_(session_id(), pack_type, recv_buffer_.data(), pack_size);
                        }
                        recv_buffer_.pop_cache(pack_size);
                    }
                    else if (result == parse_type::less)
                    {
                        // 整理缓存 继续解包
                        recv_buffer_.move2head();
                        // 不足一包 继续接收
                        handle_recv();
                        break;
                    }
                    else /*(result == parse_type::bad || result == parse_type::indeterminate)*/
                    {
                        // 解包异常 停止
                        handle_stop(error_code::packet_error, "parse failed");
                        break;
                    }
                }
            }
            else
            {
                // 接收异常 停止
                handle_stop(ec.value(), ec.message());
            }
        });
    }

    void handle_send()
    {
        lock_guard_type lk(mutex_);
        if (stopped())
        {
            return;
        }
        if (send_buff_ptr_ && !send_buff_ptr_->empty())
        {
            // 缓存数据未发完时继续发送
            handle_async_send();
        }
        else
        {
            // 缓存发送完则判断发送队列
            if (send_queue_.empty())
            {
                // 无数据时挂起等待数据
                non_empty_send_queue_.expires_at(time_point_type::max());
                non_empty_send_queue_.async_wait(std::bind(&socket_session<TSocket, TBuffer>::handle_send, std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this())));
                // 设置心跳定时器
                if (heartbeat_interval_ > 0 && !heartbeat_data_.empty())
                {
                    heartbeat_timer_.expires_after(asio::chrono::seconds(heartbeat_interval_));
                    heartbeat_timer_.async_wait(std::bind(&socket_session<TSocket, TBuffer>::check_heartbeat, std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this())));
                }
            }
            else
            {
                // 设置发送超时
                if (send_timeout_ > 0)
                {
                    send_deadline_.expires_after(asio::chrono::seconds(send_timeout_));
                }
                // 取队列数据并发送
                send_buff_ptr_ = send_queue_.front();
                send_queue_.pop();
                handle_async_send();
            }
        }
    }

    void handle_async_send()
    {
        auto self_ = std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this());
        // 这里使用async_send是为了支持asio::ip::udp::socket，否则使用asio::async_write()可以保证一次性发完才响应代码更简洁
        // TODO:后续可以看下asio的condition设置发送结束条件达到与asio::async_write()相同的效果，暂时先判断缓存buffer剩余数据
        socket_.async_send(asio::buffer(send_buff_ptr_->data(), send_buff_ptr_->size()), [this, self_](std::error_code ec, std::size_t bytes_transferred) {
            if (!ec)
            {
                {
                    lock_guard_type lk(mutex_);
                    if (send_buff_ptr_)
                    {
                        send_buff_ptr_->pop_cache(bytes_transferred);
                    }
                }
                // 继续检测 发送
                handle_send();
            }
            else
            {
                // 发送异常 停止
                handle_stop(ec.value(), ec.message());
            }
        });
    }

    void check_deadline(timer_type &deadline)
    {
        lock_guard_type lk(mutex_);
        if (stopped())
        {
            return;
        }
        if (deadline.expiry() <= timer_type::clock_type::now())
        {
            // 超时 不调用handle_stop() 关闭socket由接收发送响应来调用关闭
            if (socket_.is_open())
            {
                boost::system::error_code ec;
                socket_.close(ec);
            }
        }
        else
        {
            // 挂起 继续
            deadline.async_wait(std::bind(&socket_session<TSocket, TBuffer>::check_deadline, std::dynamic_pointer_cast<socket_session<TSocket, TBuffer>>(shared_from_this()), std::ref(deadline)));
        }
    }

    void check_heartbeat()
    {
        if (stopped())
        {
            return;
        }
        if (heartbeat_timer_.expiry() <= timer_type::clock_type::now())
        {
            // 超时 发送心跳
            async_send(heartbeat_data_.c_str(), heartbeat_data_.length());
        }
    }
};

// explicit class declaration
// TCP
using tcp_socket = asio::ip::tcp::socket;
using tcp_session = socket_session<tcp_socket, buffer>;
// UDP
using udp_socket = asio::ip::udp::socket;
using udp_session = socket_session<udp_socket, buffer>;

} // namespace utility
} // namespace dy

#endif