#include "tool_manager.hpp"
#include <stdexcept>

void ToolManager::register_tool(const std::string& name, ToolHandler handler) {
    tools_[name] = std::move(handler);
}

py::object ToolManager::call_tool(const std::string& name, const nlohmann::json& args) const {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second(args);
    } else {
        throw std::runtime_error("Tool not found: " + name);
    }
}

bool ToolManager::has_tool(const std::string& name) const {
    return tools_.count(name) > 0;
}

std::optional<std::string> ToolManager::handle_tool_call(const std::string& model_output) {
    log_debug("Raw model output: " + model_output);

    nlohmann::json output_json = nlohmann::json::parse(model_output, nullptr, false);
    if (output_json.is_discarded() || !output_json.is_array()) {
        log_debug("Invalid tool call format.");
        std::cerr << "Invalid tool call format." << std::endl;
        return std::nullopt;
    }

    std::ostringstream aggregated_result;

    for (const auto& tool_call : output_json) {
        std::string name = tool_call["name"];
        nlohmann::json args = tool_call["arguments"];
        log_debug("Handling tool: " + name + " with args: " + args.dump());

        if (tools_.count(name)) {
            try {
                py::object tool_result = tools_[name](args);
                py::str result_str = py::str(tool_result);
                aggregated_result << "Result from " << name << ": " << result_str.cast<std::string>() << "\n";
                log_debug("Success: Tool '" + name + "' returned: " + result_str.cast<std::string>());
            } catch (const py::error_already_set& e) {
                aggregated_result << "Python error from " << name << ": " << e.what() << "\n";
                log_debug("Python error in '" + name + "': " + std::string(e.what()));
            } catch (const std::exception& e) {
                std::cout << "\nError from " << name << ": " << e.what() << "\n";
                log_debug("C++ error in '" + name + "': " + std::string(e.what()));
            }
        } else {
            aggregated_result << "Unknown tool: " << name << "\n";
            log_debug("Unknown tool: " + name);
        }
    }
    return aggregated_result.str();
}

void ToolManager::register_gmail_tools(py::object gmail_manager) {
    register_tool("get_profile", [=](const nlohmann::json&) {
        return gmail_manager.attr("get_profile")();
    });

    register_tool("send_message", [=](const nlohmann::json& args) {
        return gmail_manager.attr("send_message")(
            args.at("to"), args.at("subject"), args.at("body")
        );
    });
    
    register_tool("list_messages", [=](const nlohmann::json& args) {
        return gmail_manager.attr("list_messages")(
            args.at("query"), args.at("max_results")
        );
    });

}

void ToolManager::log_debug(const std::string& message) const {
    std::ofstream log_file("tool_debug.log", std::ios::app);
    log_file << message << std::endl;
}

