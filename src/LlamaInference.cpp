#include "LlamaInference.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <functional>

LlamaInference::LlamaInference(const std::string& model_path, int n_gpu_layers, int context_size, bool forward_to_mcp)
    : model_path_(model_path), n_gpu_layers_(n_gpu_layers), context_size_(context_size), forward_to_mcp_(forward_to_mcp) {
}

bool LlamaInference::getForwardToMcp() const {
    return forward_to_mcp_;
}

LlamaInference::~LlamaInference() {
    cleanup();
}

bool LlamaInference::initialize() {
    // Only print errors
    llama_log_set([](enum ggml_log_level level, const char* text, void* /* user_data */) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
            fprintf(stderr, "%s", text);
        }
    }, nullptr);
    
    // Load dynamic backends
    ggml_backend_load_all();
    
    // Initialize the model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = n_gpu_layers_;
    model_ = llama_model_load_from_file(model_path_.c_str(), model_params);
    if (!model_) {
        fprintf(stderr, "error: unable to load model\n");
        return false;
    }
    
    vocab_ = llama_model_get_vocab(model_);
    
    // Initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = context_size_;
    ctx_params.n_batch = context_size_;
    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        fprintf(stderr, "error: failed to create the llama_context\n");
        cleanup();
        return false;
    }
    
    // Initialize the sampler
    sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    
    // Prepare chat history buffer
    formatted_.resize(context_size_);
    
    // Initialize chat if system prompt is set
    if (!system_prompt_.empty()) {
        initializeChat();
    }
    
    return true;
}

void LlamaInference::setSystemPrompt(const std::string& system_prompt) {
    system_prompt_ = system_prompt;
    
    // Reset and initialize with the new system prompt if we're already initialized
    if (model_ && ctx_) {
        resetChat();
        initializeChat();
    }
}

void LlamaInference::initializeChat() {
    if (system_prompt_.empty()) {
        return;
    }
    
    // Clear any existing messages first
    for (auto& msg : messages_) {
        free(const_cast<char*>(msg.content));
    }
    messages_.clear();
    prev_len_ = 0;
    
    // Add system message to the beginning of the chat
    messages_.push_back({"system", strdup(system_prompt_.c_str())});
    
    // Format the system message
    const char* tmpl = llama_model_chat_template(model_, /* name */ nullptr);
    prev_len_ = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), false, nullptr, 0);
    
    if (prev_len_ < 0) {
        fprintf(stderr, "failed to apply chat template for system prompt\n");
        prev_len_ = 0;
    }
}

std::string LlamaInference::generate(const std::string& prompt, bool stream_output, std::string& output_string, std::function<void()> redraw_ui) {
    return generateWithCallback(prompt, [stream_output, &output_string, redraw_ui](const std::string& piece) {
        if (stream_output) {
            output_string += piece;
            redraw_ui();
        }
    });
}

std::string LlamaInference::generateWithCallback(
    const std::string& prompt, 
    std::function<void(const std::string&)> token_callback
) {
    std::string response;
    const bool is_first = llama_get_kv_cache_used_cells(ctx_) == 0;
    
    // Tokenize the prompt
    const int n_prompt_tokens = -llama_tokenize(vocab_, prompt.c_str(), prompt.size(), NULL, 0, is_first, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    if (llama_tokenize(vocab_, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), is_first, true) < 0) {
        fprintf(stderr, "failed to tokenize the prompt\n");
        return "";
    }
    
    // Prepare a batch for the prompt
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    llama_token new_token_id;
    
    while (true) {
        // Check if we have enough space in the context to evaluate this batch
        int n_ctx = llama_n_ctx(ctx_);
        int n_ctx_used = llama_get_kv_cache_used_cells(ctx_);
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            fprintf(stderr, "context size exceeded\n");
            break;
        }
        
        if (llama_decode(ctx_, batch)) {
            fprintf(stderr, "failed to decode\n");
            break;
        }
        
        // Sample the next token
        new_token_id = llama_sampler_sample(sampler_, ctx_, -1);
        
        // Is it an end of generation?
        if (llama_vocab_is_eog(vocab_, new_token_id)) {
            break;
        }
        
        // Convert the token to a string and add it to the response
        char buf[256];
        int n = llama_token_to_piece(vocab_, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            fprintf(stderr, "failed to convert token to piece\n");
            break;
        }
        
        std::string piece(buf, n);
        
        // Call the token callback with the new piece
        token_callback(piece);
        
        // Add the piece to the full response
        response += piece;
        
        // Prepare the next batch with the sampled token
        batch = llama_batch_get_one(&new_token_id, 1);
    }
    
    return response;
}

std::string LlamaInference::chat(const std::string& user_message, 
    bool stream_output, std::string& output_string, std::function<void()> redraw_ui) {
    if (!model_ || !ctx_ || !sampler_) {
        fprintf(stderr, "model not initialized\n");
        return "";
    }
    
    const char* tmpl = llama_model_chat_template(model_, /* name */ nullptr);
    
    // Add the user input to the message list and format it
    messages_.push_back({"user", strdup(user_message.c_str())});
    
    int new_len = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), true, formatted_.data(), formatted_.size());
    if (new_len > (int)formatted_.size()) {
        formatted_.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), true, formatted_.data(), formatted_.size());
    }
    
    if (new_len < 0) {
        fprintf(stderr, "failed to apply the chat template\n");
        return "";
    }
    
    // Remove previous messages to obtain the prompt to generate the response
    std::string prompt(formatted_.begin() + prev_len_, formatted_.begin() + new_len);
    
    // Generate a response with streaming if requested
    std::string response = generate(prompt, stream_output, output_string, redraw_ui);
    
    // Add the response to the messages
    messages_.push_back({"assistant", strdup(response.c_str())});
    
    prev_len_ = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), false, nullptr, 0);
    if (prev_len_ < 0) {
        fprintf(stderr, "failed to apply the chat template\n");
        return "";
    }
    
    return response;
}

void LlamaInference::setForwardToMcp(bool forward_to_mcp) {
    forward_to_mcp_ = forward_to_mcp;
}

void LlamaInference::resetChat() {
    // Free message contents
    for (auto& msg : messages_) {
        free(const_cast<char*>(msg.content));
    }
    
    messages_.clear();
    prev_len_ = 0;
    
    // Reinitialize with system prompt if set
    if (!system_prompt_.empty()) {
        initializeChat();
    }
}

void LlamaInference::setContextSize(int n_ctx) {
    // This requires reinitializing the context if already created
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    
    context_size_ = n_ctx;
    formatted_.resize(context_size_);
    
    if (model_) {
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = context_size_;
        ctx_params.n_batch = context_size_;
        ctx_ = llama_init_from_model(model_, ctx_params);
        
        // Reinitialize chat with system prompt if needed
        if (!system_prompt_.empty()) {
            initializeChat();
        }
    }
}

void LlamaInference::setGpuLayers(int ngl) {
    // This requires reinitializing the model
    n_gpu_layers_ = ngl;
    
    if (model_) {
        std::string saved_system_prompt = system_prompt_;
        cleanup();
        initialize();
        
        // Restore system prompt if needed
        if (!saved_system_prompt.empty()) {
            setSystemPrompt(saved_system_prompt);
        }
    }
}

void LlamaInference::cleanup() {
    // Free resources
    for (auto& msg : messages_) {
        free(const_cast<char*>(msg.content));
    }
    messages_.clear();
    
    if (sampler_) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
}
