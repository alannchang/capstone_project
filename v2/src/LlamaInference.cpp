#include "LlamaInference.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <functional>

LlamaInference::LlamaInference(const std::string& model_path, int n_gpu_layers, int context_size)
    : model_path_(model_path), n_gpu_layers_(n_gpu_layers), context_size_(context_size) {
    // No need to initialize logger here - it should be already initialized by the main program
    LOG_INFO("LlamaInference instance created with model path: {}", model_path_.c_str());
    LOG_DEBUG("GPU Layers: {}, Context Size: {}", n_gpu_layers_, context_size_);
}

LlamaInference::~LlamaInference() {
    LOG_INFO("LlamaInference destructor called");
    cleanup();
}

bool LlamaInference::initialize() {
    // Only print errors
    llama_log_set([](enum ggml_log_level level, const char* text, void* /* user_data */) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
            LOG_ERROR("GGML: {}", text);
        }
    }, nullptr);
    
    LOG_INFO("Initializing LlamaInference");
    
    // Load dynamic backends
    ggml_backend_load_all();
    
    // Initialize the model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = n_gpu_layers_;
    model_ = llama_model_load_from_file(model_path_.c_str(), model_params);
    if (!model_) {
        LOG_ERROR("Unable to load model from path: {}", model_path_.c_str());
        return false;
    }
    
    LOG_INFO("Model loaded successfully");
    vocab_ = llama_model_get_vocab(model_);
    
    // Initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = context_size_;
    ctx_params.n_batch = context_size_;
    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        LOG_ERROR("Failed to create the llama_context");
        cleanup();
        return false;
    }
    
    LOG_INFO("Context initialized with size: {}", context_size_);
    
    // Initialize the sampler
    sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    
    LOG_INFO("Sampler initialized");
    
    // Prepare chat history buffer
    formatted_.resize(context_size_);
    
    // Initialize chat with system prompt if set
    if (!system_prompt_.empty()) {
        LOG_DEBUG("Initializing chat with system prompt");
        initializeChat();
    }
    
    LOG_INFO("Initialization complete");
    return true;
}

void LlamaInference::setSystemPrompt(const std::string& system_prompt) {
    system_prompt_ = system_prompt;
    if (model_ && ctx_) {
        initializeChat();
    }
}

void LlamaInference::initializeChat() {
    // Clear existing messages
    resetChat();
    
    if (!system_prompt_.empty()) {
        // Add system prompt as the first message
        llama_chat_message system_msg = { "system", strdup(system_prompt_.c_str()) };
        messages_.push_back(system_msg);
        
        // Update prev_len_ to include system message
        const char* tmpl = llama_model_chat_template(model_, nullptr);
        prev_len_ = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), false, nullptr, 0);
        if (prev_len_ < 0) {
            LOG_ERROR("Failed to apply the chat template for system prompt");
            prev_len_ = 0;
        }
    }
}

std::string LlamaInference::generate(const std::string& prompt, bool stream_output) {
    return generateWithCallback(prompt, [stream_output](const std::string& piece) {
        if (stream_output) {
            printf("%s", piece.c_str());
            fflush(stdout);
        }
    });
}

std::string LlamaInference::generateWithCallback(
    const std::string& prompt, 
    std::function<void(const std::string&)> token_callback
) {
    LOG_DEBUG("Generating response for prompt length: {}", prompt.size());
    std::string response;
    const bool is_first = llama_get_kv_cache_used_cells(ctx_) == 0;
    
    // Tokenize the prompt
    const int n_prompt_tokens = -llama_tokenize(vocab_, prompt.c_str(), prompt.size(), NULL, 0, is_first, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    if (llama_tokenize(vocab_, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), is_first, true) < 0) {
        LOG_ERROR("Failed to tokenize the prompt");
        return "";
    }
    
    LOG_DEBUG("Tokenized prompt into {} tokens", prompt_tokens.size());
    
    // Prepare a batch for the prompt
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    llama_token new_token_id;
    
    while (true) {
        // Check if we have enough space in the context to evaluate this batch
        int n_ctx = llama_n_ctx(ctx_);
        int n_ctx_used = llama_get_kv_cache_used_cells(ctx_);
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            LOG_ERROR("Context size exceeded: used={}, needed={}, max={}", n_ctx_used, batch.n_tokens, n_ctx);
            break;
        }
        
        if (llama_decode(ctx_, batch)) {
            LOG_ERROR("Failed to decode batch");
            break;
        }
        
        // Sample the next token
        new_token_id = llama_sampler_sample(sampler_, ctx_, -1);
        
        // Is it an end of generation?
        if (llama_vocab_is_eog(vocab_, new_token_id)) {
            LOG_DEBUG("End of generation reached");
            break;
        }
        
        // Convert the token to a string and add it to the response
        char buf[256];
        int n = llama_token_to_piece(vocab_, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            LOG_ERROR("Failed to convert token to piece");
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
    
    LOG_DEBUG("Generation complete, response length: {}", response.size());
    return response;
}

std::string LlamaInference::chat(const std::string& user_message, bool stream_output) {
    if (!model_ || !ctx_ || !sampler_) {
        LOG_ERROR("Model not initialized");
        return "";
    }
    
    LOG_INFO("Processing chat message: {}", user_message.c_str());
    const char* tmpl = llama_model_chat_template(model_, /* name */ nullptr);
    
    // Add the user input to the message list and format it
    llama_chat_message user_msg = { "user", strdup(user_message.c_str()) };
    messages_.push_back(user_msg);
    
    int new_len = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), true, formatted_.data(), formatted_.size());
    if (new_len > (int)formatted_.size()) {
        LOG_DEBUG("Resizing formatted buffer from {} to {}", formatted_.size(), new_len);
        formatted_.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), true, formatted_.data(), formatted_.size());
    }
    
    if (new_len < 0) {
        LOG_ERROR("Failed to apply the chat template");
        return "";
    }
    
    // Remove previous messages to obtain the prompt to generate the response
    std::string prompt(formatted_.begin() + prev_len_, formatted_.begin() + new_len);
    
    // Generate a response with streaming if requested
    std::string response = generate(prompt, stream_output);
    
    LOG_INFO("Generated response length: {}", response.size());
    
    // Add the response to the messages
    llama_chat_message assistant_msg = { "assistant", strdup(response.c_str()) };
    messages_.push_back(assistant_msg);
    
    prev_len_ = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), false, nullptr, 0);
    if (prev_len_ < 0) {
        LOG_ERROR("Failed to apply the chat template");
        return "";
    }
    
    return response;
}

void LlamaInference::resetChat() {
    // Free message contents
    for (auto& msg : messages_) {
        free(const_cast<char*>(msg.content));
    }
    
    messages_.clear();
    prev_len_ = 0;
    
    // Re-initialize with system prompt if set
    if (!system_prompt_.empty()) {
        llama_chat_message system_msg = { "system", strdup(system_prompt_.c_str()) };
        messages_.push_back(system_msg);
        
        const char* tmpl = llama_model_chat_template(model_, nullptr);
        prev_len_ = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), false, nullptr, 0);
        if (prev_len_ < 0) {
            LOG_ERROR("Failed to apply the chat template for system prompt");
            prev_len_ = 0;
        }
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
    }
}

void LlamaInference::setGpuLayers(int ngl) {
    // This requires reinitializing the model
    n_gpu_layers_ = ngl;
    
    if (model_) {
        cleanup();
        initialize();
    }
}

void LlamaInference::cleanup() {
    LOG_DEBUG("Cleaning up LlamaInference resources");
    
    // Free resources
    for (auto& msg : messages_) {
        free(const_cast<char*>(msg.content));
    }
    messages_.clear();
    
    if (sampler_) {
        LOG_DEBUG("Freeing sampler");
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    
    if (ctx_) {
        LOG_DEBUG("Freeing context");
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    
    if (model_) {
        LOG_DEBUG("Freeing model");
        llama_model_free(model_);
        model_ = nullptr;
    }
    
    LOG_INFO("Cleanup complete");
}
