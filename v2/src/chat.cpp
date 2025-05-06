#include "LlamaInference.h"
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " -m model.gguf [-c context_size] [-ngl n_gpu_layers]" << std::endl;
        return 1;
    }
    
    // Parse command line arguments
    std::string model_path;
    int ngl = 99;
    int n_ctx = 2048;
    
    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
                model_path = argv[++i];
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            }
        } catch (std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }
    
    if (model_path.empty()) {
        std::cout << "Model path is required." << std::endl;
        return 1;
    }
    
    // Create and initialize the LlamaInference object
    LlamaInference llama(model_path, ngl, n_ctx);
    if (!llama.initialize()) {
        std::cerr << "Failed to initialize LlamaInference." << std::endl;
        return 1;
    }
    
    std::cout << "Model loaded successfully. Enter your messages (empty line to exit):" << std::endl;
    
    // Main chat loop
    while (true) {
        std::cout << "\033[32m> \033[0m";
        std::string user_input;
        std::getline(std::cin, user_input);
        
        if (user_input.empty()) {
            break;
        }
        
        std::cout << "\033[33m";
        // Enable streaming output by passing true
        std::string response = llama.chat(user_input, true);
        std::cout << "\n\033[0m";
    }
    
    return 0;
}
