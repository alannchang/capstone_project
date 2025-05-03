#include "tool_manager.hpp"
#include <stdexcept>

// Constructor initializes the logger reference
ToolManager::ToolManager(Logger& logger)
    : logger_(logger) {}

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

// Helper function to execute a single tool call and handle exceptions/logging
std::string ToolManager::execute_single_tool(const nlohmann::json& tool_call) {
    std::ostringstream result_stream;
    std::string name = tool_call.value("name", ""); // Use .value for safer access
    nlohmann::json args = tool_call.value("arguments", nlohmann::json::object()); // Default to empty object

    if (name.empty()) {
        // log_debug("Tool call missing 'name' field.");
        logger_.log(LogLevel::WARNING, "Tool call missing 'name' field.");
        return "Error: Tool call missing 'name' field.\n";
    }

    // log_debug("Executing tool: " + name + " with args: " + args.dump());
    logger_.log(LogLevel::DEBUG, "Executing tool: ", name, " with args: ", args.dump());

    auto it = tools_.find(name);
    if (it != tools_.end()) {
        try {
            py::object tool_result = it->second(args); // Call the registered handler
            py::str result_str = py::str(tool_result);
            result_stream << "Result from " << name << ": " << result_str.cast<std::string>();
            // log_debug("Success: Tool '" + name + "' returned: " + result_str.cast<std::string>());
            logger_.log(LogLevel::DEBUG, "Success: Tool ", name, " returned: ", result_str.cast<std::string>());
        } catch (const py::error_already_set& e) {
            result_stream << "Python error from " << name << ": " << e.what();
            // log_debug("Python error in '" + name + "': " + std::string(e.what()));
            logger_.log(LogLevel::ERROR, "Python error executing tool ", name, ": ", e.what());
            // std::cerr << "Python Error (Tool: " << name << "): " << e.what() << std::endl; // Keep cerr for now?
        } catch (const std::exception& e) {
            result_stream << "C++ error from " << name << ": " << e.what();
            // log_debug("C++ error in '" + name + "': " + std::string(e.what()));
            logger_.log(LogLevel::ERROR, "C++ error executing tool ", name, ": ", e.what());
            // std::cerr << "C++ Error (Tool: " << name << "): " << e.what() << std::endl; // Keep cerr for now?
        }
    } else {
        result_stream << "Unknown tool: " << name;
        // log_debug("Unknown tool requested: " + name);
        logger_.log(LogLevel::WARNING, "Unknown tool requested: ", name);
    }
    return result_stream.str();
}

// Renamed from handle_tool_call
std::optional<std::string> ToolManager::handle_tool_call_string(const std::string& model_output_json_string) {
    // log_debug("Raw model output for tool parsing: " + model_output_json_string);
    logger_.log(LogLevel::DEBUG, "Raw model output for tool parsing: ", model_output_json_string);

    // Attempt to parse the JSON string
    nlohmann::json output_json = nlohmann::json::parse(model_output_json_string, nullptr, false); // Non-throwing parse
    
    // Check if parsing failed or the result is not a JSON array
    if (output_json.is_discarded() || !output_json.is_array()) {
        // log_debug("Model output is not a valid JSON array for tool calls.");
        logger_.log(LogLevel::WARNING, "Model output is not a valid JSON array for tool calls.");
        // Returning nullopt indicates no valid tool calls were found / parsed
        return std::nullopt;
    }

    // Check if the array is empty
    if (output_json.empty()) {
        // log_debug("Model output contained an empty tool call array.");
        logger_.log(LogLevel::DEBUG, "Model output contained an empty tool call array.");
        return std::nullopt;
    }

    std::ostringstream aggregated_results;
    bool first_result = true;

    // Iterate through each tool call object in the array
    for (const auto& tool_call_json : output_json) {
        if (!tool_call_json.is_object()) {
            // log_debug("Skipping invalid item in tool call array (not an object).");
            logger_.log(LogLevel::DEBUG, "Skipping invalid item in tool call array (not an object).");
            continue; // Skip non-object elements
        }

        if (!first_result) {
            aggregated_results << "\n"; // Add newline between results
        }
        // Execute the single tool call and append its result
        aggregated_results << execute_single_tool(tool_call_json);
        first_result = false;
    }

    std::string final_result = aggregated_results.str();
    if (final_result.empty()) {
        // This might happen if the array only contained invalid items
        // log_debug("No valid tool calls executed from the provided JSON array.");
        logger_.log(LogLevel::DEBUG, "No valid tool calls executed from the provided JSON array.");
        return std::nullopt;
    }
    
    // log_debug("Aggregated tool results: " + final_result);
    logger_.log(LogLevel::DEBUG, "Aggregated tool results: ", final_result);
    // Return the aggregated results string
    return final_result;
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

