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
    // REMOVED: llama_kv_self_clear(ctx_); // This was for independent prompts

    std::string response;

    std::vector<llama_token> prompt_tokens;
    prompt_tokens.resize(prompt.length() + 16); // Provide some buffer
    int n_prompt_tokens = llama_tokenize(
        vocab_,
        prompt.c_str(), 
        prompt.length(), 
        prompt_tokens.data(), 
        prompt_tokens.size(), 
        false, // add_bos - assuming template handles this, or it's not needed for subsequent turns
        true   // parse_special
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

    // KV Cache Overflow Management
    const int n_ctx = llama_n_ctx(ctx_);
    if (n_past_ + n_prompt_tokens > n_ctx) {
        // Calculate how many tokens we need to remove to make space for the new prompt tokens
        // and keep at least some context (e.g., half of it, or a fixed amount)
        // This is a simple strategy; more sophisticated ones might be needed for optimal performance.
        int n_tokens_to_fit_prompt = n_past_ + n_prompt_tokens - n_ctx; 
        int n_discard = n_tokens_to_fit_prompt + (n_ctx / 4); // Remove enough to fit + 1/4th of context as buffer
        n_discard = std::min(n_past_, n_discard); // Cannot discard more than available

        // fprintf(stderr, "KV cache overflow (prompt): n_past_=%d, n_prompt_tokens=%d, n_ctx=%d. Planning to discard %d tokens.\\n", n_past_, n_prompt_tokens, n_ctx, n_discard);

        if (n_discard > 0) {
            llama_kv_self_seq_rm(ctx_, 0, 0, n_discard); // Remove n_discard tokens from the beginning of sequence 0
            n_past_ -= n_discard;
            // fprintf(stderr, "KV cache after discard (prompt): n_past_ adjusted to %d\\n", n_past_);
        }
    }
    
    // Initialize batch. Max batch size set to 512 or num_prompt_tokens. embd=0, n_seq_max=1 for now.
    // The batch size should be large enough for the prompt and then 1 for generation.
    llama_batch batch = llama_batch_init(std::max(512, n_prompt_tokens), 0, 1);

    // Add prompt tokens to the batch
    batch.n_tokens = n_prompt_tokens;
    for (int i = 0; i < n_prompt_tokens; ++i) {
        batch.token[i]    = prompt_tokens[i];
        batch.pos[i]      = n_past_ + i; // Use n_past_ for continuous context
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0]= 0; // Assuming sequence ID 0
        batch.logits[i]   = false;
    }
    if (batch.n_tokens > 0) {
        batch.logits[batch.n_tokens - 1] = true; // Request logits for the last token of the prompt
    }

    // The 'n_cur' variable from previous version is effectively replaced by n_past_ management
    // int n_cur = 0; // current position in the sequence (REMOVED)
    
    while (response.length() < 2048) { // Added a safety break for max response length
        if (n_past_ >= n_ctx) { // If n_past_ (which will be pos of next token) hits context limit
            // fprintf(stderr, "Context limit reached during generation: n_past_=%d, n_ctx=%d\\n", n_past_, n_ctx);
            // Strategy: remove some tokens from the start to make space for new ones
            const int n_discard_generation = n_ctx / 4; // Discard 1/4th of the context
            
            if (n_discard_generation > 0 && n_past_ > n_discard_generation) {
                llama_kv_self_seq_rm(ctx_, 0, 0, n_discard_generation);
                n_past_ -= n_discard_generation;
                // fprintf(stderr, "KV cache after discard (generation): n_past_ adjusted to %d\\n", n_past_);
            } else if (n_past_ > 0) { // Cannot discard 1/4 if less than that exists, discard all but one to be safe
                llama_kv_self_seq_rm(ctx_, 0, 0, n_past_ -1);
                n_past_ = 1; // Keep at least one token to avoid issues, though this state is tricky
            }
            // If n_past_ is 0 here and we still need to generate, it's an edge case.
            // The current token being prepared (batch.pos[0] = n_past_) should use the new n_past_.
        }

        if (llama_decode(ctx_, batch)) {
            fprintf(stderr, "failed to decode\\n");
            llama_batch_free(batch);
            return response;
        }
        
        // After the first decode (prompt processing), update n_past_
        // This happens only once per call to generateWithCallback for the prompt.
        if (batch.n_tokens == n_prompt_tokens) { // Identifying the prompt processing decode
             n_past_ += n_prompt_tokens; 
        }

        llama_token new_token_id = llama_sampler_sample(sampler_, ctx_, -1);
        
        if (llama_vocab_is_eog(llama_model_get_vocab(model_), new_token_id)) {
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
        
        // Prepare batch for the next token (generation phase)
        batch.n_tokens = 1;
        batch.token[0]    = new_token_id;
        batch.pos[0]      = n_past_; // Position of the new token is current n_past_
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0]= 0;
        batch.logits[0]   = true; 
        
        n_past_++; // Increment n_past_ for the token just added to be decoded
    }
    
    llama_batch_free(batch);
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
    // Clear KV cache and reset past token count for the new session
    if (ctx_) {
        llama_kv_self_clear(ctx_);
    }
    n_past_ = 0;

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
