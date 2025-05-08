#ifndef LOGGER_H
#define LOGGER_H

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& loggerName = "llama_app", 
              const std::string& logFile = "logs/llama_app.log",
              bool consoleOutput = false,
              spdlog::level::level_enum level = spdlog::level::info) {
        
        try {
            std::vector<spdlog::sink_ptr> sinks;
            
            // Create directory for logs if it doesn't exist
            std::string directory = "logs";
            try {
                if (!std::filesystem::exists(directory)) {
                    std::filesystem::create_directories(directory);
                }
            } catch(const std::exception& e) {
                std::cerr << "Failed to create log directory: " << e.what() << std::endl;
            }
            
            // Add rotating file sink (5MB max size, 3 rotated files) - always include file logging
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFile, 1024 * 1024 * 5, 3);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
            file_sink->set_level(level);
            sinks.push_back(file_sink);
            
            // Add console sink if requested
            console_sink_ = nullptr;
            if (consoleOutput) {
                console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink_->set_pattern("[%^%l%$] %v");
                console_sink_->set_level(level);
                sinks.push_back(console_sink_);
            }
            
            // Create and register logger
            logger_ = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
            logger_->set_level(level);
            logger_->flush_on(spdlog::level::err);
            
            spdlog::register_logger(logger_);
            spdlog::set_default_logger(logger_);
            
            // Use direct spdlog call
            spdlog::info("Logger initialized");
        }
        catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        }
    }
    
    void setLevel(spdlog::level::level_enum level) {
        if (logger_) {
            logger_->set_level(level);
        }
    }
    
    void setConsoleOutput(bool enabled) {
        if (!console_sink_) return;
        
        if (enabled) {
            console_sink_->set_level(logger_->level());
        } else {
            console_sink_->set_level(spdlog::level::off);
        }
        // Use direct spdlog call
        spdlog::debug("Console output {}", enabled ? "enabled" : "disabled");
    }
    
    std::shared_ptr<spdlog::logger> getLogger(const std::string& name = "") {
        if (name.empty()) {
            return spdlog::default_logger();
        }
        return spdlog::get(name);
    }
    
private:
    Logger() {} // Private constructor for singleton
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink_;
};

// Convenience macros for logging
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

#endif // LOGGER_H 