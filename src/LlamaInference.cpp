#include "LlamaInference.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <functional>
#include <stdexcept> // For std::runtime_error

// Updated constructor to accept Logger
LlamaInference::LlamaInference(const std::string& model_path, Logger& logger, int n_gpu_layers, int context_size)
    : model_path_(model_path), n_gpu_layers_(n_gpu_layers), context_size_(context_size), logger_(logger) {
    logger_.log(LogLevel::DEBUG, "LlamaInference constructor called.");
}

LlamaInference::~LlamaInference() {
    logger_.log(LogLevel::DEBUG, "LlamaInference destructor called.");
    cleanup();
}

// Implement the static callback function
void LlamaInference::llama_log_callback(enum ggml_log_level level, const char* text, void* user_data) {
    if (!user_data) return;
    Logger* logger = static_cast<Logger*>(user_data);

    // Trim trailing newline if present, as our logger adds one
    std::string msg(text);
    if (!msg.empty() && msg.back() == '\n') {
        msg.pop_back();
    }
    if (msg.empty()) return; // Don't log empty messages

    if (level == GGML_LOG_LEVEL_ERROR) {
        logger->log(LogLevel::ERROR, "llama.cpp: ", msg);
    } else if (level == GGML_LOG_LEVEL_WARN) {
        logger->log(LogLevel::WARNING, "llama.cpp: ", msg);
    } else if (level == GGML_LOG_LEVEL_INFO) {
        // Log INFO from llama.cpp as DEBUG in our logger to reduce verbosity
        logger->log(LogLevel::DEBUG, "llama.cpp: ", msg);
    } // Ignore lower levels from llama.cpp
}

bool LlamaInference::initialize() {
    logger_.log(LogLevel::INFO, "Initializing LlamaInference...");
    // Set the static callback function, passing our logger instance as user_data
    llama_log_set(&LlamaInference::llama_log_callback, &logger_);
    
    logger_.log(LogLevel::DEBUG, "Loading llama.cpp dynamic backends...");
    // Load dynamic backends
    ggml_backend_load_all();
    
    logger_.log(LogLevel::DEBUG, "Loading model: ", model_path_, " with ngl=", n_gpu_layers_);
    // Initialize the model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = n_gpu_layers_;
    model_ = llama_model_load_from_file(model_path_.c_str(), model_params);
    if (!model_) {
        std::string error_msg = "Unable to load model from: " + model_path_;
        logger_.log(LogLevel::ERROR, error_msg);
        throw LlamaException(error_msg);
    }
    logger_.log(LogLevel::INFO, "Model loaded successfully.");
    
    vocab_ = llama_model_get_vocab(model_);
    
    logger_.log(LogLevel::DEBUG, "Initializing llama context with size=", context_size_);
    // Initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = context_size_;
    ctx_params.n_batch = context_size_; // Using same batch size for simplicity
    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        cleanup(); // Cleanup before throwing
        std::string error_msg = "Failed to create llama_context";
        logger_.log(LogLevel::ERROR, error_msg);
        throw LlamaException(error_msg);
    }
    logger_.log(LogLevel::INFO, "Llama context created successfully.");
    
    logger_.log(LogLevel::DEBUG, "Initializing sampler chain...");
    // Initialize the sampler
    sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    
    // Prepare chat history buffer
    formatted_.resize(context_size_);
    
    // Initialize chat if system prompt is set
    if (!system_prompt_.empty()) {
        logger_.log(LogLevel::DEBUG, "Initializing chat with system prompt.");
        initializeChat();
    }
    
    logger_.log(LogLevel::INFO, "LlamaInference initialization complete.");
    return true;
}

void LlamaInference::setSystemPrompt(const std::string& system_prompt) {
    logger_.log(LogLevel::INFO, "Setting system prompt.");
    system_prompt_ = system_prompt;
    
    // Reset and initialize with the new system prompt if we're already initialized
    if (model_ && ctx_) {
        logger_.log(LogLevel::DEBUG, "Model already initialized, resetting chat for new system prompt.");
        resetChat();
        initializeChat();
    }
}

void LlamaInference::initializeChat() {
    if (system_prompt_.empty()) {
        logger_.log(LogLevel::DEBUG, "Skipping chat initialization (no system prompt).");
        return;
    }
    
    logger_.log(LogLevel::DEBUG, "Initializing chat internal state.");
    // Clear any existing messages first
    messages_.clear();
    prev_len_ = 0;
    
    // Add system message to the beginning of the chat
    messages_.push_back({"system", system_prompt_});
    logger_.log(LogLevel::DEBUG, "Added system message to history.");
    
    // Format the system message
    const char* tmpl = llama_model_chat_template(model_, /* name */ nullptr);

    // Temporarily convert to llama_chat_message for the API call
    std::vector<llama_chat_message> api_messages;
    api_messages.reserve(messages_.size());
    for(const auto& msg : messages_) {
        api_messages.push_back({msg.role.c_str(), msg.content.c_str()});
    }

    prev_len_ = llama_chat_apply_template(tmpl, api_messages.data(), api_messages.size(), false, nullptr, 0);
    
    if (prev_len_ < 0) {
        // std::cerr << "Warning: failed to apply chat template for system prompt" << std::endl;
        logger_.log(LogLevel::WARNING, "Failed to apply chat template for system prompt. prev_len might be incorrect.");
        prev_len_ = 0; // Reset to 0 on failure
    } else {
        logger_.log(LogLevel::DEBUG, "Applied system prompt template, prev_len=", prev_len_);
    }
}

std::string LlamaInference::generateWithCallback(
    const std::string& prompt, 
    std::function<void(const std::string&)> token_callback
) {
    logger_.log(LogLevel::DEBUG, "generateWithCallback called. Prompt length: ", prompt.length());
    std::string response;
    const bool is_first = llama_get_kv_cache_used_cells(ctx_) == 0;
    logger_.log(LogLevel::DEBUG, "Is first evaluation in context: ", is_first);
    
    // Tokenize the prompt
    logger_.log(LogLevel::DEBUG, "Tokenizing prompt...");
    const int n_prompt_tokens = -llama_tokenize(vocab_, prompt.c_str(), prompt.size(), NULL, 0, is_first, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    if (llama_tokenize(vocab_, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), is_first, true) < 0) {
        std::string error_msg = "Failed to tokenize the prompt: " + prompt.substr(0, 80) + "...";
        logger_.log(LogLevel::ERROR, error_msg);
        throw LlamaException(error_msg);
    }
    logger_.log(LogLevel::DEBUG, "Prompt tokenized into ", n_prompt_tokens, " tokens.");
    
    // Prepare a batch for the prompt
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    llama_token new_token_id;
    
    logger_.log(LogLevel::DEBUG, "Starting generation loop...");
    while (true) {
        // Check if we have enough space in the context to evaluate this batch
        int n_ctx = llama_n_ctx(ctx_);
        int n_ctx_used = llama_get_kv_cache_used_cells(ctx_);
        logger_.log(LogLevel::DEBUG, "Context: used=", n_ctx_used, ", batch=", batch.n_tokens, ", total=", n_ctx);
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            std::string error_msg = "Context size exceeded during generation";
            logger_.log(LogLevel::ERROR, error_msg, " (Used: ", n_ctx_used, " + Batch: ", batch.n_tokens, " > Total: ", n_ctx, ")");
            throw LlamaException(error_msg);
        }
        
        // Decode the batch
        if (llama_decode(ctx_, batch)) {
            std::string error_msg = "Failed during llama_decode";
            logger_.log(LogLevel::ERROR, error_msg);
            throw LlamaException(error_msg);
        }
        
        // Sample the next token
        new_token_id = llama_sampler_sample(sampler_, ctx_, -1);
        
        // Is it an end of generation?
        if (llama_vocab_is_eog(vocab_, new_token_id)) {
            logger_.log(LogLevel::DEBUG, "End of generation token encountered.");
            break;
        }
        
        // Convert the token to a string and add it to the response
        char buf[256];
        int n = llama_token_to_piece(vocab_, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            std::string error_msg = "Failed to convert token to piece";
            logger_.log(LogLevel::ERROR, error_msg);
            throw LlamaException(error_msg);
        }
        
        std::string piece(buf, n);
        
        // Call the token callback with the new piece
        token_callback(piece); // Assuming callback handles its own logging if needed
        
        // Add the piece to the full response
        response += piece;
        
        // Prepare the next batch with the sampled token
        batch = llama_batch_get_one(&new_token_id, 1);
    }
    logger_.log(LogLevel::DEBUG, "Generation loop finished. Response length: ", response.length());
    return response;
}

// Renamed from chat, uses generateWithCallback internally
std::string LlamaInference::chatWithCallback(
    const std::string& user_message, 
    std::function<void(const std::string&)> token_callback
) {
    logger_.log(LogLevel::INFO, "chatWithCallback called. User message length: ", user_message.length());
    if (!model_ || !ctx_ || !sampler_) {
        std::string error_msg = "Chat called before model was initialized";
        logger_.log(LogLevel::ERROR, error_msg);
        throw LlamaException(error_msg);
    }
    
    const char* tmpl = llama_model_chat_template(model_, /* name */ nullptr);
    
    // Add the user input to the message list and format it
    logger_.log(LogLevel::DEBUG, "Adding user message to history.");
    messages_.push_back({"user", user_message});

    // Temporarily convert to llama_chat_message for the API call
    std::vector<llama_chat_message> api_messages;
    api_messages.reserve(messages_.size());
    for(const auto& msg : messages_) {
        api_messages.push_back({msg.role.c_str(), msg.content.c_str()});
    }
    
    logger_.log(LogLevel::DEBUG, "Applying chat template...");
    int new_len = llama_chat_apply_template(tmpl, api_messages.data(), api_messages.size(), true, formatted_.data(), formatted_.size());
    if (new_len > (int)formatted_.size()) {
        logger_.log(LogLevel::DEBUG, "Resizing formatted buffer from ", formatted_.size(), " to ", new_len);
        formatted_.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, api_messages.data(), api_messages.size(), true, formatted_.data(), formatted_.size());
    }
    
    if (new_len < 0) {
        std::string error_msg = "Failed to apply chat template during chat";
        logger_.log(LogLevel::ERROR, error_msg);
        throw LlamaException(error_msg);
    }
    logger_.log(LogLevel::DEBUG, "Chat template applied. new_len=", new_len, ", prev_len=", prev_len_);
    
    // Remove previous messages to obtain the prompt to generate the response
    std::string prompt(formatted_.begin() + prev_len_, formatted_.begin() + new_len);
    logger_.log(LogLevel::DEBUG, "Generated prompt for LLM (length: ", prompt.length(), ")");
    
    // Generate a response using the callback method
    std::string response = generateWithCallback(prompt, token_callback);
    logger_.log(LogLevel::INFO, "LLM response generated (length: ", response.length(), ")");
    
    // Add the response to the messages
    logger_.log(LogLevel::DEBUG, "Adding assistant response to history.");
    messages_.push_back({"assistant", response});

    // Rebuild api_messages with the new assistant message for calculating prev_len_
    api_messages.clear();
    api_messages.reserve(messages_.size());
    for(const auto& msg : messages_) {
        api_messages.push_back({msg.role.c_str(), msg.content.c_str()});
    }
    
    prev_len_ = llama_chat_apply_template(tmpl, api_messages.data(), api_messages.size(), false, nullptr, 0);
    if (prev_len_ < 0) {
        std::string error_msg = "Failed to apply chat template after generating response";
        logger_.log(LogLevel::ERROR, error_msg);
        throw LlamaException(error_msg);
    }
    logger_.log(LogLevel::DEBUG, "Updated prev_len=", prev_len_);
    
    return response;
}

void LlamaInference::resetChat() {
    logger_.log(LogLevel::INFO, "Resetting chat history.");
    // Free message contents - no longer needed with std::string
    messages_.clear();
    prev_len_ = 0;
    
    // Reinitialize with system prompt if set
    if (!system_prompt_.empty()) {
        logger_.log(LogLevel::DEBUG, "Re-initializing chat with system prompt after reset.");
        initializeChat();
    }
    logger_.log(LogLevel::DEBUG, "Chat reset complete.");
}

void LlamaInference::setContextSize(int n_ctx) {
    logger_.log(LogLevel::INFO, "Setting context size to: ", n_ctx);
    // This requires reinitializing the context if already created
    if (ctx_) {
        logger_.log(LogLevel::DEBUG, "Freeing existing context before changing size.");
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    
    context_size_ = n_ctx;
    formatted_.resize(context_size_);
    
    if (model_) {
        logger_.log(LogLevel::DEBUG, "Re-initializing context with new size.");
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = context_size_;
        ctx_params.n_batch = context_size_;
        ctx_ = llama_init_from_model(model_, ctx_params);
        if (!ctx_) { // Check for failure during re-initialization
            std::string error_msg = "Failed to re-create context after setting size";
            logger_.log(LogLevel::ERROR, error_msg);
            // Decide on error handling: throw? Or just log and leave ctx_ as null?
            // Throwing might be better to signal the object is in a bad state.
            throw LlamaException(error_msg);
        }
        
        // Reinitialize chat with system prompt if needed
        if (!system_prompt_.empty()) {
            logger_.log(LogLevel::DEBUG, "Re-initializing chat state after context size change.");
            initializeChat();
        }
    }
}

void LlamaInference::setGpuLayers(int ngl) {
    logger_.log(LogLevel::INFO, "Setting GPU layers to: ", ngl);
    // This requires reinitializing the model
    n_gpu_layers_ = ngl;
    
    if (model_) {
        logger_.log(LogLevel::DEBUG, "Re-initializing model and context due to GPU layer change.");
        std::string saved_system_prompt = system_prompt_;
        cleanup();
        try { // Add try-catch around re-initialization
             initialize(); // This might throw
             logger_.log(LogLevel::INFO, "Re-initialization successful after GPU layer change.");
             // Restore system prompt if needed
             if (!saved_system_prompt.empty()) {
                 logger_.log(LogLevel::DEBUG, "Restoring system prompt after re-initialization.");
                 setSystemPrompt(saved_system_prompt);
             }
        } catch (const LlamaException& e) {
            logger_.log(LogLevel::ERROR, "Failed to re-initialize after setting GPU layers: ", e.what());
            // Depending on desired behavior, might re-throw or leave in a bad state.
            // For now, just log the error. The object might be unusable.
        }
    }
}

void LlamaInference::cleanup() {
    logger_.log(LogLevel::DEBUG, "Cleaning up Llama resources...");
    // Free resources
    messages_.clear();
    
    if (sampler_) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
        logger_.log(LogLevel::DEBUG, "Sampler freed.");
    }
    
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
        logger_.log(LogLevel::DEBUG, "Context freed.");
    }
    
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
        logger_.log(LogLevel::DEBUG, "Model freed.");
    }
    logger_.log(LogLevel::DEBUG, "Llama resources cleanup complete.");
}
