#include <gtest/gtest.h>
#include <pybind11/embed.h>  // For scoped_interpreter, py::object, etc.
#include <nlohmann/json.hpp> // For JSON manipulation
#include <optional>
#include <fstream>
#include <cstdio>

#include "tool_manager.hpp"

namespace py = pybind11;
using json = nlohmann::json;

class ToolManagerTest : public ::testing::Test {
protected:
    // Keep interpreter alive for all tests in the fixture
    static std::unique_ptr<pybind11::scoped_interpreter> guard;
    py::object mock_gmail_instance;
    std::unique_ptr<ToolManager> tm;

    static void SetUpTestSuite() {
        guard = std::make_unique<pybind11::scoped_interpreter>();
    }

    static void TearDownTestSuite() {
        guard.reset();
    }

    void SetUp() override {
        // Create ToolManager instance
        tm = std::make_unique<ToolManager>();

        // Create mock Gmail instance
        py::gil_scoped_acquire gil;
        mock_gmail_instance = py::eval("type('MockGmail', (object,), {})()");

        // Clear any existing log file
        std::ofstream ofs("tool_debug.log", std::ios::trunc);
    }

    void TearDown() override {
        tm.reset();
        std::remove("tool_debug.log");
    }

    // Mock Tool Handlers
    static py::object mock_python_success(const json& args) {
        py::gil_scoped_acquire gil;
        return py::str("Success: args=" + args.dump());
    }

    static py::object mock_python_error(const json& /*args*/) {
        py::gil_scoped_acquire gil;
        PyErr_SetString(PyExc_ValueError, "Mock Python Value Error");
        throw py::error_already_set();
    }

    static py::object mock_cpp_error(const json& /*args*/) {
        throw std::runtime_error("Mock C++ Runtime Error");
    }
};

// Define static member
std::unique_ptr<pybind11::scoped_interpreter> ToolManagerTest::guard;

// Test Cases

TEST_F(ToolManagerTest, RegisterAndHasTool) {
    EXPECT_FALSE(tm->has_tool("test_tool"));
    tm->register_tool("test_tool", mock_python_success);
    EXPECT_TRUE(tm->has_tool("test_tool"));
    EXPECT_FALSE(tm->has_tool("nonexistent_tool"));
}

TEST_F(ToolManagerTest, HandleToolCallStringSuccess) {
    tm->register_tool("my_tool", mock_python_success);
    
    json tool_call = {
        {"name", "my_tool"},
        {"arguments", {{"key", "value"}}}
    };
    std::string json_string = json::array({tool_call}).dump();
    
    auto result = tm->handle_tool_call(json_string);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("Result from my_tool: Success: args={\"key\":\"value\"}"),
              std::string::npos);
}

TEST_F(ToolManagerTest, HandleToolCallInvalidJson) {
    auto result = tm->handle_tool_call("not json");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ToolManagerTest, HandleToolCallNonArray) {
    auto result = tm->handle_tool_call(R"({"name": "test"})");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ToolManagerTest, HandleToolCallEmptyArray) {
    auto result = tm->handle_tool_call("[]");
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("No tool calls provided"), std::string::npos);
}

TEST_F(ToolManagerTest, ExecuteToolUnknown) {
    json tool_call = {
        {"name", "no_such_tool"},
        {"arguments", {}}
    };
    std::string json_string = json::array({tool_call}).dump();
    
    auto result = tm->handle_tool_call(json_string);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("Unknown tool: no_such_tool"),
              std::string::npos);
}

TEST_F(ToolManagerTest, ExecuteToolMissingName) {
    json tool_call = json::object();  // Create empty object instead of direct initialization
    tool_call["arguments"] = json::object();  // Add arguments as empty object
    std::string json_string = json::array({tool_call}).dump();
    
    auto result = tm->handle_tool_call(json_string);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("Invalid tool call format"),
              std::string::npos);
}

TEST_F(ToolManagerTest, ExecuteToolPythonError) {
    tm->register_tool("py_err", mock_python_error);
    
    json tool_call = {
        {"name", "py_err"},
        {"arguments", {}}
    };
    std::string json_string = json::array({tool_call}).dump();
    
    auto result = tm->handle_tool_call(json_string);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value().find("Python error from py_err: ValueError: Mock Python Value Error"),
              std::string::npos);
}

TEST_F(ToolManagerTest, ExecuteToolCppError) {
    tm->register_tool("cpp_err", mock_cpp_error);
    
    json tool_call = {
        {"name", "cpp_err"},
        {"arguments", {}}
    };
    std::string json_string = json::array({tool_call}).dump();
    
    auto result = tm->handle_tool_call(json_string);
    ASSERT_TRUE(result.has_value());
    std::string error_msg = "Error from cpp_err: Mock C++ Runtime Error";
    EXPECT_EQ(result.value(), error_msg);  // Use exact string comparison
}

TEST_F(ToolManagerTest, RegisterGmailTools) {
    // Verify tools don't exist initially
    EXPECT_FALSE(tm->has_tool("get_profile"));
    EXPECT_FALSE(tm->has_tool("send_message"));
    EXPECT_FALSE(tm->has_tool("list_messages"));

    // Register Gmail tools
    tm->register_gmail_tools(mock_gmail_instance);

    // Verify tools were registered
    EXPECT_TRUE(tm->has_tool("get_profile"));
    EXPECT_TRUE(tm->has_tool("send_message"));
    EXPECT_TRUE(tm->has_tool("list_messages"));
} 
