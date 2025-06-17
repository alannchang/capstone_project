#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <fstream>

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
    std::vector<float> embedding;
};

struct BehaviorPattern {
    std::string action_type;
    std::string action_value;
    std::string context_type;
    std::string context_value;
    int frequency;
    std::string last_occurrence;
    nlohmann::json metadata;
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
    bool storeEmbedding(int vector_id, const std::string& summary, const std::vector<float>& embedding);
    std::vector<EmbeddingRecord> getAllEmbeddings();
    
    // Behavior logging
    void logBehavior(const std::string& action_type,
                    const std::string& action_value,
                    const std::string& context_type,
                    const std::string& context_value,
                    const std::string& message_id,
                    const nlohmann::json& metadata);
    
    // Generic pattern recognition methods
    std::vector<BehaviorPattern> getBehaviorPatterns(
        const std::string& action_type = "",
        const std::string& context_type = "",
        int min_frequency = 2);
        
    std::vector<BehaviorPattern> getFrequentBehaviors(
        const std::string& action_type,
        const std::string& context_type,
        int min_frequency = 3);
        
    int getBehaviorFrequency(
        const std::string& action_type,
        const std::string& action_value,
        const std::string& context_type,
        const std::string& context_value);
    
    // Utility functions
    bool isInitialized() const { return db_ != nullptr; }
    std::string getLastError() const { return last_error_; }
    
private:
    sqlite3* db_;
    std::string db_path_;
    std::string last_error_;
    std::ofstream debug_log_file_;
    
    // Helper functions
    bool createTables();
    bool executeSQL(const std::string& sql);
    std::string getCurrentTimestamp();
    void setError(const std::string& error);
};

#endif // DATABASE_MANAGER_H 