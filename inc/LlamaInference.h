#ifndef LLAMA_INFERENCE_H
#define LLAMA_INFERENCE_H

#include "llama.h"
#include <string>
#include <vector>
#include <functional>

class LlamaInference {
public:
    // Constructor with configuration options
    LlamaInference(const std::string& model_path, bool forward_to_mcp = false, int n_gpu_layers = 99, int context_size = 2048);
    
    // Destructor to clean up resources
    ~LlamaInference();
    
    // Initialize the model, context, and sampler
    bool initialize();
    
    // Set system prompt to guide the model's behavior
    void setSystemPrompt(const std::string& system_prompt);
    
    // Generate a response for a given prompt, with optional streaming
    std::string generate(const std::string& prompt, bool stream_output, std::string& output_string, std::function<void()> redraw_ui);
    
    // Generate with a custom callback for each token
    std::string generateWithCallback(
        const std::string& prompt, 
        std::function<void(const std::string&)> token_callback
    );
    
    // Chat functionality with message history, with optional streaming
    std::string chat(const std::string& user_message, bool stream_output, std::string& output_string, std::function<void()> redraw_ui);
    
    // Reset the chat history (keeps system prompt)
    void resetChat();
    
    // Set parameters
    void setContextSize(int n_ctx);
    
    bool isForwardToMcp() const;
    
    void setForwardToMcp(bool forward_to_mcp);
    

    void setGpuLayers(int ngl);
    
private:
    // Configuration
    std::string model_path_;
    int n_gpu_layers_;
    int context_size_;
    bool forward_to_mcp_;
    std::string system_prompt_;
    
    // LLAMA resources
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    
    // Chat history
    std::vector<llama_chat_message> messages_;
    std::vector<char> formatted_;
    int prev_len_ = 0;
    
    // Initialize chat with system prompt
    void initializeChat();
    
    // Clean up resources
    void cleanup();
};

#endif // LLAMA_INFERENCE_H
