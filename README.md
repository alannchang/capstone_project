# 📬 MaiMail

**MaiMail** is a project aiming to create a local-first, privacy-respecting Gmail management application. It currently features a C++ based local LLM inference via `llama.cpp` (with a TUI chat interface) and a Python-based microservice for Gmail API interaction.

🚧 **Note: This project is under active development. The final feature set and architecture may evolve.**

## 🔍 Overview

MaiMail's core is to intelligently manage your Gmail inbox. The current version provides the foundational blocks: local LLM processing with a chat interface, and Gmail connectivity via a separate microservice.

**Currently implemented or well-developed features:**

- **Local LLM Inference with TUI Chat**: Utilizes `llama.cpp` for on-device language model operations. A terminal-based user interface (TUI) using FTXUI provides a chat interface to interact with the LLM (implemented in `src/main.cpp` and `src/LlamaInference.cpp`).
- **Gmail API Access**: A Python microservice (`gmail-microservice/gmail_service.py`) handles communication with the Gmail API via OAuth2 for email management tasks. This service is functional but not yet fully integrated with the C++ TUI application.

**Features in planning/early development (may not be fully functional):**

- 💻 **Integrated Email Management TUI**: Enhancing the current TUI to directly view, manage, and organize emails by calling the Gmail microservice.
- 🔗 **C++ and Python Microservice Integration**: Implementing the HTTP client logic within the C++ application to communicate with the Python Gmail microservice, enabling the TUI to perform Gmail actions.
- 🗣 **LLM-generated summaries and explanations** integrated with email workflows (beyond the current chat functionality).

<hr>
Demo running on a Dell G16 (Intel i9 13900HX, ram upgraded to 96 GB, no GPU/CUDA being utilized):

https://github.com/user-attachments/assets/a5b28531-d85e-4969-9128-7df683720c61

## 🧱 Architecture (Current High-Level)

```
        ┌────────────────────────────┐
        │      User (TUI)            │
        │   (C++ app, FTXUI)         │
        └────────────┬───────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │    C++ Application Logic   │
        │     (src/main.cpp -        │
        │      FTXUI Chat TUI)       │
        └────────────┬───────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │         LLM Engine         │
        │  (src/LlamaInference.cpp)  │
        │      (llama.cpp based)     │
        └────────────────────────────┘
                     │ (Communication Method TBD:
                     │  C++ HTTP Client to Python Service)
                     ▼
        ┌────────────────────────────┐
        │  Gmail Microservice (Python)│
        │  - Gmail API + OAuth2      │
        │  (gmail-microservice/)     │
        └─────────┬──────────────┘
                  │
                  ▼
        Gmail Inbox / Threads / Labels
```

## 🧩 Components (Current & Planned)

|Component|Description|Language|Status|
|---------|-----------|--------|------|
|C++ Core Logic & TUI|Main application logic, TUI chat interface, orchestrates LLM.|C++|Partially Implemented (in `src/main.cpp`)| 
|LLM Engine|LLM-based chat and text generation|C/C++ (llama.cpp)|Implemented (`src/LlamaInference.cpp`)| 
|Gmail Microservice|Manages Gmail API access and OAuth2 via FastAPI|Python|Implemented (`gmail-microservice/gmail_service.py`)| 
|IPC Layer (C++ to Python)|HTTP Client in C++ to call Python service endpoints|C++ ↔ Python|Planned/To Be Implemented|
|TUI Email Frontend|Rich terminal UI using FTXUI for full email management|C++|Planned (enhancement of current TUI)|

## 🛠️ Setup

Note: this application only works with Gmail accounts.

### Gmail API Setup
- Create a new [Google Cloud project](https://console.cloud.google.com/projectcreate)
- Enable the [Gmail API](https://console.cloud.google.com/workspace-api/products)
- Configure an [OAuth consent screen](https://console.cloud.google.com/apis/credentials/consent)
    - Select "external" and add your gmail address as a "Test User" to allow API access.
- Add the following OAuth scopes
```
https://www.googleapis.com/auth/gmail/send
https://www.googleapis.com/auth/gmail/modify
https://www.googleapis.com/auth/gmail/Labels
https://www.googleapis.com/auth/gmail/readonly
```
- Create an [OAuth Client ID](https://console.cloud.google.com/apis/credentials/oauthclient) for application type "Desktop App"
- Download the JSON file of your client's OAuth keys, rename it to "credentials.json", and move it to the project directory.

### Obtaining a model

- Models can be obtained [here](https://huggingface.co/models?library=gguf&sort=trending).
- Ensure the model is in GGUF format compatible with `llama.cpp`.
- For more information on `llama.cpp` compatible models, please refer to [llama.cpp](https://github.com/ggml-org/llama.cpp).

### Building with Cmake
```bash
# Ensure you have CMake and a C++ compiler installed
# Clone the repository with submodules (if any are used, e.g., llama.cpp)
git clone --recurse-submodules https://github.com/alannchang/capstone_project.git
cmake -B build # -DLLAMA_CURL=OFF maybe required if you are getting an error that states that curl cannot be found
cd build && make
```

### Running the application

After the previous steps have been completed:

#### C++ LLM Application (Example - if it's a standalone chat): 

```bash
# Navigate to the build directory
cd build
# Run the executable (actual name might vary based on CMakeLists.txt)
# This example assumes a 'chat' executable and a model path argument
./chat -m path/to/your/gguf/model
```

_Specific instructions for running `main.cpp` depend on its current implementation (e.g., if it requires arguments, provides a TUI, etc.). The C++ application currently provides a chat interface with the LLM. Full email management capabilities via the TUI are planned and will require integration with the Gmail microservice._

#### Gmail Microservice (Python):

First time building/running (using uv, not pip):
```bash
cd gmail-microservice
# create virtual environment
uv venv .venv
source .venv/bin/activate
# install dependencies
uv pip install .
# Run the service (actual command might vary)
fastapi run gmail_service.py
```

After building/running the first time:
```bash
cd gmail-microservice
# use existing virtual environment
source .venv/bin/activate
fastapi run gmail_service.py
```

_Note: The C++ application (TUI chat) and Python microservice are currently separate components. Their integration (C++ app calling the Python service) is planned but not yet fully implemented._

## 🧠 Future Features (Planned)

- An undo stack for enhanced user control.
- Advanced label suggestions based on email content.
- Action preview mode before executing.

## 🛠 Development Status

This is a work in progress.  Expect changes and improvements over time.

## 🙏 Acknowledgments

- [llama.cpp](https://github.com/ggml-org/llama.cpp)
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- [Google Gmail API](https://developers.google.com/workspace/gmail/api/guides)
