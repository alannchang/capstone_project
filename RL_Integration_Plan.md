# Reinforcement Learning (RL) Integration Plan for InboxPilot

## 1. Project Goal

To enhance InboxPilot with a Reinforcement Learning agent that learns user preferences for email management (e.g., archiving, deleting, labeling) and provides intelligent suggestions to automate or semi-automate inbox actions.

## 2. Core Concept

The RL agent will observe features of incoming emails and user interactions (actions taken on emails). It will learn a policy to predict the most appropriate action for new emails. This policy will be continuously refined based on user feedback (confirming or overriding suggestions).

## 3. Key Components & Technologies

*   **Programming Languages:**
    *   **C++:** For the core RL agent logic, integration with the main application, and potentially `mlpack` for RL algorithms.
    *   **Python:** The existing Gmail microservice remains as is.
*   **RL Agent:**
    *   **State Representation:** Features extracted from emails (sender, subject, LLM-derived summaries/keywords/categories, historical interactions).
    *   **Action Space:** Discrete actions like Archive, Delete, Mark as Read, Apply Label (specific set), Star, etc.
    *   **Reward Mechanism:** Primarily explicit user feedback (suggestion accepted/rejected/modified).
    *   **Algorithm:** Start with Q-Learning or SARSA due to the discrete action space. Consider Policy Gradient methods or Deep Q-Networks (DQNs) if state space becomes very complex.
    *   **Libraries (C++):**
        *   `mlpack`: Offers various machine learning algorithms, including some RL components (e.g., Q-Learning). This would be the first choice to investigate.
        *   Custom Implementation: For simpler algorithms like basic Q-Learning, direct implementation might be feasible if `mlpack` proves too heavy or lacks specific features needed.
*   **LLM Integration (`LlamaInference` - C++):**
    *   Primary role: Feature engineering for the RL agent's state space (e.g., generating summaries, extracting topics, categorizing emails).
    *   Secondary role (Future): Explaining RL agent's suggestions.
*   **Data Storage (Local):**
    *   **SQLite:** To store:
        *   The RL agent's learned policy (e.g., Q-table, or path to model weights if using a neural network).
        *   Training data: (state, action, reward, next_state) tuples for offline learning or experience replay.
        *   User preferences and interaction history that might not fit directly into the Q-table but informs state representation.
    *   **Technology:** `SQLiteCpp` (a C++ SQLite wrapper) or similar.
*   **TUI (FTXUI - C++):**
    *   Display RL agent's suggestions.
    *   Capture user feedback (confirmation, override, manual action).
*   **Inter-Process Communication (IPC):**
    *   Existing HTTP communication between C++ app and Python Gmail microservice remains for executing email actions.

## 4. Phased Implementation Plan

### Phase 0: Prerequisites & Setup

*   **Objective:** Ensure all necessary tools and understanding are in place.
*   **Tasks:**
    1.  **Research `mlpack` for RL:** Evaluate `mlpack`'s Q-Learning, SARSA, and other relevant RL algorithm implementations. Check its API, ease of integration, and dependency management with CMake.
    2.  **SQLite C++ Wrapper:** Choose and integrate a lightweight SQLite C++ wrapper library (e.g., `SQLiteCpp`) into the CMake build system.
    3.  **Define Initial Data Schema:** Design the initial database schema for storing:
        *   `EmailFeatures`: (email_id, sender, subject_keywords, llm_summary_embedding (optional), other_features)
        *   `UserActions`: (email_id, action_taken, timestamp, suggested_action (if any), feedback_on_suggestion)
        *   `RLPolicy`: (state_hash, action, q_value) for Q-tables, or model save path for NNs.

### Phase 1: Data Collection & Feature Engineering Foundation

*   **Objective:** Start collecting data on user email interactions and build the feature extraction pipeline using the LLM.
*   **Implementation Details:**
    1.  **Logging User Actions:**
        *   In `main.cpp`, when a user performs an action on an email (archive, delete, label), log the email's identifiable features and the action taken to the SQLite database.
        *   Focus on actions performed *after* the C++ app is integrated to call the Gmail microservice for these tasks.
    2.  **LLM-Powered Feature Extraction:**
        *   Modify `LlamaInference` or create a new utility class that uses `LlamaInference` to process email content (e.g., body, subject) and extract:
            *   A concise summary.
            *   Keywords or topics.
            *   A potential category (e.g., "Update," "Promotion," "Personal").
        *   These features should be logged alongside user actions.
    3.  **State Representation Design (Initial):**
        *   Define an initial, simple state representation. This could be a string concatenation or a fixed-size vector of:
            *   Sender's domain.
            *   A few top keywords from subject/body (LLM extracted).
            *   LLM-derived category.
        *   Develop a hashing mechanism for this state representation if using a Q-table.
*   **Technologies:** C++, SQLite, `LlamaInference`.
*   **Verification:** Manually inspect the logged data to ensure features and actions are captured correctly.

### Phase 2: Basic RL Agent (Q-Learning) & Offline Training

*   **Objective:** Implement a basic Q-Learning agent and train it offline using the collected data.
*   **Implementation Details:**
    1.  **Q-Learning Agent Class (`RLAgent.h`, `RLAgent.cpp`):**
        *   **Constructor:** Takes learning rate (alpha), discount factor (gamma), exploration rate (epsilon).
        *   **`choose_action(state)`:** Implement epsilon-greedy strategy (explore or exploit based on Q-values).
        *   **`update_q_value(state, action, reward, next_state)`:** Implements the Q-Learning update rule.
        *   **`load_policy(db_path)` / `save_policy(db_path)`:** Methods to load/save the Q-table from/to the SQLite database.
        *   The Q-table itself can be an `std::map<std::string, std::map<ActionEnum, double>>` where the string is the hashed state.
    2.  **Action Enum:** Define an `enum class EmailAction { ARCHIVE, DELETE, LABEL_X, SPAM, ... };`
    3.  **Offline Training Script/Mode:**
        *   Create a mode in `main.cpp` or a separate utility that reads the logged `UserActions` and `EmailFeatures` from SQLite.
        *   Simulate the email processing sequence: for each logged interaction, derive the state, determine the reward (e.g., +1 for the action taken by user, 0 for others from that state), and update the Q-table.
        *   Iterate multiple epochs over the data.
*   **Technologies:** C++, `mlpack` (if chosen for Q-learning structure, or custom implementation), SQLite.
*   **Verification:**
    *   Monitor Q-value convergence for sample states during offline training.
    *   Inspect the learned Q-table for sensible values (e.g., frequently deleted senders should have high Q-values for the DELETE action in corresponding states).

### Phase 3: Online Learning & TUI Integration for Suggestions

*   **Objective:** Integrate the RL agent into the TUI to provide suggestions and learn online from user feedback.
*   **Implementation Details:**
    1.  **RL Agent in `main.cpp`:**
        *   Instantiate `RLAgent`. Load the pre-trained policy (Q-table) from Phase 2.
    2.  **Suggestion Workflow:**
        *   When a new email is focused/selected in the TUI:
            *   Extract its features using the LLM pipeline (from Phase 1).
            *   Generate the state representation.
            *   Call `rl_agent.choose_action(state)` to get a suggested action.
            *   Display the suggestion in the TUI (e.g., "Suggest: Archive (A) / Delete (D) / Label 'Work' (L) / Override (O)").
    3.  **User Feedback Loop:**
        *   **Accept Suggestion:** User presses a key corresponding to the suggestion.
            *   Reward: +1 (or a high positive value).
            *   Action is performed (via Gmail microservice).
            *   Call `rl_agent.update_q_value(...)`.
        *   **Override Suggestion / Manual Action:** User performs a different action than suggested.
            *   Reward for suggested action: -1 (or a high negative value).
            *   Reward for actual action taken: +1.
            *   Update Q-values for both (the penalized suggestion and the rewarded actual action).
            *   Action is performed.
        *   Log all interactions (suggestion, user choice, reward) to SQLite for analysis and potential future offline retraining.
    4.  **Policy Persistence:** Periodically save the updated Q-table back to SQLite.
*   **Technologies:** C++, FTXUI, `RLAgent`, SQLite.
*   **Verification:**
    *   Observe suggestions in the TUI.
    *   Verify that user feedback correctly updates Q-values (via logging or a debug view).
    *   Confirm the agent starts adapting to user preferences over time.

### Phase 4: Advanced Features & Refinements

*   **Objective:** Improve the RL agent's performance, sophistication, and user experience.
*   **Potential Enhancements:**
    1.  **Sophisticated State Representation:**
        *   Incorporate more nuanced features from the LLM (e.g., embeddings of summaries, more detailed categorizations, sentiment analysis).
        *   Include historical interaction patterns (e.g., "how many times has user X archived emails from sender Y?").
    2.  **Advanced RL Algorithms:** If Q-Learning hits limitations with a complex state space, consider:
        *   Deep Q-Networks (DQNs) using `mlpack` or another C++ deep learning library (e.g., `LibTorch` if brave, or a simpler NN library). This would require significant changes to state representation (numeric vectors) and policy storage.
    3.  **LLM-Powered Explanation of Suggestions:**
        *   Use `LlamaInference` to generate a brief explanation for *why* the RL agent made a particular suggestion (e.g., "Suggesting Archive because you usually archive newsletters from this sender.").
    4.  **Confidence Scores:** Display a confidence score with the suggestion.
    5.  **Contextual Exploration:** More intelligent exploration strategies than simple epsilon-greedy.
    6.  **Batch Updates / Experience Replay:** Improve learning stability by updating the agent with batches of past experiences.
    7.  **Allowing "No Suggestion":** If the agent's confidence is below a threshold, it suggests nothing.
    8.  **User configurable RL parameters:** Allow users to tweak learning rate, exploration, etc., or reset the learned policy.

## 5. Key Challenges & Mitigation

*   **Cold Start:**
    *   **Mitigation:** Start with significant data logging (Phase 1) before agent activation. Potentially allow an "import history" feature if Gmail API allows fetching past actions easily (difficult).
*   **State-Action Space Size:** A large number of labels could make the action space huge for Q-Learning.
    *   **Mitigation:** Start with a limited set of common actions/labels. For "Apply Label," perhaps suggest from the top N most used labels initially.
*   **Feature Engineering:** Identifying the most impactful features.
    *   **Mitigation:** Leverage LLM capabilities. Iteratively experiment and evaluate.
*   **Reward Design:** Ensuring rewards correctly guide the agent.
    *   **Mitigation:** Start simple (explicit feedback). Iteratively refine based on observed agent behavior.
*   **User Acceptance & Trust:** Users might be wary of an AI managing their email.
    *   **Mitigation:** Suggestions-first approach (user always confirms). Clear explanations (Phase 4). Easy override. Option to disable/reset RL agent.

## 6. Metrics for Success

*   **Suggestion Acceptance Rate:** Percentage of RL suggestions accepted by the user.
*   **Time Saved:** (Harder to measure directly) User reports feeling more efficient.
*   **Accuracy of Prediction:** Offline evaluation against a holdout set of user actions.
*   **Qualitative User Feedback:** Is the agent helpful and intuitive?

This detailed plan provides a roadmap. Each phase will require iterative development and testing. The modularity of InboxPilot will be a key asset in integrating this complex but powerful feature. 