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
// json
#include <nlohmann/json.hpp>
#include <fstream>

using namespace ftxui;

// Open a global log file stream for main.cpp diagnostics
std::ofstream main_debug_log;

const std::string APP_VERSION = "v0.0.1";

std::mutex response_mutex;
std::atomic<bool> is_streaming = false;
std::string response = "";
std::string current_streaming_text = ""; // Track the currently streaming text separately

// Improved function to wrap long lines to a specific width without breaking words
std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> result;
    std::istringstream iss(text);
    std::string line;
    
    while (std::getline(iss, line)) {
        std::string current_line;
        std::istringstream word_stream(line);
        std::string word;
        
        while (word_stream >> word) {
            // If this is the first word in the line or adding this word won't exceed width
            if (current_line.empty()) {
                current_line = word;
            } else if (current_line.length() + word.length() + 1 <= width) {
                current_line += " " + word;
            } else {
                // Line would be too long, push it and start a new one
                result.push_back(current_line);
                current_line = word;
            }
        }
        
        // Don't forget the last line
        if (!current_line.empty()) {
            result.push_back(current_line);
        }
        
        // If the original line was empty, preserve it
        if (line.empty()) {
            result.push_back("");
        }
    }
    
    return result;
}

// Function to track if we should auto-scroll
bool shouldAutoScroll(int scroll_offset, int max_scroll, bool was_at_bottom) {
    // Auto-scroll if we're streaming and either:
    // 1. We were already at the bottom before new content arrived
    // 2. We've never scrolled manually (scroll_offset == 0)
    return is_streaming && (was_at_bottom || scroll_offset == 0);
}

// Extract the last few tokens from a string
std::string getLastPartOfString(const std::string& text, int numChars) {
    if (text.length() <= numChars) {
        return text;
    }
    return text.substr(text.length() - numChars);
}

void StreamChat(LlamaInference& llama, bool user_scrolled, std::string prompt, std::function<void()> redraw) {
    if (main_debug_log.is_open()) main_debug_log << "DEBUG main: StreamChat entered with prompt: " << prompt.substr(0, 50) << "..." << std::endl;
    else std::cout << "DEBUG main: StreamChat entered with prompt: " << prompt.substr(0,50) << "..." << std::endl; // Fallback

    is_streaming = true;
    current_streaming_text = ""; // Reset streaming display
    
    // Use a callback that updates both the full response and the current streaming part
    llama.chat(prompt, true, response, [&redraw]() {
        // Update the current streaming text to show the most recent portion
        current_streaming_text = getLastPartOfString(response, 200);
        redraw();
    });
    
    is_streaming = false;
    user_scrolled = false; // Snap history to bottom on stream completion
    current_streaming_text = ""; // Clear streaming display when done
    redraw();

    if (main_debug_log.is_open()) main_debug_log << "DEBUG main: StreamChat finished for prompt: " << prompt.substr(0,50) << "..." << std::endl;
    else std::cout << "DEBUG main: StreamChat finished for prompt: " << prompt.substr(0,50) << "..." << std::endl; // Fallback
}

int main(int argc, char** argv) {
    // Open the log file at the beginning of main
    main_debug_log.open("llama_debug.log", std::ios::app); 
    if (main_debug_log.is_open()) main_debug_log << "\n--- Main Application Started ---" << std::endl;
    else std::cerr << "CRITICAL ERROR: Failed to open llama_debug.log in main!" << std::endl;

    if (argc < 2) {
        if (main_debug_log.is_open()) main_debug_log << "ERROR main: Not enough arguments." << std::endl;
        std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size (default 4096)] [-ngl n_gpu_layers]" << std::endl;
        return 1;
    }
    
    // parse command line arguments
    std::string model_path;
    int ngl = 99;
    int n_ctx = 4096; // Increased default context size
    
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
    /*
    nlohmann::json tool_schema;
    std::ifstream file("runtime-deps/tools.json");
    file >> tool_schema;
    */
/* 
    // build system prompt
    std::string task_instruction = R"(You are an assistant that manages a Gmail inbox.  You have access to a set of tools. When using tools, make calls in a single JSON array (DO NOT USE MARKDOWN): 

    [{\"name\": \"tool_call_name\", \"arguments\": {\"arg1\": \"value1\", \"arg2\": \"value2\"}}, ... (additional parallel tool calls as needed)]

    If no tool is suitable, state that explicitly. If the user\'s input lacks required parameters, ask for clarification. Do not interpret or respond until tool results are returned. Once they are available, process them or make additional calls if needed. For tasks that don\'t require tools, such as casual conversation or general advice, respond directly in plain text. The available tools are:)\"");

    std::string available_tools = tool_schema.dump(2);

    std::string system_prompt = 
        // \"<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\" 
        task_instruction 
        + \"\n\n\" 
        + available_tools; 
        // + \"\n<|eot_id|><|start_header_id|>user<|end_header_id|>\n\";
*/
    // std::string system_prompt = "You are a helpful assistant."; // Old simple prompt

    // New system prompt specifically for Qwen3 and tool calling, using a custom delimiter
    std::string system_prompt = R"EOF(You are a helpful AI assistant.
You have access to a variety of tools to interact with Gmail and other services.
When you decide to use a tool, you MUST respond ONLY with a single, valid JSON object. This JSON object should have two keys:
1. "tool_name": A string representing the exact name of the tool to be called.
2. "parameters": A JSON object containing the arguments for the tool. If a tool takes no arguments, provide an empty JSON object (e.g., {}).

Do NOT add any other text, explanations, or markdown formatting around the JSON object. Your entire response must be just the JSON object itself when calling a tool.

Available tools:
- {"tool_name": "send_email", "parameters": {"to": "string (email_address)", "subject": "string", "body": "string"}} - Sends an email. (Example: {"tool_name": "send_email", "parameters": {"to": "test@example.com", "subject": "Hello", "body": "Hi there!"}})
- {"tool_name": "list_labels", "parameters": {}} - Lists all Gmail labels. (Example: {"tool_name": "list_labels", "parameters": {}})
- {"tool_name": "get_profile", "parameters": {}} - Gets the user's Gmail profile. (Example: {"tool_name": "get_profile", "parameters": {}})
- {"tool_name": "trash_message", "parameters": {"message_id": "string"}} - Moves a specific message to trash. (Example: {"tool_name": "trash_message", "parameters": {"message_id": "123xyz"}})
// TODO: Add descriptions for other tools as they are implemented in LlamaInference.cpp.
// Ensure the "parameters" descriptions here clearly indicate the type and purpose of each argument.
// For example:
// - {"tool_name": "get_label", "parameters": {"label_id": "string (ID of the label)"}} - Gets details for a specific label.
// - {"tool_name": "create_label", "parameters": {"name": "string (desired_label_name)", "label_list_visibility": "string (e.g., 'labelShow')", "message_list_visibility": "string (e.g., 'show')"}} - Creates a new label.
// - {"tool_name": "list_messages", "parameters": {"query": "string (Gmail search query)", "max_results": "integer (optional, default 10)"}} - Lists messages.

After you request a tool call, I will execute it and provide you with the result in a message with role "tool".
Based on the tool's result, you can then:
- Formulate a final textual response to the user.
- Decide to call another tool (again, by responding ONLY with the JSON for that tool call).
- Ask the user for clarification if needed.

If you can answer the user's request directly without needing any tools, please provide a direct textual response without any JSON.
If a tool call results in an error, I will inform you of the error. You should then try to inform the user or try a different approach if appropriate.
)EOF";

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

    // Scrolling state
    int scroll_offset = 0;
    const int page_size = 10; // How many lines to scroll on page up/down
    bool user_scrolled = false; // Track if user has manually scrolled

    std::string prompt;
    
    // Text input component for user input
    Component user_prompt_box = Input(&prompt, "Type prompt here") | border;
    
    // Container for both components
    auto container = Container::Vertical({
        Renderer([&]{
            // Placeholder for the response area
            return text("Loading...");
        }),
        user_prompt_box
    });

    // Event handling
    container = container | CatchEvent([&](Event event) {
        // Only handle scrolling events if not streaming
        if (!is_streaming) {
            if (event == Event::ArrowUp) {
                if (scroll_offset > 0) {
                    scroll_offset--;
                    user_scrolled = true;
                    screen.PostEvent(Event::Custom);
                    return true;
                }
            } else if (event == Event::ArrowDown) {
                // Will check bounds in the renderer
                scroll_offset++;
                user_scrolled = true;
                screen.PostEvent(Event::Custom);
                return true;
            } else if (event == Event::PageUp) {
                scroll_offset = std::max(0, scroll_offset - page_size);
                user_scrolled = true;
                screen.PostEvent(Event::Custom);
                return true;
            } else if (event == Event::PageDown) {
                scroll_offset += page_size;
                user_scrolled = true;
                screen.PostEvent(Event::Custom);
                return true;
            }
        }
        
        // Handle input submission
        if (event == Event::Return && !prompt.empty() && !is_streaming) {
            if (main_debug_log.is_open()) {
                main_debug_log << "DEBUG main: Event::Return triggered." << std::endl;
                main_debug_log << "DEBUG main: Prompt from UI: '" << prompt << "'" << std::endl;
                main_debug_log << "DEBUG main: is_streaming is false." << std::endl;
            } else { // Fallback if log isn't open
                std::cout << "DEBUG main: Event::Return triggered with prompt: '" << prompt << "'" << std::endl;
            }

            response = "";
            scroll_offset = 0; // Reset scroll position
            user_scrolled = false; // Reset user scroll state
            std::string _prompt = prompt;
            prompt.clear();
            
            std::thread([&llama, user_scrolled, _prompt, &screen]() {
                StreamChat(llama, user_scrolled, _prompt, [&] { 
                    screen.PostEvent(Event::Custom);
                });
            }).detach();
            
            return true;
        }
        
        return false;
    });

    auto renderer = Renderer(container, [&] {
        // Get terminal size
        auto size = Terminal::Size();
        int width = size.dimx - 6; // Account for borders and some padding
        
        // For history area, use all but 5 lines (2 for streaming, 1 for separator, 2 for padding)
        int history_height = size.dimy - 13; // Adjusted to make room for streaming area
        
        // Create wrapped lines for scrolling from the full response
        std::vector<std::string> history_lines = wrapText(response, width);
        
        // Adjust scroll bounds for history area
        int max_scroll = std::max(0, static_cast<int>(history_lines.size()) - history_height);
        
        // Auto-scroll to bottom of history if user hasn't manually scrolled
        if (!user_scrolled) {
            scroll_offset = max_scroll;
        }
        
        // Ensure scroll_offset is within bounds
        scroll_offset = std::min(scroll_offset, max_scroll);
        scroll_offset = std::max(0, scroll_offset);
        
        // Select visible portion of history lines
        Element history_display;
        if (history_lines.empty()) {
            history_display = text(" ");
        } else {
            std::vector<Element> visible_elements;
            for (int i = scroll_offset; i < scroll_offset + history_height && i < static_cast<int>(history_lines.size()); i++) {
                visible_elements.push_back(text(history_lines[i]));
            }
            history_display = vbox(visible_elements);
        }
        
        // Create scroll info
        std::string scroll_info = "";
        if (history_lines.size() > history_height) {
            std::stringstream ss;
            ss << "[" << (scroll_offset + 1) << "-" 
               << std::min(scroll_offset + history_height, static_cast<int>(history_lines.size())) 
               << "/" << history_lines.size() << "]";
            scroll_info = ss.str();
        }
        
        // Prepare the streaming area display
        Element streaming_area;
        if (is_streaming) {
            // Wrap the current streaming text
            std::vector<std::string> streaming_lines = wrapText(current_streaming_text, width);
            
            // If we have more than 2 lines, take just the last 2
            if (streaming_lines.size() > 2) {
                streaming_lines = {
                    streaming_lines[streaming_lines.size() - 2],
                    streaming_lines[streaming_lines.size() - 1]
                };
            }
            
            // Create the streaming display area
            std::vector<Element> streaming_elements;
            for (const auto& line : streaming_lines) {
                streaming_elements.push_back(text(line));
            }
            
            // Ensure we have exactly 2 lines (pad with empty if needed)
            while (streaming_elements.size() < 2) {
                streaming_elements.push_back(text(""));
            }
            
            streaming_area = vbox({
                separator(),
                text("  🔴 Streaming...") | color(Color::Red),
                vbox(streaming_elements) | border
            });
        } else {
            // No streaming area when not streaming
            streaming_area = filler();
        }
        
        // Basic layout with history, streaming indicator, and input
        return vbox({
            text("MaiMail " + APP_VERSION) | center,
            separator(),
            history_display | border | flex,
            scroll_info.empty() ? filler() : text(scroll_info) | center,
            streaming_area,
            separator(),
            user_prompt_box->Render()
        });
    });

    screen.Loop(renderer);
    if (main_debug_log.is_open()) {
        main_debug_log << "--- Main Application Exiting ---" << std::endl;
        main_debug_log.close();
    }
    return 0;
}
