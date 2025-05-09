#include "LlamaInference.h"
#include "Logger.h"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <vector>
#include <sstream>

namespace fs = std::filesystem;
using namespace ftxui;

const std::string APP_VERSION = "0.1.0";

// Global state for chat
std::mutex response_mutex;
std::atomic<bool> is_streaming{false};
std::string response = "";

// Function to handle streaming chat responses with true token-by-token streaming
// without maintaining chat history
void StreamWithoutHistory(LlamaInference& llama, const std::string& prompt, std::function<void()> redraw) {
    is_streaming = true;
    
    // First, add the user message to display
    {
        std::lock_guard<std::mutex> lock(response_mutex);
        response = "You: " + prompt + "\n\nAssistant: ";
    }
    redraw();
    
    // Use generateWithCallback for true token-by-token streaming
    llama.generateWithCallback(prompt, [&](const std::string& token) {
        // Update the response with each token
        {
            std::lock_guard<std::mutex> lock(response_mutex);
            response += token;
        }
        redraw();
    });
    
    is_streaming = false;
    redraw();
}

void print_usage() {
    std::cerr << "Usage: llm-ui <model_path> [options]\n"
              << "Options:\n"
              << "  --gpu-layers <n>     Number of GPU layers to use (default: 0)\n"
              << "  --ctx-size <n>       Context size (default: 4096)\n"
              << "  --help               Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    std::string model_path = argv[1];
    int gpu_layers = 0;
    int context_size = 4096;
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "--gpu-layers" && i + 1 < argc) {
            gpu_layers = std::stoi(argv[++i]);
        } else if (arg == "--ctx-size" && i + 1 < argc) {
            context_size = std::stoi(argv[++i]);
        }
    }
    
    // Check if model file exists
    if (!fs::exists(model_path)) {
        std::cerr << "Error: Model file not found: " << model_path << std::endl;
        return 1;
    }
    
    // Initialize logger
    Logger::init("llm-ui", false);
    spdlog::info("Starting llm-ui with model: {}", std::string(model_path));
    spdlog::info("GPU Layers: {}, Context Size: {}", gpu_layers, context_size);
    
    // Initialize LlamaInference
    LlamaInference llama(model_path, gpu_layers, context_size);
    llama.setSystemPrompt("You are a helpful AI assistant. Answer questions concisely and accurately. Keep responses brief.");
    
    if (!llama.initialize()) {
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }
    
    response = "Model loaded successfully! You can now start chatting.";
    
    // UI Setup
    auto screen = ScreenInteractive::Fullscreen();
    
    // User input component
    std::string user_prompt;
    Component user_prompt_box = Input(&user_prompt, "Type your message...") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !user_prompt.empty() && !is_streaming) {
                std::string prompt_copy = user_prompt;
                user_prompt.clear();
                
                // Process in a separate thread
                std::thread([&llama, prompt_copy, &screen]() {
                    StreamWithoutHistory(llama, prompt_copy, [&] { 
                        screen.PostEvent(Event::Custom);
                    });
                }).detach();
                
                return true;
            }
            return false;
        });
    
    // Create main container
    auto container = Container::Vertical({
        user_prompt_box
    });
    
    // Create renderer
    auto renderer = Renderer(container, [&] {
        return vbox({
            text("LLaMA Chat " + APP_VERSION) | bold | center,
            separator(),
            // Make sure response text wraps properly
            paragraph(response) | flex | border,
            separator(),
            user_prompt_box->Render()
        });
    });
    
    // Run the UI loop
    screen.Loop(renderer);
    
    return 0;
} 
