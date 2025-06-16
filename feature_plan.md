# Design Plan: Local Gmail Assistant with Qwen3, FAISS, and SQLite

## Overview

This document outlines a design plan for a local Gmail assistant application. The assistant leverages `llama.cpp` to run the Qwen3 model, uses FAISS for fast vector search, and employs SQLite for persistent, structured storage of user interactions. The assistant responds to natural language prompts and performs tool-based actions on the user's Gmail account.

## Goals

- Enable natural language interaction with Gmail.
- Perform Gmail API actions (e.g., read, reply, archive) via tool calls.
- Learn from and adapt to user behavior over time.
- Stay fully local and private.

## Core Components

### 1. Qwen3 via llama.cpp

- LLM backend used for natural language understanding and tool call generation.
- Receives structured prompts including current user input and past context.
- Returns JSON-formatted tool calls or recommendations.

### 2. FAISS (C++ API)

- Vector search engine to retrieve semantically relevant past actions and email content.
- Indexes embedded representations of user prompts, summaries of emails, and tool call history.
- Used for contextual retrieval to inform Qwen3 prompt generation.

### 3. SQLite

- Local database for persistent logging of:
  - User prompts
  - Tool calls
  - Gmail responses
  - Summary records for FAISS
- Enables time-based or filterable queries (e.g., last 5 promotional emails).
- Links to FAISS entries via vector ID or hash.

## Sample Flow: How the Program Works

### Step 1: User Input

User types a prompt, e.g.:

```
"Get my last unread email."
```

### Step 2: Contextual Retrieval

- The app embeds the prompt (using a local embedding model).
- Searches the FAISS index for semantically similar past interactions.
- Retrieves metadata from SQLite for those entries.

### Step 3: Prompt Construction for Qwen3

The application builds a prompt for the LLM that includes:
- The user query
- Relevant history (from SQLite)
- Retrieved memory snippets (from FAISS)

Example prompt:

```
User previously marked promotional emails as read on:
- June 2
- May 27

Current prompt:
"Get my last unread email."
```

### Step 4: Qwen3 LLM Inference

Qwen3 (via llama.cpp) is called with the prompt.  
It responds with a tool call:

```json
{
  "tool": "getUnreadEmail",
  "params": {
    "label": "INBOX",
    "maxResults": 1
  }
}
```

### Step 5: Gmail Tool Execution

- The app sends the API request to Gmail.
- Retrieves the unread email and displays it to the user.

### Step 6: Logging and Memory Update

- SQLite logs the entire interaction:
  - Prompt
  - Tool call
  - Gmail response
- The response summary is embedded and added to FAISS.
- Mapping of FAISS vector ID to SQLite record ID is maintained.

---

## SQLite Schema (Draft)

### Table: `tool_calls`

| Column      | Type        | Notes                          |
|-------------|-------------|--------------------------------|
| id          | INTEGER     | Primary key                    |
| timestamp   | TEXT        | ISO 8601 datetime              |
| prompt      | TEXT        | User natural language input    |
| tool_name   | TEXT        | e.g., `getUnreadEmail`         |
| tool_params | TEXT (JSON) | Structured tool parameters     |
| response    | TEXT (JSON) | Gmail API response             |

### Table: `embeddings`

| Column      | Type    | Notes                              |
|-------------|---------|------------------------------------|
| id          | INTEGER | Matches tool_call or summary id    |
| vector_id   | INTEGER | Corresponds to FAISS index         |
| summary     | TEXT    | Text used to generate embedding    |
| embedding   | BLOB    | Raw vector (optional, for debugging) |

---

## Future Enhancements

- Add support for feedback on tool call success.
- Implement suggestion mode (e.g., reply recommendations).
- Add summarization for long threads.
- Periodic re-indexing of FAISS for freshness.
- Optional export of SQLite logs for training datasets.

## Conclusion

This design leverages foundational models and local vector search to build a personalized, privacy-conscious Gmail assistant. It avoids the complexity of reinforcement learning by using LLM reasoning and pattern recognition augmented by local memory.

With C++ as the primary language, and first-class support from FAISS and SQLite, the implementation remains efficient, portable, and fully local.
