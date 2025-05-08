#include "LlamaInference.h"
#include "Logger.h"
#include <iostream>
#include <cstring>
#include <filesystem>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -m MODEL_PATH    Path to the model file (required)" << std::endl;
    std::cout << "  -c CONTEXT_SIZE  Context size (default: 2048)" << std::endl;
    std::cout << "  -ngl LAYERS      Number of GPU layers (default: 99)" << std::endl;
    std::cout << "  --show-logs      Show logs in console (default: logs are only written to files)" << std::endl;
    std::cout << "  -l LEVEL         Log level: trace, debug, info, warn, error (default: info)" << std::endl;
    std::cout << "  -h, --help       Display this help message and exit" << std::endl;
}

int main(int argc, char** argv) {
    // Initialize the logger first
    try {
        std::filesystem::create_directories("logs");
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to create logs directory: " << e.what() << std::endl;
    }
    
    // Default parameters
    std::string model_path;
    int ngl = 99;
    int n_ctx = 2048;
    bool show_logs = false;  // Default to quiet mode
    spdlog::level::level_enum log_level = spdlog::level::info;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
                model_path = argv[++i];
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "--show-logs") == 0) {
                show_logs = true;
            } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
                std::string level = argv[++i];
                if (level == "trace") log_level = spdlog::level::trace;
                else if (level == "debug") log_level = spdlog::level::debug;
                else if (level == "info") log_level = spdlog::level::info;
                else if (level == "warn") log_level = spdlog::level::warn;
                else if (level == "error") log_level = spdlog::level::err;
                else {
                    std::cerr << "Unknown log level: " << level << std::endl;
                    return 1;
                }
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                printUsage(argv[0]);
                return 0;
            } else {
                std::cerr << "Unknown option: " << argv[i] << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        } catch (std::exception& e) {
            std::cerr << "Error parsing arguments: " << e.what() << std::endl;
            return 1;
        }
    }
    
    if (model_path.empty()) {
        std::cerr << "Model path is required." << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    // Initialize logger with specified options
    Logger::getInstance().init("chat_app", "logs/chat_app.log", show_logs, log_level);
    
    if (show_logs) {
        std::cout << "Console logging enabled. Logs will be displayed and written to logs/chat_app.log." << std::endl;
    }
    
    LOG_INFO("Chat application starting...");
    LOG_INFO("Model path set to: {}", model_path.c_str());
    LOG_INFO("Context size set to: {}", n_ctx);
    LOG_INFO("GPU layers set to: {}", ngl);
    
    // Create and initialize the LlamaInference object
    LOG_INFO("Creating and initializing LlamaInference");
    LlamaInference llama(model_path, ngl, n_ctx);
    if (!llama.initialize()) {
        LOG_CRITICAL("Failed to initialize LlamaInference");
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }
    
    LOG_INFO("Model loaded successfully, entering chat mode");
    std::cout << "Model loaded successfully. Enter your messages (empty line to exit):" << std::endl;
    
    // Main chat loop
    while (true) {
        std::cout << "\033[32m> \033[0m";
        std::string user_input;
        std::getline(std::cin, user_input);
        
        if (user_input.empty()) {
            LOG_INFO("Empty input, exiting chat loop");
            break;
        }
        
        LOG_INFO("Received user input: {}", user_input.c_str());
        std::cout << "\033[33m";
        // Enable streaming output by passing true
        std::string response = llama.chat(user_input, true);
        std::cout << "\n\033[0m";
        LOG_DEBUG("Response generated, length: {}", response.length());
    }
    
    LOG_INFO("Chat application shutting down");
    return 0;
}
