// LlamaInference.h
#ifndef LLAMA_INFERENCE_H
#define LLAMA_INFERENCE_H

#include "llama.h"
#include <string>
#include <vector>

class LlamaInference {
public:
    // Constructor with configuration options
    LlamaInference(const std::string& model_path, int n_gpu_layers = 99, int context_size = 2048);
    
    // Destructor to clean up resources
    ~LlamaInference();
    
    // Initialize the model, context, and sampler
    bool initialize();
    
    // Generate a response for a given prompt
    std::string generate(const std::string& prompt);
    
    // Chat functionality with message history
    std::string chat(const std::string& user_message);
    
    // Reset the chat history
    void resetChat();
    
    // Set parameters
    void setContextSize(int n_ctx);
    void setGpuLayers(int ngl);
    
private:
    // Configuration
    std::string model_path_;
    int n_gpu_layers_;
    int context_size_;
    
    // LLAMA resources
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    
    // Chat history
    std::vector<llama_chat_message> messages_;
    std::vector<char> formatted_;
    int prev_len_ = 0;
    
    // Clean up resources
    void cleanup();
};

#endif // LLAMA_INFERENCE_H
