// main.cpp
#include "LlamaInference.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <sstream>
// FTXUI
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using json = nlohmann::json;
using namespace ftxui;

const std::string APP_VERSION = "v0.0.2";
const std::string MCP_SERVER_URL = "http://localhost:8000";
const std::string SSE_PATH = "/sse";
const std::string MCP_POST_PATH = "/message";

std::mutex response_mutex;
std::atomic<bool> is_streaming = false;
std::string response = "";
std::string session_id = "";
std::mutex queue_mutex;
std::condition_variable context_cv;
std::queue<std::string> context_messages;

void SendMCPInitialize() {
    httplib::Client cli(MCP_SERVER_URL);
    json init_req = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"capabilities", json::object()},
            {"clientInfo", { {"name", "llama.cpp"}, {"version", "1.0.0"} }},
            {"protocolVersion", "2024-11-05"}
        }}
    };
    std::stringstream url;
    url << MCP_POST_PATH << "?sessionId=" << session_id;
    cli.Post(url.str().c_str(), init_req.dump(), "application/json");
}

void StartMCPClient() {
    std::thread([]() {
        httplib::Client cli(MCP_SERVER_URL);
        cli.set_read_timeout(60, 0);

        auto res = cli.Get(SSE_PATH.c_str());
        if (res && res->status == 200) {
            std::istringstream stream(res->body);
            std::string line;
            std::string data_buffer;
            while (std::getline(stream, line)) {
                if (line.rfind("event:", 0) == 0) {
                    std::string event_type = line.substr(6);
                    std::getline(stream, line);
                    if (line.rfind("data:", 0) == 0) {
                        std::string data = line.substr(5);
                        if (event_type.find("endpoint") != std::string::npos) {
                            auto pos = data.find("sessionId=");
                            if (pos != std::string::npos) {
                                session_id = data.substr(pos + 10);
                                SendMCPInitialize();
                            }
                        }
                    }
                }
            }
        }
    }).detach();
}

std::string GetContextFromMCP(const std::string& prompt) {
    httplib::Client cli(MCP_SERVER_URL);
    json req = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "context/suggest"},
        {"params", {{"prompt", prompt}}}
    };
    std::stringstream url;
    url << MCP_POST_PATH << "?sessionId=" << session_id;
    auto res = cli.Post(url.str().c_str(), req.dump(), "application/json");
    if (res && res->status == 200) {
        try {
            auto json_resp = json::parse(res->body);
            if (json_resp.contains("result") && json_resp["result"].contains("augmented")) {
                return json_resp["result"]["augmented"].get<std::string>();
            }
        } catch (...) {
            return prompt;
        }
    }
    return prompt;
}

void StreamChat(LlamaInference& llama, std::string prompt, std::function<void()> redraw) {
    is_streaming = true;
    std::string enriched_prompt = GetContextFromMCP(prompt);
    llama.chat(enriched_prompt, true, response, redraw);
    is_streaming = false;
    redraw();
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
        "Be direct and to the point. Avoid lengthy explanations or introductions."
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

