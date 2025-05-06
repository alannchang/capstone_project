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


void StreamChat(LlamaInference& llama, ToolManager& tool_manager, std::string prompt, std::function<void()> redraw) {
    is_streaming = true;

    while (1) {
        response.clear();
        llama.chat(prompt, true, response, redraw);
        std::string raw_output = response;

        auto tool_result = tool_manager.handle_tool_call(raw_output);

        if (tool_result.has_value()) {
            response += "\n\n[Tool Calling in progress... Please wait.]\n";
            prompt = tool_result.value();
        } else {
            break;
        }
        redraw();
    }
    is_streaming = false;
    redraw();
}

void test_tool_directly(ToolManager& tool_manager) {
    std::string test_json = R"([
        {
            "name": "list_messages",
            "arguments": {
                "max_results": 2,
                "query": "is:unread"
            }
        }
    ])";

    std::cerr << "[TEST] Calling handle_tool_call()...\n";
    auto result = tool_manager.handle_tool_call(test_json);

    if (result.has_value()) {
        std::cerr << "[TEST] Tool call succeeded. Result:\n" << result.value() << std::endl;
    } else {
        std::cerr << "[TEST] Tool call failed or returned no value.\n";
    }
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

    // initialize Gmail api wrapper
    pybind11::scoped_interpreter guard{};
    GmailManagerWrapper gmail_mgr("runtime-deps/credentials.json", "runtime-deps/token.json");

    response = "Gmail Authorization Successful\n" + gmail_mgr.get_profile_str(gmail_mgr.get_profile());

    // load tools from json file
    nlohmann::json tool_schema;
    std::ifstream file("runtime-deps/tools.json");
    file >> tool_schema;

    // initialize tool manager
    ToolManager tool_manager;
    tool_manager.register_gmail_tools(gmail_mgr.get_instance());
    // test_tool_directly(tool_manager);
 
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

    std::string user_prompt;
    Component user_prompt_box = Input(&user_prompt, "Type prompt here") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !user_prompt.empty() && !is_streaming) {
                response = "";
                std::string combined_prompt = system_prompt + user_prompt /*+ "\n<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n"*/;
                user_prompt.clear();
                std::thread([&llama, &tool_manager, combined_prompt, &screen]() {
                    StreamChat(llama, tool_manager, combined_prompt, [&] { screen.PostEvent(Event::Custom); });
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
