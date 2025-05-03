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
#include "Logger.hpp"

using namespace ftxui;

// Configuration Constants
const std::string APP_VERSION = "v0.0.1";
const std::string CREDENTIALS_PATH = "runtime-deps/credentials.json";
const std::string TOKEN_PATH = "runtime-deps/token.json";
const std::string TOOLS_SCHEMA_PATH = "runtime-deps/tools.json";
const std::string DEBUG_LOG_PATH = "tool_debug.log";
const std::string APP_LOG_PATH = "app.log";

std::mutex response_mutex;
std::atomic<bool> is_streaming = false;
std::string response = "";


void StreamChat(LlamaInference& llama, ToolManager& tool_manager, std::string initial_prompt, std::function<void()> redraw) {
    is_streaming = true;
    std::string current_llm_prompt = initial_prompt;

    while (true) {
        std::string accumulated_this_turn;
        
        // Define the callback for Llama to use during generation
        auto token_callback = [&](const std::string& piece){
            std::lock_guard<std::mutex> lock(response_mutex);
            response += piece;         // Append to shared UI string
            accumulated_this_turn += piece; // Accumulate for tool parsing this turn
            redraw(); // Notify UI to redraw
        };

        // Call Llama using chatWithCallback (or generateWithCallback if chat state isn't needed for the prompt)
        // If StreamChat handles the user message history, maybe generateWithCallback is better.
        // Assuming LlamaInference::chatWithCallback manages history internally based on prior calls.
        std::string full_response_this_turn = llama.chatWithCallback(current_llm_prompt, token_callback);

        // Note: accumulated_this_turn should be equivalent to full_response_this_turn if callback logic is correct.
        // We parse the accumulated response for tools.
        std::optional<std::string> tool_result = tool_manager.handle_tool_call_string(accumulated_this_turn);

        if (tool_result.has_value()) {
            {
                std::lock_guard<std::mutex> lock(response_mutex);
                // Append tool result to the main response string for UI display
                response += "\n[Tool execution result:]\n" + tool_result.value() + "\n";
            }
            // Set the *result* of the tool call as the prompt for the next LLM turn
            current_llm_prompt = tool_result.value(); 
            redraw(); // Redraw after adding tool result
        } else {
            // No tool call detected/needed or tool execution failed in a way handle_tool_call_string returned nullopt
            // End the conversation loop
            break;
        }
    }
    is_streaming = false;
    redraw(); // Final redraw after streaming stops
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
    
    try {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
                model_path = argv[++i];
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Error parsing command line arguments: " << e.what() << std::endl;
        return 1;
    }
    
    if (model_path.empty()) {
        std::cerr << "Model path (-m) is required." << std::endl;
        return 1;
    }

    // --- Initialize Logger --- 
    Logger logger(APP_LOG_PATH, LogLevel::DEBUG); // Log DEBUG and above to app.log
    logger.log(LogLevel::INFO, "--- Application Starting --- Version: ", APP_VERSION);
    logger.log(LogLevel::DEBUG, "Parsed Args: model_path=", model_path, ", ngl=", ngl, ", n_ctx=", n_ctx);

    // --- Initialize Python & Gmail --- 
    pybind11::scoped_interpreter guard{};
    std::unique_ptr<GmailManagerWrapper> gmail_mgr;
    try {
        gmail_mgr = std::make_unique<GmailManagerWrapper>(CREDENTIALS_PATH, TOKEN_PATH, logger);
        // Initial response string setup:
        response = "Gmail Authorization Successful\n" + gmail_mgr->get_profile_str(gmail_mgr->get_profile());
    } catch (const std::exception& e) {
        logger.log(LogLevel::ERROR, "Failed to initialize Gmail Manager: ", e.what());
        std::cerr << "Critical Error: Failed to initialize Gmail Manager. Check logs." << std::endl;
        return 1;
    }
    
    logger.log(LogLevel::INFO, "Gmail connection successful.");

    // --- Load Tools --- 
    nlohmann::json tool_schema;
    std::ifstream file(TOOLS_SCHEMA_PATH);
    if (!file.is_open()) {
        logger.log(LogLevel::ERROR, "Unable to open tool schema file: ", TOOLS_SCHEMA_PATH);
        std::cerr << "Critical Error: Cannot load tool schema. Check logs." << std::endl;
        return 1; 
    }
    try {
        file >> tool_schema;
        logger.log(LogLevel::DEBUG, "Tool schema loaded successfully from: ", TOOLS_SCHEMA_PATH);
    } catch (const nlohmann::json::parse_error& e) {
        logger.log(LogLevel::ERROR, "Error parsing tool schema file ", TOOLS_SCHEMA_PATH, ": ", e.what());
        std::cerr << "Critical Error: Cannot parse tool schema. Check logs." << std::endl;
        return 1;
    }

    // --- Initialize Tool Manager --- 
    ToolManager tool_manager(logger); // Pass logger reference
    tool_manager.register_gmail_tools(gmail_mgr->get_instance());
    logger.log(LogLevel::INFO, "ToolManager initialized and Gmail tools registered.");
 
    // --- Build System Prompt --- (Logging this might be too verbose?)
    // logger.log(LogLevel::DEBUG, "Building system prompt...");
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

    // --- Initialize Llama --- 
    std::unique_ptr<LlamaInference> llama;
    try {
        // Pass logger to LlamaInference constructor
        llama = std::make_unique<LlamaInference>(model_path, logger, ngl, n_ctx);

        // Set system prompt
        llama->setSystemPrompt(system_prompt);

        if (!llama->initialize()) {
            // This path might be unreachable if initialize() now throws on failure
            // But keep it for safety unless initialize() signature changes
            logger.log(LogLevel::ERROR, "Failed to initialize LlamaInference (post-constructor).");
            std::cerr << "Failed to initialize LlamaInference (post-constructor)." << std::endl;
            return 1;
        }
    } catch (const LlamaException& e) {
        logger.log(LogLevel::ERROR, "Error initializing Llama model: ", e.what());
        std::cerr << "Critical Error: Failed to initialize LLM. Check logs." << std::endl;
        return 1;
    } catch (const std::exception& e) {
        logger.log(LogLevel::ERROR, "An unexpected error occurred during Llama initialization: ", e.what());
        std::cerr << "Critical Error: Unexpected LLM initialization failure. Check logs." << std::endl;
        return 1;
    }
    logger.log(LogLevel::INFO, "LlamaInference initialized successfully.");

    // --- UI Setup --- 
    logger.log(LogLevel::INFO, "Setting up UI...");
    auto screen = ScreenInteractive::Fullscreen();

    std::string user_prompt;
    Component user_prompt_box = Input(&user_prompt, "Type prompt here") | border |
        CatchEvent([&](Event event) {
            if (event == Event::Return && !user_prompt.empty() && !is_streaming) {
                logger.log(LogLevel::DEBUG, "User input received: ", user_prompt);
                // Prepare initial prompt for the *first* turn (might just be the user message)
                // The system prompt is handled internally by LlamaInference now.
                std::string first_turn_prompt = user_prompt; 
                {
                    std::lock_guard<std::mutex> lock(response_mutex);
                    response = "> " + user_prompt + "\n"; // Display user input immediately
                }
                user_prompt.clear();

                pybind11::gil_scoped_release gil;
                // Capture logger by reference for the background thread
                std::thread([&llama, &tool_manager, &logger, first_turn_prompt, &screen]() {
                    pybind11::gil_scoped_acquire gil;
                    try {
                        logger.log(LogLevel::INFO, "Starting background chat/tool execution thread.");
                        // Pass the redraw lambda correctly
                        StreamChat(*llama, tool_manager, first_turn_prompt, [&] { screen.PostEvent(Event::Custom); });
                        logger.log(LogLevel::INFO, "Background chat/tool execution thread finished.");
                    } catch (const LlamaException& e) {
                        // Handle Llama errors during streaming/chat
                        logger.log(LogLevel::ERROR, "Llama runtime error in background thread: ", e.what());
                        {
                            std::lock_guard<std::mutex> lock(response_mutex);
                            response = "Error during processing: " + std::string(e.what());
                        }
                        is_streaming = false;
                        screen.PostEvent(Event::Custom); // Redraw UI with error
                    } catch (const std::exception& e) {
                        // Handle other potential errors
                        logger.log(LogLevel::ERROR, "Exception in background thread: ", e.what());
                        {
                            std::lock_guard<std::mutex> lock(response_mutex);
                            response = "An unexpected error occurred: " + std::string(e.what());
                        }
                        is_streaming = false;
                        screen.PostEvent(Event::Custom); // Redraw UI with error
                    }
                }).detach();
                return true;
            }
            return false;
        });

    auto container = Container::Vertical({
        user_prompt_box
    });

    auto renderer = Renderer(container, [&] {
      std::lock_guard<std::mutex> lock(response_mutex);
      return vbox({
          text("MaiMail " + APP_VERSION) | center,
          separator(),
          paragraphAlignLeft(response) | yflex | border,
          separator(),
          user_prompt_box->Render()
      });
    });

    logger.log(LogLevel::INFO, "Starting UI event loop.");
    screen.Loop(renderer);

    logger.log(LogLevel::INFO, "--- Application Exiting --- ");
    return 0;
}
