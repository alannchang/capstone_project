#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <atomic>
#include <memory>
#include <map>

// Platform-specific socket headers
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    typedef int socket_t;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include <nlohmann/json.hpp> 

class MCPClient {
public:
    struct Message {
        std::string id;
        std::string role;
        std::string content;
        std::map<std::string, nlohmann::json> metadata;
    };

    struct Response {
        std::string requestId;
        std::string messageId;
        std::string content;
        bool isComplete;
        std::map<std::string, nlohmann::json> metadata;
    };

    struct ConnectionConfig {
        std::string host = "localhost";
        int port = 8080;
        std::string apiKey;
        std::string sessionId = "";
        int timeoutMs = 30000;
        bool useTLS = false;
    };

    using ResponseCallback = std::function<void(const Response&)>;
    using ErrorCallback = std::function<void(const std::string&, int)>;

    MCPClient();
    ~MCPClient();

    bool connect(const ConnectionConfig& config);
    void disconnect();
    bool isConnected() const;

    // Main API method to send a message to the LLM
    std::string sendMessage(
        const std::vector<Message>& messages,
        const std::map<std::string, nlohmann::json>& parameters = {},
        ResponseCallback onResponse = nullptr,
        ErrorCallback onError = nullptr
    );

    // Cancel an ongoing request
    bool cancelRequest(const std::string& requestId);

    // Get connection status
    std::string getStatus() const;

private:
    ConnectionConfig config_;
    socket_t socket_ = INVALID_SOCKET;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopping_{false};
    std::thread receiveThread_;
    std::thread heartbeatThread_;
    
    std::mutex mutex_;
    std::map<std::string, ResponseCallback> responseCallbacks_;
    std::map<std::string, ErrorCallback> errorCallbacks_;
    std::map<std::string, std::string> requestToMessageMap_;
    
    // Internal methods
    bool initializeSocket();
    void cleanupSocket();
    void receiveLoop();
    void heartbeatLoop();
    bool sendRequest(const nlohmann::json& request);
    void handleResponse(const nlohmann::json& response);
    void handleError(const std::string& requestId, const std::string& error, int code);
    std::string generateRequestId();
};

#endif // MCP_CLIENT_H
