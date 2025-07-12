# 📬 MaiMail

**MaiMail** is a project aiming to create a local-first, privacy-respecting Gmail management application. It currently features a C++ based local LLM inference via `llama.cpp` (with a TUI chat interface) and a Python-based microservice for Gmail API interaction.

🚧 **Note: This project is under active development. The final feature set and architecture may evolve.**

## 🔍 Overview

MaiMail's core is to intelligently manage your Gmail inbox. The current version provides the foundational blocks: local LLM processing with a chat interface, and tool calling using a devoted Gmail microservice.

**Currently implemented or well-developed features:**

- **Local LLM Inference with TUI Chat**: Utilizes `llama.cpp` for on-device language model operations. A terminal-based user interface (TUI) using FTXUI provides a chat interface to interact with the LLM (implemented in `src/main.cpp` and `src/LlamaInference.cpp`).
- **Gmail API Access**: A Python microservice (`gmail-microservice/gmail_service.py`) handles communication with the Gmail API via OAuth2 for email management tasks.
- **C++ and Python Microservice Integration**: Implementing the HTTP client logic within the C++ application to enable tool/function calling to perform Gmail actions.

**Features in planning/early development (may not be fully functional):**
- 🧠 **Pattern Recognition Based Recommendations**: When the LLM notices a pattern in the user's behaviors, the LLM will actively make recommendations to the user.
- 💻 **Integrated Email Management TUI**: Enhancing the current TUI to directly view, manage, and organize emails by calling the Gmail microservice.

<hr>
Demo running on a Dell G16 (Intel i9 13900HX, ram upgraded to 96 GB, no GPU/CUDA being utilized):


https://github.com/user-attachments/assets/a2f77362-5e22-44ec-939d-7c4724c44506



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
git clone https://github.com/alannchang/capstone_project.git
cmake -B build # -DLLAMA_CURL=OFF maybe required if you are getting an error that states that curl cannot be found
cd build && make
```

#### Gmail Microservice (Python):

First time building/running (using uv, not pip):
```bash
cd gmail-microservice
uv venv .venv
source .venv/bin/activate
uv pip install .
fastapi run gmail_service.py
```

After building/running the first time:
```bash
cd gmail-microservice
source .venv/bin/activate
fastapi run gmail_service.py
```

### Running the application

After the previous steps have been completed:

#### C++ LLM Application: 

```bash
cd build
./chat -m path/to/your/gguf/model # -h option for complete list of command line args
```

## 🧠 Future Features (Planned)

- An undo stack for enhanced user control.
- Advanced label suggestions based on email content.

## 🙏 Acknowledgments

- [llama.cpp](https://github.com/ggml-org/llama.cpp)
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- [Google Gmail API](https://developers.google.com/workspace/gmail/api/guides)
