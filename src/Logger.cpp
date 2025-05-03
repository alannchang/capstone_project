#include "Logger.hpp"
#include <iostream> // For potential fallback logging to cerr

Logger::Logger(std::string log_filepath, LogLevel min_level)
    : min_level_(min_level) {
    // Open the log file in append mode
    log_stream_.open(log_filepath, std::ios::app);
    if (!log_stream_.is_open()) {
        // Fallback: Log an error to cerr if the file couldn't be opened
        std::cerr << "Error: Unable to open log file: " << log_filepath << std::endl;
    }
    log(LogLevel::INFO, "Logger initialized. Log file: ", log_filepath);
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level >= min_level_) {
         log_internal(level, message);
    }
}

void Logger::log_internal(LogLevel level, const std::string& message) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);

    // Format timestamp (e.g., YYYY-MM-DD HH:MM:SS)
    std::ostringstream timestamp_ss;
    timestamp_ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");

    // Acquire lock for thread-safe writing
    std::lock_guard<std::mutex> lock(log_mutex_);

    if (log_stream_.is_open()) {
        log_stream_ << "[" << timestamp_ss.str() << "] "
                    << "[" << level_to_string(level) << "] "
                    << message << std::endl;
    } else {
        // Fallback to cerr if the file stream isn't open
        std::cerr << "[" << timestamp_ss.str() << "] "
                  << "[" << level_to_string(level) << "] "
                  << message << std::endl;
    }
}

std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
} 
