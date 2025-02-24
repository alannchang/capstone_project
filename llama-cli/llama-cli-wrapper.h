#include <iostream>
#include <string>
#include <array>
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <future>
#include <functional>

class LlamaWrapper {
private:
    FILE* input_pipe;
    FILE* output_pipe;

public:
    struct Config {
        std::string binary_path;    // Path to llama-cli binary
        std::string model_path;     // Path to .gguf model file
        // Add other configuration options as needed
    };

    LlamaWrapper(const Config& config) {
        // Construct the command with proper arguments
        std::string cmd = config.binary_path + " -m " + config.model_path + " 2>&1";

        #ifdef _WIN32
            input_pipe = _popen(cmd.c_str(), "w");
            output_pipe = _popen(cmd.c_str(), "r");
            if (!input_pipe || !output_pipe) {
                throw std::runtime_error("Failed to create pipes to llama-cli process: " + cmd);
            }
        #else
            input_pipe = popen(cmd.c_str(), "w");
            output_pipe = popen(cmd.c_str(), "r");
            if (!input_pipe || !output_pipe) {
                throw std::runtime_error("Failed to create pipes to llama-cli process: " + cmd);
            }
        #endif
    }

    ~LlamaWrapper() {
        if (input_pipe) {
            #ifdef _WIN32
                _pclose(input_pipe);
            #else
                pclose(input_pipe);
            #endif
        }
        if (output_pipe) {
            #ifdef _WIN32
                _pclose(output_pipe);
            #else
                pclose(output_pipe);
            #endif
        }
    }

    // Send a prompt to the model
    void sendPrompt(const std::string& prompt) {
        if (fprintf(input_pipe, "%s\n", prompt.c_str()) < 0) {
            throw std::runtime_error("Failed to send prompt to llama-cli");
        }
        if (fflush(input_pipe) != 0) {
            throw std::runtime_error("Failed to flush prompt to llama-cli");
        }
    }

    // Read output with callback for streaming
    void streamOutput(std::function<void(const std::string&)> callback) {
        std::array<char, 1024> buffer;

        // Create async task to read output
        auto future = std::async(std::launch::async, [this, &buffer, callback]() {
            while (fgets(buffer.data(), buffer.size(), output_pipe) != nullptr) {
                std::string output(buffer.data());
                callback(output);
            }
        });

        // Wait for completion with timeout
        if (future.wait_for(std::chrono::seconds(30)) == std::future_status::timeout) {
            throw std::runtime_error("Output streaming timed out");
        }
    }

    // Simple synchronous read
    std::string readOutput() {
        std::array<char, 1024> buffer;
        std::string result;

        while (fgets(buffer.data(), buffer.size(), output_pipe) != nullptr) {
            result += buffer.data();
        }

        return result;
    }
};
