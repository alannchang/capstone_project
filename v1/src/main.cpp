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
#include <pybind11/embed.h>
#include "python_bindings.hpp"
// json
#include <nlohmann/json.hpp>
#include <fstream>

#include "tool_manager.hpp"

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

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size] [-ngl n_gpu_layers]" << std::endl;
        return 1;
    }
    
    // parse command line arguments
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
    // load tools from json file
    nlohmann::json tool_schema;
    std::ifstream file("runtime-deps/tools.json");
    file >> tool_schema;
/* 
    // build system prompt
    std::string task_instruction = R"(You are an assistant that manages a Gmail inbox.  You have access to a set of tools. When using tools, make calls in a single JSON array (DO NOT USE MARKDOWN): 

    [{"name": "tool_call_name", "arguments": {"arg1": "value1", "arg2": "value2"}}, ... (additional parallel tool calls as needed)]

    If no tool is suitable, state that explicitly. If the user's input lacks required parameters, ask for clarification. Do not interpret or respond until tool results are returned. Once they are available, process them or make additional calls if needed. For tasks that don't require tools, such as casual conversation or general advice, respond directly in plain text. The available tools are:)";

    std::string available_tools = tool_schema.dump(2);

    std::string system_prompt = 
        // "<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n" 
        task_instruction 
        + "\n\n" 
        + available_tools; 
        // + "\n<|eot_id|><|start_header_id|>user<|end_header_id|>\n";
*/
    std::string system_prompt = "You are a helpful assistant.";
    // initialize LlamaInference object
    LlamaInference llama(model_path, ngl, n_ctx);

    // Set system prompt
    llama.setSystemPrompt(system_prompt);

    if (!llama.initialize()) {
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }

    // UI Setup

    auto screen = ScreenInteractive::Fullscreen();

    std::mutex response_mutex;
    std::atomic<bool> is_streaming = false;

    std::string prompt;
    Component user_prompt_box = Input(&prompt, "Type prompt here") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !prompt.empty() && !is_streaming) {
                response = "";
                std::string _prompt = prompt;
                prompt.clear();
                std::thread([&llama, _prompt, &screen]() {
                    StreamChat(llama, _prompt, [&] { screen.PostEvent(Event::Custom); });
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
