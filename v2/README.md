# Gmail Manager v2

A modern C++ application for managing Gmail using LLM inference capabilities.

## Features (Planned)

- Gmail integration through Python API
- LLM-powered email analysis and response generation
- Modern terminal UI using FTXUI
- Thread-safe operations
- Comprehensive logging system
- Configurable LLM inference

## Project Structure

```
v2/
├── include/          # Public headers
├── src/             # Implementation files
├── test/            # Test files
├── docs/            # Documentation
└── scripts/         # Build and utility scripts
```

## Requirements

- C++17 compatible compiler
- CMake 3.14 or higher
- Python 3.x with development headers
- Google Test (for testing)

All other dependencies (pybind11, nlohmann_json, FTXUI, llama.cpp) are automatically fetched and built by CMake.

## Building

```bash
mkdir build && cd build
cmake .. -DLLAMA_CUBLAS=OFF -DLLAMA_METAL=OFF  # Enable CUDA or Metal if needed
make
```

## Optional Build Flags

- `LLAMA_CUBLAS=ON`: Enable CUDA support for llama.cpp
- `LLAMA_METAL=ON`: Enable Metal support for llama.cpp (macOS only)
- `BUILD_TESTS=OFF`: Disable building tests
- `ENABLE_WARNINGS=OFF`: Disable compiler warnings as errors

## Testing

```bash
make test
```

## License

[Your chosen license] 