#ifndef utility_include_utility_logger_boost_logger_hpp
#define utility_include_utility_logger_easy_logger_hpp

#include "easyloggingpp/easylogging++.h"

INITIALIZE_EASYLOGGINGPP // 初始化

namespace dy
{
namespace utility
{
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
#define UTILITY_LOGGER(lvl) LOG(lvl)
#define trace   TRACE
#define debug   DEBUG
#define info    INFO
#define warning WARNING
#define error   ERROR
#define fatal   FATAL

class UtilityLogger
{
public:
    static bool InitLoggerEnv(std::string conf_file)
    {
        if (!conf_file.empty())
        {
            // Load configuration from file
            el::Configurations el_conf(conf_file);
            // Reconfigure single logger
            // el::Loggers::reconfigureLogger("default", conf);
            // Actually reconfigure all loggers instead
            el::Loggers::reconfigureAllLoggers(el_conf);
            // Now all the loggers will use configuration from file
        }
        return true;
    }

private:
};
}//namespace utility
}//namespace dy

#endif//!utility_include_utility_logger_easy_logger_hpp