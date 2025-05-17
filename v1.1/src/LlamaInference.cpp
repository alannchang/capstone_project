#include "LlamaInference.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <functional>

LlamaInference::LlamaInference(const std::string& model_path, int n_gpu_layers, int context_size)
    : model_path_(model_path), n_gpu_layers_(n_gpu_layers), context_size_(context_size) {
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
    // Clear KV cache for sequence 0 to ensure a fresh start for this generation
    if (ctx_) { // Ensure context is valid before clearing
        llama_kv_self_clear(ctx_);
    }

    std::string response;

    std::vector<llama_token> prompt_tokens;
    prompt_tokens.resize(prompt.length() + 16);
    int n_prompt_tokens = llama_tokenize(
        vocab_,
        prompt.c_str(), 
        prompt.length(), 
        prompt_tokens.data(), 
        prompt_tokens.size(), 
        false, 
        true
    );

    if (n_prompt_tokens < 0) {
        fprintf(stderr, "failed to tokenize the prompt (error code: %d)\\n", n_prompt_tokens);
        return "";
    }
    prompt_tokens.resize(n_prompt_tokens);

    if (prompt_tokens.empty() && !prompt.empty()) {
        fprintf(stderr, "failed to tokenize the prompt (resulted in empty token list)\\n");
        return "";
    }
    
    // Initialize batch. Max batch size set to 512 or num_prompt_tokens. embd=0, n_seq_max=1 for now.
    llama_batch batch = llama_batch_init(std::max(512, n_prompt_tokens), 0, 1);

    // Add prompt tokens to the batch
    batch.n_tokens = n_prompt_tokens;
    for (int i = 0; i < n_prompt_tokens; ++i) {
        batch.token[i]    = prompt_tokens[i];
        batch.pos[i]      = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0]= 0; // Assuming sequence ID 0
        batch.logits[i]   = false;
    }
    if (batch.n_tokens > 0) {
        batch.logits[batch.n_tokens - 1] = true; // Request logits for the last token of the prompt
    }

    llama_token new_token_id;
    int n_cur = 0; // current position in the sequence
    
    while (true) {
        int n_ctx = llama_n_ctx(ctx_);
        // Corrected: Use llama_get_kv_cache_used_cells for total KV cache usage.
        int n_ctx_used = llama_get_kv_cache_used_cells(ctx_);

        // n_cur here is the total length of the sequence if we were to submit the current batch.
        // For the first pass (prompt processing), batch.n_tokens is n_prompt_tokens.
        // For subsequent passes (token generation), batch.n_tokens is 1.
        // n_ctx_used is tokens already in KV. We are adding batch.n_tokens.
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            fprintf(stderr, "context size exceeded: %d + %d > %d\\n", n_ctx_used, batch.n_tokens, n_ctx);
            llama_batch_free(batch);
            return response;
        }
        
        if (llama_decode(ctx_, batch)) {
            fprintf(stderr, "failed to decode\\n");
            llama_batch_free(batch);
            return response;
        }
        
        // After the first decode (prompt), n_cur should be n_prompt_tokens.
        // This is the starting position for the next generated token.
        if (n_cur == 0) {
             n_cur = batch.n_tokens; 
        }

        new_token_id = llama_sampler_sample(sampler_, ctx_, -1);
        
        // LOG_DEBUG("Decoding token: {}", new_token_id);

        // Check for End-of-Generation token
        if (llama_vocab_is_eog(llama_model_get_vocab(model_), new_token_id)) {
            // LOG_INFO("EOG token found. Stopping generation.");
            // llama_batch_free(batch); // Removed to prevent double-free
            break;
        }
        
        char piece_buf[256]; 
        int piece_len = llama_token_to_piece(vocab_, new_token_id, piece_buf, sizeof(piece_buf), 0, true);
        std::string piece_str;

        if (piece_len >= 0) {
            piece_str.assign(piece_buf, piece_len);
        } else {
            fprintf(stderr, "failed to convert token to piece (error/buf too small: %d)\\n", piece_len);
        }
        
        if (!piece_str.empty()) {
            token_callback(piece_str);
            response += piece_str;
        }
        
        // Prepare batch for the next token
        batch.n_tokens = 1;
        batch.token[0]    = new_token_id;
        batch.pos[0]      = n_cur; // Position of the new token in the sequence
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0]= 0;
        batch.logits[0]   = true; // Request logits for this new token to continue generation
        
        n_cur++; // Increment sequence position for the next token
    }
    
    llama_batch_free(batch); // Free batch when done with generation loop
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
