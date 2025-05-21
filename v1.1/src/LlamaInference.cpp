#include "LlamaInference.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <functional>

// Added: for json
#include "nlohmann/json.hpp"
// Added: for httplib
#include "httplib.h"

// Using alias for json
using json = nlohmann::json;

// Helper function to try parsing LLM response as a tool call
// Returns true if it's a tool call, and populates tool_name and tool_params
// This could also be a private static method of LlamaInference class
namespace { // Anonymous namespace for helper
bool tryParseToolCall(const std::string& llm_response, std::string& tool_name, json& tool_params) {
    try {
        if (llm_response.empty() || llm_response.front() != '{' || llm_response.back() != '}') {
            return false; // Not even looking like a JSON object
        }
        json parsed_json = json::parse(llm_response);
        if (parsed_json.is_object() && parsed_json.contains("tool_name") && parsed_json["tool_name"].is_string()) {
            tool_name = parsed_json["tool_name"].get<std::string>();
            // Parameters are optional for some tools, but the key "parameters" should exist if any are expected.
            // If "parameters" is not found, we'll assume it means no parameters or an empty JSON object {}.
            if (parsed_json.contains("parameters") && parsed_json["parameters"].is_object()) {
                tool_params = parsed_json["parameters"];
            } else {
                tool_params = json::object(); // Default to empty object if not present or not an object
            }
            return true;
        }
    } catch (const json::parse_error& e) {
        // This is expected if the response is not JSON, so don't flood stderr
        // std::cerr << "JSON parse error in tryParseToolCall: " << e.what() << " for response: " << llm_response << std::endl;
        return false;
    } catch (const std::exception& e) { // Catch other potential exceptions
        // std::cerr << "Exception in tryParseToolCall: " << e.what() << std::endl;
        return false;
    }
    return false;
}
} // end anonymous namespace

LlamaInference::LlamaInference(const std::string& model_path, int n_gpu_layers, int context_size, int max_response_chars)
    : model_path_(model_path), n_gpu_layers_(n_gpu_layers), context_size_(context_size), max_response_chars_(max_response_chars) {
    debug_log_file_.open("llama_debug.log", std::ios::app); // Open log file in append mode
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "\n--- LlamaInference Initialized ---" << std::endl << std::flush;
    } else {
        std::cerr << "CRITICAL ERROR: Failed to open llama_debug.log!" << std::endl;
    }
}

LlamaInference::~LlamaInference() {
    cleanup();
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "--- LlamaInference Cleanup ---" << std::endl << std::flush;
        debug_log_file_.close();
    }
}

bool LlamaInference::initialize() {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::initialize: Method entered. Model path: " << model_path_ << std::endl << std::flush;
    } else {
        std::cerr << "ERROR LlamaInference::initialize: Method entered, but log file not open! Model path: " << model_path_ << std::endl;
    }

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
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "ERROR LlamaInference::initialize: llama_model_load_from_file failed for path: " << model_path_ << std::endl << std::flush;
        } else {
            std::cerr << "ERROR LlamaInference::initialize: llama_model_load_from_file failed (log file not open). Path: " << model_path_ << std::endl;
        }
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
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "ERROR LlamaInference::initialize: llama_init_from_model failed." << std::endl << std::flush;
        } else {
            std::cerr << "ERROR LlamaInference::initialize: llama_init_from_model failed (log file not open)." << std::endl;
        }
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
    
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::initialize: Initialization successful." << std::endl << std::flush;
    }
    return true;
}

void LlamaInference::setSystemPrompt(const std::string& system_prompt) {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::setSystemPrompt: Method entered." << std::endl << std::flush;
    } else {
        std::cerr << "INFO LlamaInference::setSystemPrompt: Method entered (log not open)." << std::endl;
    }
    system_prompt_ = system_prompt;
    
    // Reset and initialize with the new system prompt if we're already initialized
    if (model_ && ctx_) {
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG LlamaInference::setSystemPrompt: Model and context exist, re-initializing chat." << std::endl << std::flush;
        }
        resetChat();
        initializeChat();
    } else {
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG LlamaInference::setSystemPrompt: Model/context not yet loaded. Prompt set, chat will be initialized later." << std::endl << std::flush;
        }
    }
}

void LlamaInference::initializeChat() {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::initializeChat: Method entered." << std::endl << std::flush;
    } else {
        std::cerr << "INFO LlamaInference::initializeChat: Method entered (log not open)." << std::endl;
    }

    if (system_prompt_.empty()) {
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG LlamaInference::initializeChat: System prompt is empty, skipping." << std::endl << std::flush;
        }
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
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "ERROR LlamaInference::initializeChat: llama_chat_apply_template failed for system prompt. Error code: " << prev_len_ << std::endl << std::flush;
        }
        prev_len_ = 0;
    } else {
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG LlamaInference::initializeChat: System prompt applied. prev_len_ = " << prev_len_ << std::endl << std::flush;
        }
    }
}

std::string LlamaInference::generate(const std::string& prompt, bool stream_output, std::string& output_string, std::function<void()> redraw_ui) {
    // This is an older method, ensure it logs if ever called directly.
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::generate: Method entered (older version). Prompt: " << prompt.substr(0,50) << "..." << std::endl << std::flush;
    } else {
        std::cerr << "INFO LlamaInference::generate: Method entered (older version, log not open). Prompt: " << prompt.substr(0,50) << "..." << std::endl;
    }
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
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::generateWithCallback: Method entered." << std::endl;
        debug_log_file_ << "DEBUG LlamaInference::generateWithCallback: Received prompt (first 200 chars): " << prompt.substr(0, 200) << "..." << std::endl;
        debug_log_file_ << "DEBUG LlamaInference::generateWithCallback: Prompt length: " << prompt.length() << std::endl << std::flush;
    } else {
        std::cerr << "INFO LlamaInference::generateWithCallback: Method entered (log file not open)." << std::endl;
        std::cerr << "INFO LlamaInference::generateWithCallback: Received prompt (first 200 chars): " << prompt.substr(0, 200) << "..." << std::endl;
    }

    if (prompt.empty()) {
        if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::generateWithCallback: Received an empty prompt!" << std::endl << std::flush;
        else std::cerr << "ERROR LlamaInference::generateWithCallback: Received an empty prompt!" << std::endl;
        return ""; // Early exit if prompt is empty
    }
    if (!model_ || !ctx_ || !sampler_ || !vocab_) {
        if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::generateWithCallback: Called with uninitialized Llama resources!" << std::endl << std::flush;
        else std::cerr << "ERROR LlamaInference::generateWithCallback: Called with uninitialized Llama resources!" << std::endl;
        return "[Error: Llama resources not initialized in generateWithCallback]";
    }

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
        fprintf(stderr, "failed to tokenize the prompt (error code: %d)\n", n_prompt_tokens);
        if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::generateWithCallback: llama_tokenize failed. Code: " << n_prompt_tokens << std::endl << std::flush;
        return "";
    }
    prompt_tokens.resize(n_prompt_tokens);

    if (prompt_tokens.empty() && !prompt.empty()) {
        fprintf(stderr, "failed to tokenize the prompt (resulted in empty token list)\n");
        if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::generateWithCallback: llama_tokenize resulted in empty token list for non-empty prompt." << std::endl << std::flush;
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

        if (n_discard > 0) {
            llama_kv_self_seq_rm(ctx_, 0, 0, n_discard); // Remove n_discard tokens from the beginning of sequence 0
            n_past_ -= n_discard;
            if (debug_log_file_.is_open()) {
                debug_log_file_ << "DEBUG LlamaInference::generateWithCallback: KV cache overflow handled. Discarded " << n_discard << " tokens. n_past_ adjusted to " << n_past_ << std::endl << std::flush;
            }
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
    
    while (response.length() < max_response_chars_) { // Added a safety break for max response length
        if (n_past_ >= n_ctx) { // If n_past_ (which will be pos of next token) hits context limit
            // fprintf(stderr, "Context limit reached during generation: n_past_=%d, n_ctx=%d\n", n_past_, n_ctx);
            // Strategy: remove some tokens from the start to make space for new ones
            const int n_discard_generation = n_ctx / 4; // Discard 1/4th of the context
            
            if (n_discard_generation > 0 && n_past_ > n_discard_generation) {
                llama_kv_self_seq_rm(ctx_, 0, 0, n_discard_generation);
                n_past_ -= n_discard_generation;
                if (debug_log_file_.is_open()) {
                    debug_log_file_ << "DEBUG LlamaInference::generateWithCallback: KV cache nearing full during generation. Discarded. n_past_ is now " << n_past_ << std::endl << std::flush;
                }
            } else if (n_past_ > 0) { // Cannot discard 1/4 if less than that exists, discard all but one to be safe
                llama_kv_self_seq_rm(ctx_, 0, 0, n_past_ -1);
                n_past_ = 1; // Keep at least one token to avoid issues, though this state is tricky
            }
            // If n_past_ is 0 here and we still need to generate, it's an edge case.
            // The current token being prepared (batch.pos[0] = n_past_) should use the new n_past_.
        }

        if (llama_decode(ctx_, batch) != 0) {
            fprintf(stderr, "llama_decode failed\n");
            if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::generateWithCallback: llama_decode failed after processing prompt." << std::endl << std::flush;
            return response; // Return whatever we might have accumulated or empty
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
            fprintf(stderr, "failed to convert token to piece (error/buf too small: %d)\n", piece_len);
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

    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::generateWithCallback: Finished generation. Total response length: " << response.length() << std::endl << std::flush;
    }
    return response;
}

std::string LlamaInference::chat(const std::string& user_message, 
    bool stream_output, std::string& output_string, std::function<void()> redraw_ui) {

    // ***** VERY FIRST LINE FOR DEBUGGING *****
    fprintf(stderr, "****** URGENT DEBUG: LlamaInference::chat HAS BEEN ENTERED! ******\n");
    fflush(stderr); // Force it to print NOW

    // Existing log logic
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "****** DEBUG LlamaInference::chat: METHOD ENTERED (after fprintf) ******" << std::endl;
        debug_log_file_ << "DEBUG LlamaInference::chat: User input (first 100 chars): " << user_message.substr(0, 100) << (user_message.length() > 100 ? "..." : "") << std::endl;
        debug_log_file_ << "DEBUG LlamaInference::chat: stream_output: " << stream_output << std::endl << std::flush;
    } else {
        std::cerr << "****** ERROR LlamaInference::chat: METHOD ENTERED (after fprintf), BUT LOG FILE NOT OPEN! ******" << std::endl;
        std::cerr << "ERROR LlamaInference::chat: User input (first 100 chars): " << user_message.substr(0, 100) << (user_message.length() > 100 ? "..." : "") << std::endl;
    }

    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Checking model, context, and sampler..." << std::endl << std::flush;
    if (!model_ || !ctx_ || !sampler_) {
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "ERROR LlamaInference::chat: Model/context/sampler not initialized! model=" << model_ << ", ctx=" << ctx_ << ", sampler=" << sampler_ << std::endl << std::flush;
        } else {
            std::cerr << "ERROR LlamaInference::chat: Model/context/sampler not initialized!" << std::endl;
        }
        fprintf(stderr, "model not initialized\n"); // This might not be visible with FTXUI
        return "[Error: Model not initialized]";
    }
    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Model, context, and sampler OK." << std::endl << std::flush;
    
    // Add user message to history
    // Note: llama_chat_message content must be managed (strdup/free) if LlamaInference owns it.
    // Assuming LlamaInference's messages_ vector handles this.
    // If messages_ stores {role, content} pairs where content is char*, ensure it's correctly managed.
    // For simplicity, let's assume a helper function to add messages or direct manipulation of messages_
    
    // Current implementation of LlamaInference::chat uses `llama_chat_apply_template`
    // which formats messages_ into `formatted_`. Then `generateWithCallback` is called
    // with this `formatted_` buffer.
    // We need to inject the tool call loop here.

    // Maximum number of tool calls in a single user turn to prevent loops
    const int MAX_TOOL_CALLS = 5; 
    int tool_calls_remaining = MAX_TOOL_CALLS;

    // We need a way to manage the conversation history that includes tool calls and their responses.
    // The existing `messages_` (std::vector<llama_chat_message>) stores {role, content}.
    // We'll add tool requests and tool responses to this history.
    // A "tool" role could represent the tool's output.
    // The LLM's request to call a tool is just an "assistant" message that happens to be JSON.

    // Add current user message to the main history
    // Assuming messages_ are {role, content} pairs.
    // The LlamaInference class seems to manage 'messages_' internally already.
    // The `llama_chat_apply_template` in the original chat likely uses this.
    // We need to make sure user_message is added before the loop starts.
    
    // This part is tricky with the existing `llama_chat_apply_template` and `messages_`.
    // Let's assume `messages_` is the primary store.
    // The original `chat` function structure:
    // 1. Adds user_message to `messages_`.
    // 2. Calls `llama_chat_apply_template` using `model_`, `messages_`, `formatted_.data()`, `formatted_.size()`.
    // 3. Calls `generateWithCallback(std::string(formatted_.data(), len), ...)`
    // We need to replicate this but in a loop.

    // Clear previous output string for streaming
    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Clearing output_string." << std::endl << std::flush;
    output_string.clear();

    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: About to strdup user_message." << std::endl << std::flush;
    char* user_msg_content = strdup(user_message.c_str());
    if (!user_msg_content) {
        if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::chat: strdup failed for user_message!" << std::endl << std::flush;
        else std::cerr << "ERROR LlamaInference::chat: strdup failed for user_message!" << std::endl;
        fprintf(stderr, "Failed to duplicate user message string.\n");
        return "[Error: Memory allocation failed]";
    }
    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: strdup successful, about to add to messages_." << std::endl << std::flush;
    messages_.push_back({"user", user_msg_content});
    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: User message added to history. Message count: " << messages_.size() << std::endl << std::flush;


    std::string current_llm_response_text;

    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Entering main tool call loop." << std::endl << std::flush;
    for (int i = 0; i < MAX_TOOL_CALLS; ++i) {
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Loop iteration " << i << std::endl << std::flush;
        current_llm_response_text.clear();

        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] Getting chat template..." << std::endl << std::flush;
        const char* chat_template_str = llama_model_chat_template(model_, nullptr);
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] Got chat template pointer: " << (void*)chat_template_str << std::endl << std::flush;

        if (!chat_template_str) {
            if (debug_log_file_.is_open()) debug_log_file_ << "WARNING LlamaInference::chat: [Loop " << i << "] Model has no chat template. Using fallback." << std::endl << std::flush;
            fprintf(stderr, "Warning: Model does not have a chat template. Using default.\n");
            chat_template_str = "{{#each messages}}{{@root.bos_token}}{{role}}\n{{content}}{{@root.eos_token}}{{/each}}";
        }

        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] Resizing formatted_ buffer if needed. current size: " << formatted_.size() << ", context_size_: " << context_size_ << std::endl << std::flush;
        if (formatted_.size() < static_cast<size_t>(context_size_)) { // Ensure context_size_ is treated as size_t for comparison
             try {
                formatted_.resize(context_size_);
                if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] formatted_ resized to: " << formatted_.size() << std::endl << std::flush;
             } catch (const std::bad_alloc& e) {
                if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::chat: [Loop " << i << "] std::bad_alloc while resizing formatted_ to " << context_size_ << ". what(): " << e.what() << std::endl << std::flush;
                return "[Error: Memory allocation failed for prompt buffer]";
             }
        }

        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] Applying chat template. messages_.size(): " << messages_.size() << std::endl;
            for(size_t j=0; j < messages_.size(); ++j) {
                 debug_log_file_ << "  Msg[" << j << "] Role: " << messages_[j].role << ", Content (first 50): " << std::string(messages_[j].content).substr(0,50) << "..." << std::endl;
            }
            debug_log_file_ << std::flush;
        }
        int formatted_len = llama_chat_apply_template(
            chat_template_str, 
            messages_.data(), 
            messages_.size(), 
            true, /* add_generation_prompt - true for assistant's turn */
            formatted_.data(), 
            formatted_.size()
        );
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] llama_chat_apply_template returned: " << formatted_len << std::endl << std::flush;

        if (formatted_len < 0) {
            if (debug_log_file_.is_open()) {
                debug_log_file_ << "ERROR LlamaInference::chat: [Loop " << i << "] Failed to apply chat template (returned " << formatted_len << "). Dumping messages:" << std::endl;
                for(const auto& msg : messages_) {
                    debug_log_file_ << "  Role: " << msg.role << ", Content: " << msg.content << std::endl;
                }
                debug_log_file_ << std::flush;
            }
            fprintf(stderr, "Error: Failed to apply chat template (length %d). Dumping messages:\n", formatted_len);
            for(const auto& msg : messages_) {
                fprintf(stderr, "Role: %s, Content: %s\n", msg.role, msg.content);
            }
            // Attempt to recover by removing the last message if it caused the issue.
            if (!messages_.empty()) {
                free(const_cast<char*>(messages_.back().content));
                messages_.pop_back();
            }
            return "[Error: Failed to format prompt for LLM]";
        }
        if (static_cast<size_t>(formatted_len) > formatted_.size()) {
            fprintf(stderr, "Error: Formatted prompt length (%d) exceeds buffer size (%zu).\n", formatted_len, formatted_.size());
            // Try to recover or error out
            if (!messages_.empty()) { // Remove last message, might be too long
                free(const_cast<char*>(messages_.back().content));
                messages_.pop_back();
            }
            return "[Error: Prompt too long for buffer]";
        }
        
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] Constructing prompt_for_llm string. formatted_len: " << formatted_len << std::endl << std::flush;
        std::string prompt_for_llm(formatted_.data(), formatted_len);
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] prev_len_ before update: " << prev_len_ << std::endl << std::flush;
        prev_len_ = formatted_len; 
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: [Loop " << i << "] prev_len_ updated to: " << prev_len_ << std::endl << std::flush;

        // 2. Get response from LLM
        // The generateWithCallback internally handles tokenization, KV cache, and generation.
        // We want to stream ALL output to the UI, including potential tool calls.
        // So, we use the user-provided token_callback directly.
        
        // The user's token_callback likely appends to output_string and calls redraw_ui.
        // We also need the full response for parsing, so generateWithCallback should return it.
        std::function<void(const std::string&)> ui_streaming_callback = 
            [&output_string, &redraw_ui](const std::string& piece) {
            output_string += piece; // Append to the main output string for UI
            redraw_ui();
        };

        // Clear output_string before this specific LLM turn's generation, 
        // as we are building the response for *this turn* into it for streaming.
        // However, `output_string` is the cumulative response for the entire chat() call.
        // The design is a bit tricky here. `response` (global in main.cpp) seems to be the target for `output_string`.
        // Let's assume `output_string` is meant to be the complete response being built up across tool calls for the UI.
        // If the intent is that `output_string` should only contain the *current* turn's streaming output, 
        // that's a larger refactor of how main.cpp uses it.
        // For now, pieces will be appended to `output_string`.
        // We will use `current_llm_response_text` to get the *specific output of this turn* for parsing.
        std::string llm_output_for_this_turn_parsing;
        std::function<void(const std::string&)> combined_callback = 
            [&output_string, &redraw_ui, &llm_output_for_this_turn_parsing](const std::string& piece) {
            output_string += piece; // Stream to main UI output
            llm_output_for_this_turn_parsing += piece; // Accumulate for parsing this turn's output
            redraw_ui();
        };

        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG: Prompt for LLM (length " << prompt_for_llm.length() << "):\n" << prompt_for_llm << "\nEND DEBUG PROMPT" << std::endl;
        } else {
            std::cout << "DEBUG: Prompt for LLM (length " << prompt_for_llm.length() << "):\n" << prompt_for_llm << "\nEND DEBUG PROMPT" << std::endl;
        }

        // This call is for the LLM to decide on a tool or give a final answer
        // generateWithCallback will use combined_callback to stream to UI and collect for parsing.
        // The return value of generateWithCallback is also the full response it generated.
        current_llm_response_text = generateWithCallback(prompt_for_llm, combined_callback);
        // After this, `current_llm_response_text` IS `llm_output_for_this_turn_parsing`. Using return value is cleaner.


        if (current_llm_response_text.empty() && prompt_for_llm.length() > 0) {
             // fprintf(stderr, "Warning: LLM generated an empty response for a non-empty prompt.\n"); // Keep as fprintf for now
             if (debug_log_file_.is_open()) debug_log_file_ << "WARNING: LLM generated an empty response for a non-empty prompt." << std::endl;
             else std::cerr << "WARNING: LLM generated an empty response for a non-empty prompt." << std::endl;
             // This could be an error or a sign the model has nothing more to say.
        }
        if (debug_log_file_.is_open()) {
            debug_log_file_ << "DEBUG: LLM Raw Response:\n" << current_llm_response_text << "\nEND DEBUG LLM RESPONSE" << std::endl << std::flush;
        } else {
            std::cout << "DEBUG: LLM Raw Response:\n" << current_llm_response_text << "\nEND DEBUG LLM RESPONSE" << std::endl;
        }


        // Add LLM's response to history (as 'assistant')
        // This is important so the next turn sees the LLM's thought process / tool request.
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: About to strdup assistant_msg_content." << std::endl << std::flush;
        char* assistant_msg_content = strdup(current_llm_response_text.c_str());
        if (!assistant_msg_content) { 
            if (debug_log_file_.is_open()) debug_log_file_ << "ERROR LlamaInference::chat: strdup failed for assistant_msg_content!" << std::endl << std::flush;
            /* error handling */ return "[Error: Memory alloc for assistant msg]"; 
        }
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: strdup for assistant_msg_content successful. Adding to messages_." << std::endl << std::flush;
        messages_.push_back({"assistant", assistant_msg_content});
        if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Assistant message added to history. Message count: " << messages_.size() << std::endl << std::flush;


        // 3. Check if it's a tool call
        std::string tool_name;
        json tool_params;
        
        // Extract JSON part if <think> block is present
        std::string potential_json_str;
        size_t think_end_pos = current_llm_response_text.rfind("</think>");
        size_t search_start_pos = 0;

        if (think_end_pos != std::string::npos) {
            search_start_pos = think_end_pos + strlen("</think>");
        }

        // Find the first '{' at or after search_start_pos
        size_t json_start_pos = current_llm_response_text.find("{", search_start_pos);

        if (json_start_pos != std::string::npos) {
            // Find the last '}' in the entire string (or at least after json_start_pos).
            // rfind is suitable here as we expect the JSON to be the last significant part of the string.
            size_t json_end_pos = current_llm_response_text.rfind("}");
            
            // Ensure json_end_pos is after json_start_pos to form a valid substring
            if (json_end_pos != std::string::npos && json_end_pos > json_start_pos) {
                potential_json_str = current_llm_response_text.substr(json_start_pos, json_end_pos - json_start_pos + 1);
                // Basic validation: does it start with { and end with }?
                if (!potential_json_str.empty() && potential_json_str.front() == '{' && potential_json_str.back() == '}') {
                    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Extracted potential JSON (revised logic): " << potential_json_str.substr(0,200) << (potential_json_str.length() > 200 ? "..." : "") << std::endl << std::flush;
                } else {
                    if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: Extracted string (revised logic) does not start/end with braces: " << potential_json_str.substr(0,200) << (potential_json_str.length() > 200 ? "..." : "") << std::endl << std::flush;
                    potential_json_str.clear(); // Invalid, clear it
                }
            } else {
                 if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: No matching '}' found after '{', or '}' is before '{' (revised logic)." << std::endl << std::flush;
            }
        } else {
             if (debug_log_file_.is_open()) debug_log_file_ << "DEBUG LlamaInference::chat: No '{' found after think block (or at all if no think block) (revised logic)." << std::endl << std::flush;
        }
        

        if (tryParseToolCall(potential_json_str, tool_name, tool_params)) {
            if (debug_log_file_.is_open()) {
                debug_log_file_ << "DEBUG LlamaInference::chat: Detected tool call from extracted JSON." << std::endl;
                debug_log_file_ << "DEBUG LlamaInference::chat: Tool Name: " << tool_name << std::endl;
                debug_log_file_ << "DEBUG LlamaInference::chat: Tool Params: " << tool_params.dump() << std::endl << std::flush;
            } else {
                std::cout << "DEBUG: Detected tool call: " << tool_name << " with params: " << tool_params.dump() << std::endl;
            }
            tool_calls_remaining--;
            if (tool_calls_remaining < 0) {
                fprintf(stderr, "Error: Maximum tool call limit reached.\n");
                // Add a message to history indicating this error
                const char* err_msg = "[Error: Max tool calls reached]";
                char* err_msg_content = strdup(err_msg);
                if(err_msg_content) messages_.push_back({"assistant", err_msg_content}); // Or a "system" error role
                return err_msg; // Stop further processing
            }

            std::string tool_api_endpoint;
            std::string http_method = "POST"; // Default to POST

            // Map tool_name to microservice endpoint and method
            // This mapping should be robust.
            if (tool_name == "send_email") { // Matches FastAPI in gmail_service.py @app.post("/messages")
                tool_api_endpoint = "/messages";
                http_method = "POST";
            } else if (tool_name == "list_labels") { // @app.get("/labels")
                tool_api_endpoint = "/labels";
                http_method = "GET";
            } else if (tool_name == "get_profile") { // @app.get("/profile")
                tool_api_endpoint = "/profile";
                http_method = "GET";
            } else if (tool_name == "trash_message") { // @app.delete("/messages/{message_id}")
                tool_api_endpoint = "/messages/" + tool_params.value("message_id", "");
                http_method = "DELETE";
                 if (!tool_params.contains("message_id") || !tool_params["message_id"].is_string()) {
                     const char* param_err = "[Error: trash_message tool call missing 'message_id' string parameter]";
                     char* param_err_content = strdup(param_err);
                     if(param_err_content) messages_.push_back({"system", param_err_content});
                     return param_err; 
                 }
                 tool_params.erase("message_id"); // Remove from body if it's in path
            }
            // Add more tools from gmail_service.py:
            // get_label (GET /labels/{label_id})
            // create_label (POST /labels)
            // update_label (PUT /labels/{label_id})
            // delete_label (DELETE /labels/{label_id})
            // list_messages (GET /messages) - note query params here
            // get_history (GET /history)
            else if (tool_name == "list_messages") { // Added this tool
                tool_api_endpoint = "/messages";
                http_method = "GET";
                // Parameters like "query" and "max_results" will be handled by make_tool_request for GET
                // ADDITION: Limit max_results to prevent overly long responses
                if (tool_params.contains("max_results") && tool_params["max_results"].is_number()) {
                    int current_max_results = tool_params["max_results"].get<int>();
                    const int MAX_RESULTS_CAP = 3; // Cap at 3 messages
                    if (current_max_results > MAX_RESULTS_CAP) {
                        tool_params["max_results"] = MAX_RESULTS_CAP;
                        if (debug_log_file_.is_open()) {
                            debug_log_file_ << "INFO: Capping list_messages max_results from " << current_max_results 
                                            << " to " << MAX_RESULTS_CAP << std::endl << std::flush;
                        }
                    }
                } else if (!tool_params.contains("max_results")) { // If not specified by LLM, add a default small one
                     tool_params["max_results"] = 3; // Default cap if LLM doesn't specify
                     if (debug_log_file_.is_open()) {
                        debug_log_file_ << "INFO: list_messages max_results not specified by LLM, setting to 3." << std::endl << std::flush;
                     }
                } // If max_results is present but not a number, it will be passed as is (and likely fail at microservice or GET formatting)
            }
            else {
                std::string unknown_tool_msg = "[Error: Unknown tool name: " + tool_name + "]";
                // fprintf(stderr, "%s\n", unknown_tool_msg.c_str()); // Keep as fprintf for now
                if (debug_log_file_.is_open()) debug_log_file_ << "ERROR: Unknown tool name: " << tool_name << std::endl;
                else std::cerr << "ERROR: Unknown tool name: " << tool_name << std::endl;

                char* unknown_tool_content = strdup(unknown_tool_msg.c_str());
                if(unknown_tool_content) messages_.push_back({"system", unknown_tool_content}); // Or some error role
                // Continue to next LLM iteration, maybe it can recover or user can clarify. Or return error.
                // For now, let's let the LLM try to respond to this error.
                continue; 
            }
            
            std::string tool_response_str = make_tool_request(http_method, tool_api_endpoint, tool_params);
            if (debug_log_file_.is_open()) {
                debug_log_file_ << "DEBUG: Tool Response from microservice:\n" << tool_response_str << "\nEND DEBUG TOOL RESPONSE" << std::endl;
            } else {
                std::cout << "DEBUG: Tool Response from microservice:\n" << tool_response_str << "\nEND DEBUG TOOL RESPONSE" << std::endl;
            }

            // Add tool response to history.
            // Need a role for tool responses. llama.cpp examples sometimes use "tool" or just feed it as "assistant" or "user".
            // Let's use "tool" role for now, assuming the chat template can handle it.
            // If not, we might need to format it as a user or assistant message saying "Tool X returned: ..."
            char* tool_resp_content = strdup(tool_response_str.c_str());
            if (!tool_resp_content) { /* error handling */ return "[Error: Memory alloc for tool response]"; }
            messages_.push_back({"tool", tool_resp_content}); // Using "tool" role

            // Loop back to let LLM process tool response.
        } else {
            // Not a tool call, so this is the final response.
            if (debug_log_file_.is_open()) {
                debug_log_file_ << "DEBUG LlamaInference::chat: LLM response was NOT parsed as a tool call (original or extracted). Treating as final response." << std::endl;
                debug_log_file_ << "DEBUG LlamaInference::chat: String attempted for parsing (first 100): " << potential_json_str.substr(0,100) << "..." << std::endl << std::flush;
            }
            // It has already been streamed to the UI via the `combined_callback`.
            return output_string; // Final response, exit loop.
        }
    }

    // If loop finishes due to MAX_TOOL_CALLS, return the last LLM response or an error.
    // The last LLM response is already in `messages_` as 'assistant'.
    // We should also return an error message indicating loop termination.
    const char* max_calls_msg = "[Error: Exceeded maximum tool iterations. Last response was a tool call.]";
    // Add this error to messages_ so the state reflects it.
    char* max_calls_content = strdup(max_calls_msg);
    if(max_calls_content) messages_.push_back({"system", max_calls_content});

    // The output_string already contains everything streamed, including the last (tool) response.
    // Append the error message to it.
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "INFO: Chat ended. Final output_string (includes error if loop exhausted):\n" << output_string << "\n" << std::string(max_calls_msg) << std::endl;
    } // Also append to output_string for UI
    output_string += "\n" + std::string(max_calls_msg);
    redraw_ui(); // Ensure the final error message is displayed

    return output_string;
}

void LlamaInference::resetChat() {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::resetChat: Method entered." << std::endl << std::flush;
    } else {
        std::cerr << "INFO LlamaInference::resetChat: Method entered (log not open)." << std::endl;
    }
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
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::setContextSize: " << n_ctx << std::endl << std::flush;
    }
    context_size_ = n_ctx;
    // Note: This might require re-initialization if called after initialize()
}

void LlamaInference::setGpuLayers(int ngl) {
    if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::setGpuLayers: " << ngl << std::endl << std::flush;
    }
    n_gpu_layers_ = ngl;
    // Note: This requires re-initialization
}

void LlamaInference::setMaxResponseChars(int max_chars) {
     if (debug_log_file_.is_open()) {
        debug_log_file_ << "DEBUG LlamaInference::setMaxResponseChars: " << max_chars << std::endl << std::flush;
    }
    max_response_chars_ = max_chars > 0 ? max_chars : 1024; // Ensure it's positive
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

std::string LlamaInference::make_tool_request(const std::string& http_method, const std::string& endpoint, const json& params) {
    httplib::Client cli(gmail_microservice_address_.c_str());
    cli.set_connection_timeout(10); // 10 seconds
    cli.set_read_timeout(30);       // 30 seconds

    httplib::Result res;
    std::string params_str = params.empty() ? "" : params.dump();
    std::string request_path = endpoint;

    // Convert http_method to uppercase for reliable comparison
    std::string method_upper = http_method;
    for (char &c : method_upper) {
        c = toupper(c);
    }

    if (method_upper == "POST") {
        if (!params_str.empty()) {
            res = cli.Post(endpoint.c_str(), params_str, "application/json");
        } else { // POST with no body
            res = cli.Post(endpoint.c_str());
        }
    } else if (method_upper == "GET") {
        // For GET, parameters are typically URL-encoded.
        if (!params.empty()) {
            std::string query_string = "?";
            bool first_param = true;
            for (auto& [key, val] : params.items()) {
                if (!first_param) {
                    query_string += "&";
                }
                // Basic URL encoding for key and value might be needed here if they can contain special characters.
                // httplib itself doesn't directly expose a general purpose URL encoder for query params.
                // For simplicity, assuming keys are safe and values are simple strings/numbers.
                // Proper URL encoding: httplib::detail::encode_url can be studied or use a library if complex values are expected.
                query_string += key; // Assuming key is URL-safe
                query_string += "=";
                if (val.is_string()) {
                    query_string += val.get<std::string>(); // TODO: URL encode this value
                } else if (val.is_number()) {
                    query_string += std::to_string(val.get<double>());
                } else if (val.is_boolean()) {
                    query_string += val.get<bool>() ? "true" : "false";
                }
                // Add other type handlers if necessary
                first_param = false;
            }
            request_path += query_string;
            if (debug_log_file_.is_open()) debug_log_file_ << "INFO: Constructed GET request path with query: " << request_path << std::endl;
            else std::cout << "INFO: Constructed GET request path with query: " << request_path << std::endl;
        }
        res = cli.Get(request_path.c_str());
    } else if (method_upper == "DELETE") {
        res = cli.Delete(endpoint.c_str());
    }
    // Add other methods like PUT if needed
    else {
        json error_response;
        error_response["error"] = "Unsupported HTTP method for tool request: " + http_method;
        return error_response.dump();
    }

    if (res) {
        if (res->status >= 200 && res->status < 300) {
            return res->body.empty() ? "{}" : res->body; // Return empty JSON if body is empty
        } else {
            json error_response;
            error_response["error"] = "Tool request failed";
            error_response["status_code"] = res->status;
            error_response["reason"] = res->reason;
            error_response["body"] = res->body;
            // For debugging:
            // std::cerr << "Tool request error: " << error_response.dump(2) << std::endl;
            return error_response.dump();
        }
    } else {
        auto err = res.error();
        json error_response;
        error_response["error"] = "Tool request HTTP library error";
        error_response["httplib_error_code"] = static_cast<int>(err); // httplib::Error is an enum
        error_response["httplib_error_message"] = httplib::to_string(err);
        // For debugging:
        // std::cerr << "Tool request httplib error: " << error_response.dump(2) << std::endl;
        return error_response.dump();
    }
}
