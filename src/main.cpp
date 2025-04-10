// main.cpp
#include "LlamaInference.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <nlohmann/json.hpp>
#include <httplib.h>

// FTXUI
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;
using json = nlohmann::json;

const std::string APP_VERSION = "v0.0.1";

const std::string MCP_SERVER_URL = "http://localhost:8000";
const std::string SSE_PATH = "/sse";
const std::string MCP_POST_PATH = "/message";

std::mutex response_mutex;
std::atomic<bool> is_streaming = false;
std::string response = "";

std::mutex queue_mutex;
std::deque<std::string> context_messages;
std::condition_variable context_cv;

void StreamChat(LlamaInference& llama, const std::string& prompt, std::function<void()> redraw) {
    is_streaming = true;

    std::string full_prompt;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (const auto& msg : context_messages) {
            full_prompt += msg + "\n";
        }
    }
    full_prompt += prompt;

    llama.chat(full_prompt, true, response, redraw);
    is_streaming = false;
    redraw();
}

void StartMCPClient() {
    std::thread([] {
        httplib::Client cli(MCP_SERVER_URL);
        cli.set_read_timeout(3600, 0); // long-lived SSE

        // Get the SSE stream from the server
        auto res = cli.Get(SSE_PATH.c_str());
        if (res && res->status == 200) {
            std::istringstream stream(res->body);
            std::string line;
            std::string data_buffer;
            while (std::getline(stream, line)) {
                if (line.rfind("data:", 0) == 0) {
                    data_buffer += line.substr(5) + "\n";
                } else if (line.empty()) {
                    if (!data_buffer.empty()) {
                        try {
                            auto json_msg = json::parse(data_buffer);
                            std::string msg = json_msg["content"];
                            {
                                std::lock_guard<std::mutex> lock(queue_mutex);
                                context_messages.push_back(msg);
                            }
                            context_cv.notify_one();
                        } catch (...) {}
                        data_buffer.clear();
                    }
                }
            }
        } else {
            std::cerr << "Failed to get SSE stream from MCP server!" << std::endl;
        }
    }).detach();
}

void PostResponseToMCP(const std::string& content) {
    httplib::Client cli(MCP_SERVER_URL);
    json payload = {
        {"content", content},
        {"role", "assistant"}
    };
    cli.Post(MCP_POST_PATH.c_str(), payload.dump(), "application/json");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size] [-ngl n_gpu_layers]" << std::endl;
        return 1;
    }

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

    LlamaInference llama(model_path, ngl, n_ctx);

    llama.setSystemPrompt(
        "You are a helpful AI assistant. Keep your responses concise, limited to 3-5 sentences maximum. "
        "Be direct and to the point. You have access to external context via MCP messages."
    );

    if (!llama.initialize()) {
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }

    StartMCPClient();

    auto screen = ScreenInteractive::Fullscreen();
    std::string user_prompt;

    Component user_prompt_box = Input(&user_prompt, "Type prompt here") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !user_prompt.empty() && !is_streaming) {
                response = "";
                std::string prompt_copy = user_prompt;
                user_prompt.clear();
                std::thread([&llama, prompt_copy, &screen]() {
                    StreamChat(llama, prompt_copy, [&] { screen.PostEvent(Event::Custom); });
                    PostResponseToMCP(response);
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
            paragraphAlignLeft(response) | border,
            separator(),
            user_prompt_box->Render()
        });
    });

    screen.Loop(renderer);
    return 0;
}

