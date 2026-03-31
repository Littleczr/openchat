#pragma once
// chat_history.h
#ifndef CHAT_HISTORY_H
#define CHAT_HISTORY_H

#include <vector>
#include <string>

// Poco headers
#include <Poco/JSON/Object.h>

// Chat history management class with file persistence
class ChatHistory
{
public:
    ChatHistory();
    ~ChatHistory() = default;

    // Message management
    void AddUserMessage(const std::string& content);
    void AddAssistantMessage(const std::string& content);
    void AddSystemMessage(const std::string& content);

    // History management
    void Clear();
    size_t GetMessageCount() const;
    bool IsEmpty() const;

    // API integration
    std::string BuildChatRequestJson(const std::string& model, bool stream = true) const;

    // Streaming support methods
    void AddAssistantPlaceholder();
    void UpdateLastAssistantMessage(const std::string& content);
    void RemoveLastAssistantMessage();
    bool HasAssistantPlaceholder() const;

    // Access methods
    const std::vector<Poco::JSON::Object::Ptr>& GetMessages() const;

    // Utility methods
    std::string GetLastUserMessage() const;
    std::string GetLastAssistantMessage() const;

    // ── File persistence ─────────────────────────────────────────
    // Save conversation to a JSON file. If filePath is empty, uses m_filePath.
    bool SaveToFile(const std::string& filePath, const std::string& model);
    // Load conversation from a JSON file. Returns the model name stored in the file.
    bool LoadFromFile(const std::string& filePath, std::string& outModel);

    // File path management
    std::string GetFilePath() const { return m_filePath; }
    void SetFilePath(const std::string& path) { m_filePath = path; }
    bool HasFilePath() const { return !m_filePath.empty(); }

    // Conversation metadata
    std::string GetTitle() const { return m_title; }
    void SetTitle(const std::string& title) { m_title = title; }
    std::string GetCreatedAt() const { return m_createdAt; }

    // Generate a title from the first user message
    std::string GenerateTitle() const;

    // Get the default conversations directory (creates it if needed)
    static std::string GetConversationsDir();
    // Generate a unique filename for a new conversation
    static std::string GenerateFilePath();

private:
    std::vector<Poco::JSON::Object::Ptr> m_messages;

    // File persistence state
    std::string m_filePath;     // Current file path (empty = unsaved)
    std::string m_title;        // Conversation title
    std::string m_createdAt;    // ISO timestamp of creation
    std::string m_updatedAt;    // ISO timestamp of last save

    // Helper methods
    Poco::JSON::Object::Ptr CreateMessage(const std::string& role, const std::string& content);
    bool IsLastMessageRole(const std::string& role) const;
    static std::string CurrentTimestamp();
};

#endif // CHAT_HISTORY_H
