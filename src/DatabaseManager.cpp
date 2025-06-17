#include "DatabaseManager.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

DatabaseManager::DatabaseManager(const std::string& db_path) 
    : db_(nullptr), db_path_(db_path) {
    debug_log_file_.open("database_debug.log", std::ios::app);
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "\n--- DatabaseManager Initialized ---" << std::endl << std::flush;
    }
}

DatabaseManager::~DatabaseManager() {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "--- DatabaseManager Cleanup ---" << std::endl << std::flush;
        debug_log_file_.close();
    }
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
    
    // Create email_actions table for pattern recognition
    std::string create_email_actions = R"(
        CREATE TABLE IF NOT EXISTS email_actions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL DEFAULT (datetime('now')),
            action_type TEXT NOT NULL,      -- e.g., 'delete', 'label', 'forward', 'reply'
            action_value TEXT NOT NULL,     -- e.g., 'label:Important', 'forward:team@company.com'
            context_type TEXT NOT NULL,     -- e.g., 'sender', 'subject', 'time', 'category'
            context_value TEXT NOT NULL,    -- e.g., 'sam@gmail.com', 'Weekly Report', 'morning'
            message_id TEXT,
            metadata TEXT,                  -- JSON field for additional context
            UNIQUE(action_type, action_value, message_id)
        )
    )";
    
    return executeSQL(create_tool_calls) && executeSQL(create_embeddings) && executeSQL(create_email_actions);
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

// Pattern Recognition Methods

void DatabaseManager::logBehavior(const std::string& action_type,
                                const std::string& action_value,
                                const std::string& context_type,
                                const std::string& context_value,
                                const std::string& message_id,
                                const nlohmann::json& metadata) {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG DatabaseManager::logBehavior: Attempting to log behavior:" << std::endl;
        debug_log_file_ << "  action_type: " << action_type << std::endl;
        debug_log_file_ << "  action_value: " << action_value << std::endl;
        debug_log_file_ << "  context_type: " << context_type << std::endl;
        debug_log_file_ << "  context_value: " << context_value << std::endl;
        debug_log_file_ << "  message_id: " << message_id << std::endl;
        debug_log_file_ << "  metadata: " << metadata.dump() << std::endl;
    }

    try {
        if (!db_) {
            setError("Database not initialized");
            return;
        }
        
        std::string sql = R"(
            INSERT OR IGNORE INTO email_actions 
            (action_type, action_value, context_type, context_value, message_id, metadata)
            VALUES (?, ?, ?, ?, ?, ?)
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        
        if (rc != SQLITE_OK) {
            setError("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
            return;
        }
        
        sqlite3_bind_text(stmt, 1, action_type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, action_value.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, context_type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, context_value.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, message_id.c_str(), -1, SQLITE_STATIC);
        
        std::string metadata_str = metadata ? metadata.dump() : "{}";
        sqlite3_bind_text(stmt, 6, metadata_str.c_str(), -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            setError("Failed to log behavior: " + std::string(sqlite3_errmsg(db_)));
            return;
        }
        
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG DatabaseManager::logBehavior: Successfully logged behavior" << std::endl;
        }
    } catch (const std::exception& e) {
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "ERROR DatabaseManager::logBehavior: Failed to log behavior: " << e.what() << std::endl;
        }
        throw;
    }
}

std::vector<BehaviorPattern> DatabaseManager::getBehaviorPatterns(
    const std::string& action_type,
    const std::string& context_type,
    int min_frequency) {
    
    std::vector<BehaviorPattern> patterns;
    if (!db_) {
        setError("Database not initialized");
        return patterns;
    }
    
    std::string sql = R"(
        SELECT 
            action_type,
            action_value,
            context_type,
            context_value,
            COUNT(*) as frequency,
            datetime(MAX(timestamp)) as last_occurrence,
            metadata
        FROM email_actions 
        WHERE 1=1
    )";
    
    if (!action_type.empty()) {
        sql += " AND action_type = ?";
    }
    if (!context_type.empty()) {
        sql += " AND context_type = ?";
    }
    
    sql += R"(
        GROUP BY action_type, action_value, context_type, context_value
        HAVING COUNT(*) >= ?
        ORDER BY frequency DESC, last_occurrence DESC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
        return patterns;
    }
    
    int param_idx = 1;
    if (!action_type.empty()) {
        sqlite3_bind_text(stmt, param_idx++, action_type.c_str(), -1, SQLITE_STATIC);
    }
    if (!context_type.empty()) {
        sqlite3_bind_text(stmt, param_idx++, context_type.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, param_idx, min_frequency);
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BehaviorPattern pattern;
        pattern.action_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        pattern.action_value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        pattern.context_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        pattern.context_value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        pattern.frequency = sqlite3_column_int(stmt, 4);
        pattern.last_occurrence = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        
        try {
            std::string metadata_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            pattern.metadata = nlohmann::json::parse(metadata_str);
        } catch (const nlohmann::json::parse_error&) {
            pattern.metadata = nlohmann::json::object();
        }
        
        patterns.push_back(pattern);
    }
    
    sqlite3_finalize(stmt);
    return patterns;
}

std::vector<BehaviorPattern> DatabaseManager::getFrequentBehaviors(
    const std::string& action_type,
    const std::string& context_type,
    int min_frequency) {
    
    return getBehaviorPatterns(action_type, context_type, min_frequency);
}

int DatabaseManager::getBehaviorFrequency(
    const std::string& action_type,
    const std::string& action_value,
    const std::string& context_type,
    const std::string& context_value) {
    
    if (!db_) {
        setError("Database not initialized");
        return 0;
    }
    
    std::string sql = R"(
        SELECT COUNT(*) 
        FROM email_actions 
        WHERE action_type = ? 
        AND action_value = ? 
        AND context_type = ? 
        AND context_value = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, action_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, action_value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, context_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, context_value.c_str(), -1, SQLITE_STATIC);
    
    int frequency = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        frequency = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return frequency;
}

std::vector<EmbeddingRecord> DatabaseManager::getAllEmbeddings() {
    std::vector<EmbeddingRecord> records;
    
    if (!db_) {
        setError("Database not initialized");
        return records;
    }
    
    std::string sql = "SELECT id, vector_id, summary FROM embeddings ORDER BY id DESC";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare embeddings query: " + std::string(sqlite3_errmsg(db_)));
        return records;
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

bool DatabaseManager::storeEmbedding(int vector_id, const std::string& summary, const std::vector<float>& embedding) {
    if (!db_) {
        setError("Database not initialized");
        return false;
    }
    
    std::string sql = R"(
        INSERT INTO embeddings (vector_id, summary, embedding)
        VALUES (?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        setError("Failed to prepare embedding storage: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, vector_id);
    sqlite3_bind_text(stmt, 2, summary.c_str(), -1, SQLITE_STATIC);
    
    // Store embedding as BLOB
    sqlite3_bind_blob(stmt, 3, embedding.data(), 
                     static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Failed to store embedding: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    return true;
} 