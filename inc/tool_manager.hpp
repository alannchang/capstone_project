#ifndef TOOL_MANAGER_HPP
#define TOOL_MANAGER_HPP

#include <pybind11/embed.h>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <unordered_map>
#include <functional>

namespace py = pybind11;

using ToolHandler = std::function<py::object(const nlohmann::json&)>;

class ToolManager {

public:

    void register_tool(const std::string& name, ToolHandler handler);

    std::optional<std::string> handle_tool_call(const std::string& model_output);   
    
    py::object call_tool(const std::string& name, const nlohmann::json& args) const;

    bool has_tool(const std::string& name) const;

    void register_gmail_tools(py::object gmail_manager);

private:

    std::unordered_map<std::string, ToolHandler> tools_;

};

#endif // TOOL_MANAGER_HPP

