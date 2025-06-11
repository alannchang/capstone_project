#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <nlohmann/json.hpp>

struct ToolCallRecord {
    int id;
    std::string timestamp;
    std::string prompt;
    std::string tool_name;
    nlohmann::json tool_params;
    nlohmann::json response;
};

struct EmbeddingRecord {
    int id;
    int vector_id;
    std::string summary;
    std::vector<float> embedding; // Optional, for debugging
};

class DatabaseManager {
public:
    DatabaseManager(const std::string& db_path = "gmail_assistant.db");
    ~DatabaseManager();
    
    // Initialize database and create tables
    bool initialize();
    
    // Tool call logging
    int logToolCall(const std::string& prompt, 
                   const std::string& tool_name,
                   const nlohmann::json& tool_params,
                   const nlohmann::json& response);
    
    // Retrieve tool calls
    std::vector<ToolCallRecord> getRecentToolCalls(int limit = 10);
    std::vector<ToolCallRecord> getToolCallsByTimeRange(const std::string& start_time, 
                                                        const std::string& end_time);
    
    // Embedding management
    bool logEmbedding(int tool_call_id, int vector_id, const std::string& summary);
    std::vector<EmbeddingRecord> getEmbeddingsByVectorIds(const std::vector<int>& vector_ids);
    
    // Utility functions
    bool isInitialized() const { return db_ != nullptr; }
    std::string getLastError() const { return last_error_; }
    
private:
    sqlite3* db_;
    std::string db_path_;
    std::string last_error_;
    
    // Helper functions
    bool createTables();
    bool executeSQL(const std::string& sql);
    std::string getCurrentTimestamp();
    void setError(const std::string& error);
};

#endif // DATABASE_MANAGER_H 