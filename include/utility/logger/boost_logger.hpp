#ifndef utility_include_utility_logger_boost_logger_hpp
#define utility_include_utility_logger_boost_logger_hpp

#include "boost/filesystem.hpp"
#include "boost/log/core.hpp"
#include "boost/log/trivial.hpp"
#include "boost/log/expressions.hpp"
#include "boost/log/sinks.hpp"
#include "boost/log/utility/setup.hpp"
#include "boost/log/sources/severity_logger.hpp"
#include "boost/log/sources/record_ostream.hpp"
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/attributes/mutable_constant.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

namespace dy
{
namespace utility
{
/**
 * add by tangzt 20200305
 * boost log 封装及日志规则暂定
 * 统一使用log.conf进行配置，不同日志等级包含不同的attribute，通常情况下无需更改
 * 输出到控制台的日志格式与info相同
 * ============================================================
 * 日志分为以下等级，从上至下越来越详细：
 *  trace：不包含任何attribute.通常情况下还是推荐使用info
 *  info【默认】：包含TimeStamp，Severity，File和Function; 记录正常运行状态·业务数据时使用
 *  debug：包含TimeStamp, Severity, File, Line, Function, ProcessID, ThreadID; 在容易出现错误的代码块使用. 受掌控的错误·非法数据建议使用这个等级
 *  warning：包含TimeStamp，Severity，Scope，File, Function, ProcessID, ThreadID; 通常不会抵达，需要引起重视的代码块使用. 一般的catch建议使用这个等级
 *  error：包含所有提供的attribute; 在判断已经出现了【严重】错误，出现后程序可能不能维持正常运行运行的代码块中使用.
 *  fatal: 暂时和error相同. 有需求再区分
 * ============================================================
 * 以下attribute是经过验证不会输出空值，在配置中使用的：
 *  TimeStamp：时间戳，精确到微秒
 *  Severity: 日志等级
 *  LineID：日志计数，即程序启动至今记录了多少条日志
 *  ProcessID：进程序号
 *  ThreadID：线程序号
 *  Scope：调用堆栈，较长
 *  File: 当前文件，不包含路径
 *  Function：当前函数名
 *  Line: 当前行
 * =============================================================
 */

/**
 * add by yangjl 2020-08-29
 * __FILE__ 宏会带路径，想去掉路径可以使用strstr查找截断，但是影响运行效率
 * 在编译选项中添加 -D__FILENAME__='"$(subst $(dir $<),,$<)"' 文件名称宏
 * 此方法是编译期由编译器进行字符串截取，不会影响运行时效率
 * 如果不需要特殊处理文件路径，则默认仍然使用__FILE__宏做文件名称
 */
#ifndef __FILENAME__
#define __FILENAME__ __FILE__
#endif

/**
 * logger defined
 */
#define UTILITY_LOGGER(lvl)                        \
    BOOST_LOG_TRIVIAL(lvl)                             \
        << boost::log::add_value("File", __FILENAME__) \
        << boost::log::add_value("Line", __LINE__)     \
        << boost::log::add_value("Function", __FUNCTION__)

/**
 * boost log
 */
namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace attrs = boost::log::attributes;

class UtilityLogger
{
public:
    static bool InitLoggerEnv(std::string conf_file)
    {
        using namespace logging::trivial;

        std::ifstream conf_ifs(conf_file);
        if (conf_ifs.is_open())
        {
            try
            {
                // 基础属性
                logging::add_common_attributes();
                logging::register_simple_formatter_factory<severity_level, char>("Severity");
                logging::register_simple_filter_factory<severity_level, char>("Severity");
                // 配置属性
                logging::init_from_stream(conf_ifs);
            }
            catch (const std::exception& ep)
            {
                std::cout << "InitLoggerEnv failed err:" << ep.what() << std::endl;
                return false;
            }
            conf_ifs.close();
        }
        
        return true;
    }

private:
};
}//namespace utility
}//namespace dy

#endif//!utility_include_utility_logger_boost_logger_hpp