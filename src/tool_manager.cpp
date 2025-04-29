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

