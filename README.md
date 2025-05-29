# 📬 InboxPilot (Name subject to change)

**InboxPilot** is a project aiming to create a local-first, privacy-respecting Gmail management application. It currently features C++ based local LLM inference via `llama.cpp` and a Python-based microservice for Gmail API interaction.

🚧 **Note: This project is under active development. The final feature set and architecture may evolve. Some features described in older documentation (like a fully integrated TUI and Reinforcement Learning agent) are currently either in early stages or planned for future development.**

## 🔍 Overview

InboxPilot's core is to intelligently manage your Gmail inbox. The current version provides the foundational blocks: local LLM processing and Gmail connectivity.

**Currently implemented or well-developed features:**

- **Local LLM Inference**: Utilizes `llama.cpp` for on-device language model operations (implemented in `src/LlamaInference.cpp`).
- **Gmail API Access**: A Python microservice (`gmail-microservice/gmail_service.py`) handles communication with the Gmail API via OAuth2 for email management tasks.

**Features in planning/early development (may not be fully functional):**

- 💻 TUI-based email viewer and command console (FTXUI is a planned dependency).
- 🧠 RL agent that learns preferred actions (archive, label, delete, etc.).
- 🗣 LLM-generated summaries and explanations integrated with email workflows.
- 🔁 Feedback loop: user confirms/overrides → model improves.

## 🧱 Architecture (Current High-Level)

```
        ┌────────────────────────────┐
        │      User (Potential TUI)  │
        │   (C++ app, FTXUI planned) │
        └────────────┬───────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │    C++ Application Logic   │
        │     (src/main.cpp)         │
        └────────────┬───────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │         LLM Engine         │
        │  (src/LlamaInference.cpp)  │
        │      (llama.cpp based)     │
        └────────────────────────────┘
                     │ (Communication Method TBD)
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

*The Reinforcement Learning (RL) agent and a more complex Core App Controller are envisioned but not yet distinctly implemented modules.*

## 🧩 Components (Current & Planned)

|Component|Description|Language|Status|
|---------|-----------|--------|------|
|C++ Core Logic|Main application logic, orchestrates LLM.|C++|Partially Implemented (in `src/main.cpp`)| 
|LLM Engine|LLM-based summarization and natural explanations|C/C++ (llama.cpp)|Implemented (`src/LlamaInference.cpp`)| 
|Gmail Microservice|Manages Gmail API access and OAuth2|Python|Implemented (`gmail-microservice/gmail_service.py`)| 
|TUI Frontend|Rich terminal UI using FTXUI|C++|Planned/Early Development|
|RL Agent|Learns actions over time via reinforcement|C++ (mlpack) or Python|Planned|
|IPC Layer|Connects C++ components and Python service|C++ ↔ Python|To Be Defined/Implemented|

## 🔄 Learning Loop Example (Envisioned)

- New email arrives via Gmail API.
- LLM summarizes the message.
- RL agent suggests the best action based on learned behavior.
- LLM builds a human-friendly explanation for the suggestion.
- User confirms or overrides the action; feedback is recorded.
- RL policy updates for better future decisions.

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
cmake -B build
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

_Specific instructions for running `main.cpp` depend on its current implementation (e.g., if it requires arguments, provides a TUI, etc.)._

#### Gmail Microservice (Python):

```bash
# Navigate to the microservice directory
cd gmail-microservice
# It's recommended to use a virtual environment
python -m venv .venv
source .venv/bin/activate
# Install dependencies (assuming a requirements.txt or pyproject.toml based setup)
# If pyproject.toml uses Poetry or similar, consult its documentation for install commands.
# For a basic setup, ensure google-api-python-client and google-auth-oauthlib are installed.
# Example using pip (if a requirements.txt exists or you install manually):
# pip install -r requirements.txt 
# or pip install google-api-python-client google-auth-oauthlib

# Run the service (actual command might vary)
python gmail_service.py
```
_Note: The C++ application and Python microservice are currently separate components. Their integration method and combined execution flow are yet to be fully defined and implemented._

## 🧠 Future Features (Planned)

- Offline training mode for the RL agent.
- An undo stack for enhanced user control.
- Advanced label suggestions based on email content.
- Action preview mode before executing.
- User-specific profiles and customization.
- Plugin-like API for extending LLM capabilities.

## 🛠 Development Status

This is a very early-stage project.
Please note that this project is evolving — expect changes and improvements over time.

## 🙏 Acknowledgments

- [llama.cpp](https://github.com/ggml-org/llama.cpp)
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- [Google Gmail API](https://developers.google.com/workspace/gmail/api/guides)
