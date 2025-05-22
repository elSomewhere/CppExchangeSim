//
// Created by elanter on 5/16/25.
//

#ifndef LOGGING_H
#define LOGGING_H


#include <iostream>
#include <string>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>

// --- Logging ---
    enum class LogLevel {
        NONE, DEBUG, INFO, WARNING, ERROR, OTHER
    };

struct LoggerConfig {
    static inline LogLevel G_CURRENT_LOG_LEVEL = LogLevel::ERROR;
};

inline void LogMessage(LogLevel level, const std::string &source, const std::string &message) {
    if (level >= LoggerConfig::G_CURRENT_LOG_LEVEL) {
//        auto now_sys = std::chrono::system_clock::now();
//        auto now_c = std::chrono::system_clock::to_time_t(now_sys);
//        std::ostringstream oss;
//        oss << "[" << std::put_time(std::localtime(&now_c), "%T") << "] "
//            << "[" << static_cast<int>(level) << "] "
//            << "[" << source << "] "
//            << message << std::endl;
//        std::cerr << oss.str();
    }
}

#endif //LOGGING_H
