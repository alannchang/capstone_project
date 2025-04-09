#include "McpClient.hpp"
#include <chrono>
#include <random>
#include <sstream>
#include <iostream>
#include <cstring>

// Constructor
MCPClient::MCPClient() {
#ifdef _WIN32
    // Initialize Winsock for Windows
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
    }
#endif
}

// Destructor
MCPClient::~MCPClient() {
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

// Connect to MCP server
bool MCPClient::connect(const ConnectionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_) {
        disconnect();
    }
    
    config_ = config;
    
    if (!initializeSocket()) {
        return false;
    }
    
    // Connect to the server
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(config_.port);
    
    struct hostent* server = gethostbyname(config_.host.c_str());
    if (server == nullptr) {
        std::cerr << "Error resolving hostname" << std::endl;
        cleanupSocket();
        return false;
    }
    
    memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    if (::connect(socket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server" << std::endl;
        cleanupSocket();
        return false;
    }
    
    // Send authentication if API key is provided
    if (!config_.apiKey.empty()) {
        nlohmann::json authRequest = {
            {"type", "auth"},
            {"api_key", config_.apiKey}
        };
        
        if (!sendRequest(authRequest)) {
            cleanupSocket();
            return false;
        }
        
        // We should wait for auth response here in a real implementation
        // For simplicity, we're assuming auth succeeds
    }
    
    connected_ = true;
    stopping_ = false;
    
    // Start the receive thread
    receiveThread_ = std::thread(&MCPClient::receiveLoop, this);
    
    // Start the heartbeat thread
    heartbeatThread_ = std::thread(&MCPClient::heartbeatLoop, this);
    
    return true;
}

// Disconnect from MCP server
void MCPClient::disconnect() {
    stopping_ = true;
    connected_ = false;
    
    // Clean up the socket
    cleanupSocket();
    
    // Join threads
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
    
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    
    // Clear callbacks
    std::lock_guard<std::mutex> lock(mutex_);
    responseCallbacks_.clear();
    errorCallbacks_.clear();
    requestToMessageMap_.clear();
}

bool MCPClient::isConnected() const {
    return connected_;
}

std::string MCPClient::sendMessage(
    const std::vector<Message>& messages,
    const std::map<std::string, nlohmann::json>& parameters,
    ResponseCallback onResponse,
    ErrorCallback onError
) {
    if (!connected_) {
        if (onError) {
            onError("Not connected to server", -1);
        }
        return "";
    }
    
    std::string requestId = generateRequestId();
    std::string messageId = "";
    
    // Create message array for JSON
    nlohmann::json messagesJson = nlohmann::json::array();
    for (const auto& msg : messages) {
        if (msg.role == "assistant" && messageId.empty()) {
            messageId = msg.id;  // Use the assistant message ID if available
        }
        
        nlohmann::json jsonMsg = {
            {"id", msg.id},
            {"role", msg.role},
            {"content", msg.content}
        };
        
        if (!msg.metadata.empty()) {
            jsonMsg["metadata"] = msg.metadata;
        }
        
        messagesJson.push_back(jsonMsg);
    }
    
    // If no message ID was found, use the first message's ID
    if (messageId.empty() && !messages.empty()) {
        messageId = messages[0].id;
    }
    
    // Store the message ID for this request
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requestToMessageMap_[requestId] = messageId;
        
        if (onResponse) {
            responseCallbacks_[requestId] = onResponse;
        }
        
        if (onError) {
            errorCallbacks_[requestId] = onError;
        }
    }
    
    // Build request
    nlohmann::json request = {
        {"type", "message"},
        {"request_id", requestId},
        {"messages", messagesJson}
    };
    
    // Add parameters if any
    if (!parameters.empty()) {
        request["parameters"] = parameters;
    }
    
    // Add session ID if any
    if (!config_.sessionId.empty()) {
        request["session_id"] = config_.sessionId;
    }
    
    // Send the request
    if (!sendRequest(request)) {
        std::lock_guard<std::mutex> lock(mutex_);
        responseCallbacks_.erase(requestId);
        errorCallbacks_.erase(requestId);
        requestToMessageMap_.erase(requestId);
        
        if (onError) {
            onError("Failed to send request", -2);
        }
        
        return "";
    }
    
    return requestId;
}

bool MCPClient::cancelRequest(const std::string& requestId) {
    if (!connected_) {
        return false;
    }
    
    nlohmann::json cancelRequest = {
        {"type", "cancel"},
        {"request_id", requestId}
    };
    
    return sendRequest(cancelRequest);
}

std::string MCPClient::getStatus() const {
    if (connected_) {
        return "Connected to " + config_.host + ":" + std::to_string(config_.port);
    } else {
        return "Disconnected";
    }
}

bool MCPClient::initializeSocket() {
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = config_.timeoutMs / 1000;
    tv.tv_usec = (config_.timeoutMs % 1000) * 1000;
    
#ifdef _WIN32
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
#else
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
#endif
        std::cerr << "Failed to set socket timeout" << std::endl;
        cleanupSocket();
        return false;
    }
    
    return true;
}

void MCPClient::cleanupSocket() {
    if (socket_ != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(socket_);
#else
        close(socket_);
#endif
        socket_ = INVALID_SOCKET;
    }
}

void MCPClient::receiveLoop() {
    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    std::string messageBuffer;
    
    while (!stopping_) {
        if (socket_ == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        int bytesRead = recv(socket_, buffer, bufferSize - 1, 0);
        
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            messageBuffer.append(buffer);
            
            // Process complete messages
            size_t pos = 0;
            while ((pos = messageBuffer.find('\n')) != std::string::npos) {
                std::string message = messageBuffer.substr(0, pos);
                messageBuffer.erase(0, pos + 1);
                
                try {
                    nlohmann::json jsonResponse = nlohmann::json::parse(message);
                    handleResponse(jsonResponse);
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing JSON: " << e.what() << std::endl;
                }
            }
        } else if (bytesRead == 0) {
            // Connection closed by server
            connected_ = false;
            break;
        } else {
            // Socket error or timeout
#ifdef _WIN32
            int errorCode = WSAGetLastError();
            if (errorCode != WSAETIMEDOUT) {
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
#endif
                connected_ = false;
                break;
            }
        }
        
        // Prevent CPU thrashing on errors
        if (bytesRead <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void MCPClient::heartbeatLoop() {
    while (!stopping_) {
        if (connected_) {
            nlohmann::json heartbeat = {
                {"type", "ping"}
            };
            
            sendRequest(heartbeat);
        }
        
        // Send heartbeat every 30 seconds
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

bool MCPClient::sendRequest(const nlohmann::json& request) {
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    
    std::string requestStr = request.dump() + "\n";
    
    int bytesSent = send(socket_, requestStr.c_str(), requestStr.length(), 0);
    if (bytesSent != requestStr.length()) {
        std::cerr << "Failed to send full request" << std::endl;
        return false;
    }
    
    return true;
}

void MCPClient::handleResponse(const nlohmann::json& response) {
    std::string type = response.value("type", "");
    
    if (type == "message") {
        std::string requestId = response.value("request_id", "");
        std::string messageId;
        bool isComplete = response.value("complete", false);
        std::string content;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = requestToMessageMap_.find(requestId);
            if (it != requestToMessageMap_.end()) {
                messageId = it->second;
            }
        }
        
        if (response.contains("content")) {
            content = response["content"];
        }
        
        // Create response object
        Response resp;
        resp.requestId = requestId;
        resp.messageId = messageId;
        resp.content = content;
        resp.isComplete = isComplete;
        
        // Add metadata if any
        if (response.contains("metadata") && response["metadata"].is_object()) {
            for (auto& [key, value] : response["metadata"].items()) {
                resp.metadata[key] = value;
            }
        }
        
        // Call callback if registered
        ResponseCallback callback = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = responseCallbacks_.find(requestId);
            if (it != responseCallbacks_.end()) {
                callback = it->second;
            }
            
            // Remove callbacks if message is complete
            if (isComplete) {
                responseCallbacks_.erase(requestId);
                errorCallbacks_.erase(requestId);
                requestToMessageMap_.erase(requestId);
            }
        }
        
        if (callback) {
            callback(resp);
        }
    } else if (type == "error") {
        std::string requestId = response.value("request_id", "");
        std::string error = response.value("error", "Unknown error");
        int code = response.value("code", -1);
        
        handleError(requestId, error, code);
    } else if (type == "pong") {
        // Heartbeat response, nothing to do
    }
}

void MCPClient::handleError(const std::string& requestId, const std::string& error, int code) {
    ErrorCallback callback = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = errorCallbacks_.find(requestId);
        if (it != errorCallbacks_.end()) {
            callback = it->second;
        }
        
        // Remove callbacks on error
        responseCallbacks_.erase(requestId);
        errorCallbacks_.erase(requestId);
        requestToMessageMap_.erase(requestId);
    }
    
    if (callback) {
        callback(error, code);
    }
}

std::string MCPClient::generateRequestId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* chars = "0123456789abcdef";
    
    std::string uuid;
    uuid.reserve(36);
    
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            uuid += '-';
        } else if (i == 14) {
            uuid += '4';
        } else if (i == 19) {
            uuid += chars[(dis(gen) & 0x3) | 0x8];
        } else {
            uuid += chars[dis(gen)];
        }
    }
    
    return uuid;
}
