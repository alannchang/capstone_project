// llama.cpp
#include "LlamaInference.h"
#include <iostream>
#include <cstring>
// FTXUI
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
// Thread support
#include <thread>
#include <atomic>
#include <mutex>
// pybind11
#include <filesystem>
#include <pybind11/embed.h>

using namespace ftxui;

const std::string APP_VERSION = "v0.0.1";

std::mutex response_mutex;
std::atomic<bool> is_streaming = false;
std::string response = "";

void StreamChat(LlamaInference& llama, std::string prompt, std::function<void()> redraw) {
    is_streaming = true;
    llama.chat(prompt, true, response, redraw);
    is_streaming = false;
    redraw();
}


int main(int argc, char** argv) {

    // Llama.cpp model setup/initialization

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size] [-ngl n_gpu_layers]" << std::endl;
        return 1;
    }
    
    // Parse command line arguments
    std::string model_path;
    int ngl = 99;
    int n_ctx = 2048;
    
    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
                model_path = argv[++i];
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            }
        } catch (std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }
    
    if (model_path.empty()) {
        std::cout << "Model path is required." << std::endl;
        return 1;
    }
    
    // Create and initialize the LlamaInference object
    LlamaInference llama(model_path, ngl, n_ctx);

    // Set a system prompt to control the model's behavior
    llama.setSystemPrompt(
        "You are a helpful AI assistant. Keep your responses concise, limited to 3-5 sentences maximum. "
        "Be direct and to the point. Avoid lengthy explanations or introductions."
    );

    if (!llama.initialize()) {
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }

    // Gmail API setup
    pybind11::scoped_interpreter guard{};
    
    try {
        // All this does is find the python script
        std::filesystem::path script_path = std::filesystem::current_path() / "../gmail-api/";
        script_path = std::filesystem::canonical(script_path);
        pybind11::module_::import("sys").attr("path").attr("insert")(0, script_path.string());

        pybind11::object api_module = pybind11::module_::import("api");

        pybind11::object manager_class = api_module.attr("GmailManager");
        pybind11::object api_manager = manager_class();
        
    } catch (const pybind11::error_already_set &e) {
        std::cerr << "Python error: " << e.what() << std::endl;

    }
    

    // UI Setup

    auto screen = ScreenInteractive::Fullscreen();

    std::mutex response_mutex;
    std::atomic<bool> is_streaming = false;

    std::string user_prompt;
    Component user_prompt_box = Input(&user_prompt, "Type prompt here") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !user_prompt.empty() && !is_streaming) {
                response = "";
                std::string prompt_copy = user_prompt;
                user_prompt.clear();
                std::thread([&llama, prompt_copy, &screen]() {
                    StreamChat(llama, prompt_copy, [&] { screen.PostEvent(Event::Custom); });
                }).detach();
                return true;
            }
            return false;
        });

    auto container = Container::Vertical({
        user_prompt_box
    });

    auto renderer = Renderer(container, [&] {
      return vbox({
          text("MaiMail " + APP_VERSION) | center,
          separator(),
          paragraphAlignLeft(response) | yflex | border,
          separator(),
          user_prompt_box->Render()
      });
    });

    screen.Loop(renderer);
    return 0;
}
