#ifndef MCP_CLIENT_HPP
#define MCP_CLIENT_HPP

#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <nlohmann/json.hpp>
#include <atomic>

class McpClient {
public:
    McpClient(const std::string& server_ip, int port);
    ~McpClient();

    bool connect();
    void disconnect();
    bool sendMessage(const nlohmann::json& message); 
    nlohmann::json receiveMessage(); 
    bool isConnected();

private:
    std::string server_ip_;
    int server_port_; 
    int socket_fd_; 
    std::thread receive_thread_; 
    bool should_stop_receiving_ = false;
    std::atomic<bool> is_connected_ = false;
    nlohmann::json received_message_; 
    std::mutex receive_mutex_; 

    void receiveMessages(); 
};

#endif // MCP_CLIENT_HPP