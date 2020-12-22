#ifndef utility_include_utility_logger_logger_hpp
#define utility_include_utility_logger_logger_hpp

#ifdef USE_BOOST_LOGGER
#include "logger/boost_logger.hpp"
#else
#include "logger/easy_logger.hpp"
#endif

#endif//!utility_include_utility_logger_logger_hpp