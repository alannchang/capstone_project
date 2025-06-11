#include "DatabaseManager.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

DatabaseManager::DatabaseManager(const std::string& db_path) 
    : db_(nullptr), db_path_(db_path) {
}

DatabaseManager::~DatabaseManager() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DatabaseManager::initialize() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        setError("Cannot open database: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    if (!createTables()) {
        return false;
    }
    
    std::cout << "DatabaseManager: Successfully initialized database at " << db_path_ << std::endl;
    return true;
}

bool DatabaseManager::createTables() {
    // Create tool_calls table based on feature_plan.md schema
    std::string create_tool_calls = R"(
        CREATE TABLE IF NOT EXISTS tool_calls (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            prompt TEXT NOT NULL,
            tool_name TEXT NOT NULL,
            tool_params TEXT NOT NULL,
            response TEXT NOT NULL
        )
    )";
    
    // Create embeddings table
    std::string create_embeddings = R"(
        CREATE TABLE IF NOT EXISTS embeddings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vector_id INTEGER NOT NULL,
            summary TEXT NOT NULL,
            embedding BLOB
        )
    )";
    
    return executeSQL(create_tool_calls) && executeSQL(create_embeddings);
}

bool DatabaseManager::executeSQL(const std::string& sql) {
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);
    
    if (rc != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(error_msg);
        sqlite3_free(error_msg);
        setError(error);
        return false;
    }
    
    return true;
}

std::string DatabaseManager::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

void DatabaseManager::setError(const std::string& error) {
    last_error_ = error;
    std::cerr << "DatabaseManager Error: " << error << std::endl;
}

int DatabaseManager::logToolCall(const std::string& prompt, 
                                const std::string& tool_name,
                                const nlohmann::json& tool_params,
                                const nlohmann::json& response) {
    if (!db_) {
        setError("Database not initialized");
        return -1;
    }
    
    std::string sql = R"(
        INSERT INTO tool_calls (timestamp, prompt, tool_name, tool_params, response)
        VALUES (?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, getCurrentTimestamp().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, prompt.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, tool_name.c_str(), -1, SQLITE_STATIC);
    
    std::string params_str = tool_params.dump();
    std::string response_str = response.dump();
    
    sqlite3_bind_text(stmt, 4, params_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, response_str.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Failed to execute insert: " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }
    
    int row_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    std::cout << "DatabaseManager: Logged tool call with ID " << row_id << std::endl;
    return row_id;
}

std::vector<ToolCallRecord> DatabaseManager::getRecentToolCalls(int limit) {
    std::vector<ToolCallRecord> records;
    
    if (!db_) {
        setError("Database not initialized");
        return records;
    }
    
    std::string sql = R"(
        SELECT id, timestamp, prompt, tool_name, tool_params, response
        FROM tool_calls
        ORDER BY timestamp DESC
        LIMIT ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db_)));
        return records;
    }
    
    sqlite3_bind_int(stmt, 1, limit);
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ToolCallRecord record;
        record.id = sqlite3_column_int(stmt, 0);
        record.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        record.prompt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        record.tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        try {
            std::string params_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            std::string response_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            
            record.tool_params = nlohmann::json::parse(params_str);
            record.response = nlohmann::json::parse(response_str);
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "DatabaseManager: JSON parse error for record " << record.id << ": " << e.what() << std::endl;
            continue;
        }
        
        records.push_back(record);
    }
    
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Error while retrieving records: " + std::string(sqlite3_errmsg(db_)));
    }
    
    return records;
}

std::vector<ToolCallRecord> DatabaseManager::getToolCallsByTimeRange(const std::string& start_time, 
                                                                     const std::string& end_time) {
    std::vector<ToolCallRecord> records;
    
    if (!db_) {
        setError("Database not initialized");
        return records;
    }
    
    std::string sql = R"(
        SELECT id, timestamp, prompt, tool_name, tool_params, response
        FROM tool_calls
        WHERE timestamp BETWEEN ? AND ?
        ORDER BY timestamp DESC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare time range statement: " + std::string(sqlite3_errmsg(db_)));
        return records;
    }
    
    sqlite3_bind_text(stmt, 1, start_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, end_time.c_str(), -1, SQLITE_STATIC);
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ToolCallRecord record;
        record.id = sqlite3_column_int(stmt, 0);
        record.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        record.prompt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        record.tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        try {
            std::string params_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            std::string response_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            
            record.tool_params = nlohmann::json::parse(params_str);
            record.response = nlohmann::json::parse(response_str);
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "DatabaseManager: JSON parse error for record " << record.id << ": " << e.what() << std::endl;
            continue;
        }
        
        records.push_back(record);
    }
    
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Error while retrieving time range records: " + std::string(sqlite3_errmsg(db_)));
    }
    
    return records;
}

bool DatabaseManager::logEmbedding(int tool_call_id, int vector_id, const std::string& summary) {
    if (!db_) {
        setError("Database not initialized");
        return false;
    }
    
    std::string sql = R"(
        INSERT INTO embeddings (id, vector_id, summary)
        VALUES (?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare embedding statement: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, tool_call_id);
    sqlite3_bind_int(stmt, 2, vector_id);
    sqlite3_bind_text(stmt, 3, summary.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Failed to insert embedding: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    return true;
}

std::vector<EmbeddingRecord> DatabaseManager::getEmbeddingsByVectorIds(const std::vector<int>& vector_ids) {
    std::vector<EmbeddingRecord> records;
    
    if (!db_ || vector_ids.empty()) {
        return records;
    }
    
    // Build IN clause
    std::string placeholders = std::string(vector_ids.size() * 2 - 1, '?');
    for (size_t i = 1; i < vector_ids.size(); ++i) {
        placeholders[i * 2 - 1] = ',';
    }
    
    std::string sql = "SELECT id, vector_id, summary FROM embeddings WHERE vector_id IN (" + placeholders + ")";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare embedding select: " + std::string(sqlite3_errmsg(db_)));
        return records;
    }
    
    // Bind vector IDs
    for (size_t i = 0; i < vector_ids.size(); ++i) {
        sqlite3_bind_int(stmt, static_cast<int>(i + 1), vector_ids[i]);
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        EmbeddingRecord record;
        record.id = sqlite3_column_int(stmt, 0);
        record.vector_id = sqlite3_column_int(stmt, 1);
        record.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        records.push_back(record);
    }
    
    sqlite3_finalize(stmt);
    return records;
} 