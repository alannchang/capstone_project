#include "llama-cli-wrapper.h"

int main() {
    try {
        // Configure the wrapper with paths
        LlamaWrapper::Config config{
            .binary_path = "./llama-cli",
            .model_path = "../../models/Qwen2.5-1.5B-Instruct.Q6_K.gguf"
        };

        // Create wrapper instance
        LlamaWrapper llama(config);

        // Send a prompt and handle streaming output
        llama.sendPrompt("Tell me a story");
        llama.streamOutput([](const std::string& output) {
            std::cout << output << std::flush;
        });

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
