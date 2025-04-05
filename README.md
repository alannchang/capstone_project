# ✉️ InboxPilot (WIP)

**InboxPilot** is a local-first, privacy-respecting Gmail management application that combines a C++ TUI frontend, local LLM inference via `llama.cpp`, and reinforcement learning to automate and optimize your inbox workflows — all running fully on your machine.

🚧 **Note: This project is currently a work in progress. Features and components may change significantly as development progresses.**

## 🔍 Overview

InboxPilot aims to intelligently manage your Gmail inbox based on your preferences and behavior. It learns over time using reinforcement learning (RL) and communicates decisions through a local LLM.

**Key features in development:**

- ✅ TUI-based email viewer and command console (FTXUI)
- ✅ Local-only access to Gmail via OAuth2 (MCP server)
- 🧠 RL agent that learns preferred actions (archive, label, delete, etc.)
- 🗣 LLM-generated summaries and explanations (llama.cpp)
- 🔁 Feedback loop: user confirms/overrides → model improves

## 🧱 Architecture

```
┌────────────────────────────┐
│      User (TUI)            │
│   FTXUI-based C++ app      │
└────────────┬───────────────┘
             │
             ▼
┌────────────────────────────┐
│    Core App Controller     │
│     (C++, orchestrates)    │
└────────────┬───────────────┘
             │
  ┌──────────┴─────────────┐
  ▼                        ▼
LLM Engine            RL Agent
(llama.cpp)         (Q-learning/DQN)
(Local inference)      (Learns prefs)
  │                        ▲
  └────┐            ┌──────┘
       ▼            ▼
  ┌────────────────────────────┐
  │    Prompt Builder &        │
  │ Decision Integrator (C++)  │
  └────────┬────────┬──────────┘
           │        │
           ▼        ▼
    LLM Summary   RL Decision
           │        │
           └────────┴────┐
                        ▼
               Final Suggested Action
                        │
                        ▼
         ┌────────────────────────┐
         │  MCP Server (Python)   │
         │  - Gmail API + OAuth2  │
         │  - JSON REST/IPC       │
         └─────────┬──────────────┘
                   │
                   ▼
         Gmail Inbox / Threads / Labels

```

## 🧩 Components

|Component|Description|Language|
|---------|-----------|--------|
|TUI Frontend|Rich terminal UI using FTXUI|C++|
|LLM Engine|LLM-based summarization and natural explanations|C/C++ (llama.cpp)|
|RL Agent|Learns actions over time via reinforcement|C++ (mlpack) or Python|
|MCP Server|Manages Gmail API access and OAuth2|Python|
|IPC Layer|Connects components via REST/gRPC/Unix sockets|C++ ↔ Python|

## 🔄 Learning Loop Example

- New email arrives via MCP server.
- LLM summarizes the message.
- RL agent suggests the best action based on learned behavior.
- LLM builds a human-friendly explanation for the suggestion.
- User confirms or overrides the action; feedback is recorded.
- RL policy updates for better future decisions.

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
