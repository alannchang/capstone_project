#include "DatabaseManager.h"
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
    std::cout << "Testing DatabaseManager..." << std::endl;
    
    // Create database manager
    DatabaseManager db("test_gmail_assistant.db");
    
    // Initialize database
    if (!db.initialize()) {
        std::cerr << "Failed to initialize database: " << db.getLastError() << std::endl;
        return 1;
    }
    
    // Test logging a tool call
    nlohmann::json params = {
        {"query", "is:unread"},
        {"max_results", 5}
    };
    
    nlohmann::json response = {
        {"messages", nlohmann::json::array({
            {{"from", "test@example.com"}, {"subject", "Test Email"}, {"snippet", "This is a test email snippet"}}
        })}
    };
    
    int call_id = db.logToolCall("Show me my unread emails", "list_messages", params, response);
    if (call_id > 0) {
        std::cout << "Successfully logged tool call with ID: " << call_id << std::endl;
    } else {
        std::cerr << "Failed to log tool call: " << db.getLastError() << std::endl;
        return 1;
    }
    
    // Test retrieving recent tool calls
    auto recent_calls = db.getRecentToolCalls(5);
    std::cout << "Retrieved " << recent_calls.size() << " recent tool calls:" << std::endl;
    
    for (const auto& call : recent_calls) {
        std::cout << "  ID: " << call.id << ", Tool: " << call.tool_name 
                  << ", Prompt: " << call.prompt.substr(0, 50) << "..." << std::endl;
    }
    
    // Test embedding logging
    if (db.logEmbedding(call_id, 1001, "User asked for unread emails")) {
        std::cout << "Successfully logged embedding for tool call " << call_id << std::endl;
    } else {
        std::cerr << "Failed to log embedding: " << db.getLastError() << std::endl;
    }
    
    std::cout << "DatabaseManager test completed successfully!" << std::endl;
    return 0;
} 