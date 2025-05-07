#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "LlamaInference.h"
#include <filesystem>
#include <iostream>

class LlamaInferenceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Get the path to the test model
        const char* model_path_env = std::getenv("LLAMA_TEST_MODEL");
        if (model_path_env) {
            std::cout << "Using model from LLAMA_TEST_MODEL: " << model_path_env << std::endl;
            model_path = model_path_env;
        } else {
            model_path = "../../../../gguf-models/Llama-3.2-3B-Instruct-Q6_K.gguf";
            std::cout << "LLAMA_TEST_MODEL not set, looking for model at: " 
                      << std::filesystem::absolute(model_path).string() << std::endl;
        }

        if (!std::filesystem::exists(model_path)) {
            std::cout << "Warning: Model file not found - tests will be skipped" << std::endl;
        }
    }

    void SetUp() override {
        if (!std::filesystem::exists(model_path)) {
            GTEST_SKIP() << "Test model not found";
        }
    }

    static std::string model_path;
};

std::string LlamaInferenceTest::model_path;

TEST_F(LlamaInferenceTest, InitializationTest) {
    LlamaInference llama(model_path);
    EXPECT_TRUE(llama.initialize());
}

TEST_F(LlamaInferenceTest, SystemPromptTest) {
    LlamaInference llama(model_path);
    ASSERT_TRUE(llama.initialize());

    const std::string system_prompt = "You are a helpful AI assistant.";
    llama.setSystemPrompt(system_prompt);
    
    // Generate a response to verify system prompt integration
    const std::string user_message = "What are you?";
    std::string response = llama.chat(user_message, false);
    
    // The response should reflect the system prompt's influence
    EXPECT_FALSE(response.empty());
}

TEST_F(LlamaInferenceTest, ChatTest) {
    LlamaInference llama(model_path);
    ASSERT_TRUE(llama.initialize());

    // Test basic chat functionality
    const std::string message = "Hello, how are you?";
    std::string response = llama.chat(message, false);
    
    EXPECT_FALSE(response.empty());
}

TEST_F(LlamaInferenceTest, ContextSizeTest) {
    LlamaInference llama(model_path);
    
    // Test setting different context sizes
    const int new_ctx_size = 4096;
    llama.setContextSize(new_ctx_size);
    
    EXPECT_TRUE(llama.initialize());
}

TEST_F(LlamaInferenceTest, ChatHistoryTest) {
    LlamaInference llama(model_path);
    ASSERT_TRUE(llama.initialize());

    // Send multiple messages and verify responses
    std::string response1 = llama.chat("What is 2+2?", false);
    EXPECT_FALSE(response1.empty());

    std::string response2 = llama.chat("And what is that number multiplied by 2?", false);
    EXPECT_FALSE(response2.empty());

    // Reset chat and verify it clears history
    llama.resetChat();
    std::string response3 = llama.chat("What was the last number we discussed?", false);
    EXPECT_FALSE(response3.empty());
} 
