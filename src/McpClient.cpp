#include "McpClient.hpp"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


McpClient::McpClient(const std::string& server_ip, int port) : server_ip_(server_ip), port_(port), socket_fd_(-1) {}

McpClient::~McpClient() {
    disconnect();
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

void McpClient::handleReceive() {
    char buffer[4096];
    while (is_connected()) {
        ssize_t bytes_received = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string received_message(buffer);
            std::cout << "Received: " << received_message << std::endl; // Basic output for now
        } else if (bytes_received == 0) {
            std::cerr << "Connection closed by server." << std::endl;
            disconnect();
        }
    }
    disconnect();
}

bool McpClient::connect() {
    if (socket_fd_ != -1) {
        std::cerr << "Already connected." << std::endl;
        return true;
    }

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == -1) {
        perror("socket failed");
        return false;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect failed");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    std::cout << "Connected to MCP server: " << server_ip_ << ":" << port_ << std::endl;
    // Start the receive thread after successful connection
    receive_thread_ = std::thread(&McpClient::handleReceive, this);
    
    return true;
}

void McpClient::disconnect() {
    if (socket_fd_ != -1) {
        std::cout << "Disconnecting from MCP server." << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
    }
}

bool McpClient::sendMessage(const json& message) {
    if (socket_fd_ == -1) {
        std::cerr << "Not connected." << std::endl;
        return false;
    }

    try {
        std::string message_str = message.dump();
        ssize_t bytes_sent = send(socket_fd_, message_str.c_str(), message_str.length(), 0);

        if (bytes_sent == -1) {
            perror("send failed");
            return false;
        }
        if (bytes_sent < message_str.length()) {
            std::cerr << "Incomplete send: only sent " << bytes_sent << " of " << message_str.length() << " bytes." << std::endl;
            return false;
        }
        std::cout << "Message sent: " << message_str << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error sending message: " << e.what() << std::endl;
        return false; 
    }
}

std::string McpClient::receiveMessage() {
    // This method is deprecated and the receive functionality now done in handleReceive
    // keeping for now to avoid having to refactor code that may use it.

    //Remove this throw if needed
    throw std::runtime_error("receiveMessage() is deprecated and should not be called.");
}



bool McpClient::is_connected() const {
    return socket_fd_ != -1;
}