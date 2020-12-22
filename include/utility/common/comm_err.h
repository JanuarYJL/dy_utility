#ifndef utility_include_utility_common_err_h
#define utility_include_utility_common_err_h

namespace dy
{
namespace utility
{
enum error_code : int
{
    __origin__ = -65536,    // 错误代码定义起始值

    queue_full,             // 队列已满
    queue_empty,            // 队列为空

    packet_less,            // 数据包不全
    packet_error,           // 数据包损坏

    session_full,           // 会话已满
    session_stopped,        // 连接已关闭
    session_not_exist,      // 连接不存在

    normal_error = -1,      // 一般错误
    ok = 0,                 // 成    功
};
}//namespace utility
}//namespace dy

#endif//!utility_include_utility_common_err_h