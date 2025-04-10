// llama.cpp
#include "LlamaInference.h"
#include "McpClient.hpp"
#include <nlohmann/json.hpp>
#include <future>
#include <chrono>

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

using namespace ftxui;

const std::string APP_VERSION = "v0.0.1";

std::mutex response_mutex;
std::atomic<bool> is_streaming = false;
std::string response = "";

void SendPromptToMCP(McpClient& client, const std::string& prompt) {
    try {
        nlohmann::json message;
        message["prompt"] = prompt;  

        client.sendMessage(message);
        nlohmann::json receivedMessage = client.receiveMessage();
        if (!receivedMessage.is_null() && receivedMessage.contains("response")) {
            response = receivedMessage["response"].get<std::string>();
        } else {
            response = "Error: Invalid response format from MCP server.";
        }
    } catch (const std::exception& e) {
        response = "Error: " + std::string(e.what());
    }
}

void StreamChat(LlamaInference& llama, std::string prompt, std::function<void()> redraw) {
    is_streaming = true;
    llama.chat(prompt, true, response, redraw);
    is_streaming = false;
    redraw();
}


int main(int argc, char** argv) {

    //
    // Llama.cpp model setup/initialization
    //
    bool use_mcp = false; 
    std::string model_path;
    int ngl = 99;
    int n_ctx = 2048;

    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) { 
                model_path = argv[++i];
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-mcp") == 0) {
            }
            else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            }
        } catch (std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }    

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size] [-ngl n_gpu_layers] [-mcp]" << std::endl;
        return 1;
    }
    if (model_path.empty()) {
        std::cout << "Model path is required." << std::endl;
        return 1;
    }
    //use_mcp = true;
    
    // Create and initialize the LlamaInference object
    LlamaInference llama(model_path, ngl, n_ctx, use_mcp);
    
    

    // Create an instance of McpClient
    McpClient mcpClient("127.0.0.1", 5000);
    if(use_mcp){        // Try to connect to the MCP server
        if (!mcpClient.connect()) {
            std::cerr << "Failed to connect to MCP server." << std::endl;
            return 1;
        }
        std::cout << "Connected to MCP server" << std::endl;
    } else {
         if (argc < 2) {
            std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size] [-ngl n_gpu_layers] [-mcp]" << std::endl;
            return 1;
        }
        if (model_path.empty()) {
            std::cout << "Model path is required." << std::endl;
            return 1;
        }
    }
   

    //
    // UI Setup
    //

     // Set a system prompt to control the model's behavior
    llama.setSystemPrompt(
        "You are a helpful AI assistant. Keep your responses concise, limited to 3-5 sentences maximum. "
        "Be direct and to the point. Avoid lengthy explanations or introductions."
    );

    if (!llama.initialize()) {
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }

    auto screen = ScreenInteractive::Fullscreen();
    
    auto prompt_send = [&](std::string prompt_copy, auto& screen){
          std::thread([&, prompt_copy, &screen]() {
                
                StreamChat(llama, prompt_copy, [&] { screen.PostEvent(Event::Custom); });
                if(use_mcp && llama.getForwardToMCP()){
                     std::future<void> future_send_mcp = std::async(std::launch::async, [&mcpClient,prompt_copy]()
                        {SendPromptToMCP(mcpClient, prompt_copy);});
                        auto status = future_send_mcp.wait_for(std::chrono::seconds(3));
                        if(status != std::future_status::ready) {
                            std::cerr << "Error: Send to MCP timed out!" << std::endl;
                        }
                }
                }).detach();
        }
    };

    std::string user_prompt;
    Component user_prompt_box = Input(&user_prompt, "Type prompt here") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !user_prompt.empty() && !is_streaming) {
                response = "";
                std::string prompt_copy = user_prompt;
                user_prompt.clear();
                prompt_send(prompt_copy, screen);
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
          paragraphAlignLeft(response) | border,
          separator(),
          user_prompt_box->Render()
      });
    });

    screen.Loop(renderer);
    return 0;
}
