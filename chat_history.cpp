#define _CRT_SECURE_NO_WARNINGS

// chat_history.cpp
#include "chat_history.h"

// Poco headers for JSON
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Timestamp.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>
#include <Poco/UUIDGenerator.h>

// wxWidgets for paths and file system
#include <wx/stdpaths.h>
#include <wx/filename.h>

#include <sstream>
#include <fstream>

// File format version
static const int CONVERSATION_FORMAT_VERSION = 1;

ChatHistory::ChatHistory()
{
}

Poco::JSON::Object::Ptr ChatHistory::CreateMessage(const std::string& role, const std::string& content)
{
    Poco::JSON::Object::Ptr msg = new Poco::JSON::Object;
    msg->set("role", role);
    msg->set("content", content);
    return msg;
}

void ChatHistory::AddUserMessage(const std::string& content)
{
    m_messages.push_back(CreateMessage("user", content));
}

void ChatHistory::AddAssistantMessage(const std::string& content)
{
    m_messages.push_back(CreateMessage("assistant", content));
}

void ChatHistory::AddSystemMessage(const std::string& content)
{
    m_messages.push_back(CreateMessage("system", content));
}

void ChatHistory::Clear()
{
    m_messages.clear();
    m_filePath.clear();
    m_title.clear();
    m_createdAt.clear();
    m_updatedAt.clear();
}

size_t ChatHistory::GetMessageCount() const
{
    return m_messages.size();
}

bool ChatHistory::IsEmpty() const
{
    return m_messages.empty();
}

std::string ChatHistory::BuildChatRequestJson(const std::string& model, bool stream) const
{
    Poco::JSON::Object::Ptr root = new Poco::JSON::Object;
    root->set("model", model);
    root->set("stream", stream);

    Poco::JSON::Array::Ptr messagesArray = new Poco::JSON::Array;
    for (const auto& msg : m_messages) {
        // Skip empty messages (e.g. the assistant placeholder added before streaming)
        std::string content = msg->getValue<std::string>("content");
        if (content.empty()) continue;
        messagesArray->add(msg);
    }
    root->set("messages", messagesArray);

    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(root, oss);
    return oss.str();
}

void ChatHistory::AddAssistantPlaceholder()
{
    AddAssistantMessage("");
}

void ChatHistory::UpdateLastAssistantMessage(const std::string& content)
{
    if (!m_messages.empty() && IsLastMessageRole("assistant")) {
        m_messages.back()->set("content", content);
    }
}

void ChatHistory::RemoveLastAssistantMessage()
{
    if (!m_messages.empty() && IsLastMessageRole("assistant")) {
        m_messages.pop_back();
    }
}

bool ChatHistory::HasAssistantPlaceholder() const
{
    if (!m_messages.empty() && IsLastMessageRole("assistant")) {
        return m_messages.back()->getValue<std::string>("content").empty();
    }
    return false;
}

const std::vector<Poco::JSON::Object::Ptr>& ChatHistory::GetMessages() const
{
    return m_messages;
}

std::string ChatHistory::GetLastUserMessage() const
{
    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it) {
        if ((*it)->getValue<std::string>("role") == "user") {
            return (*it)->getValue<std::string>("content");
        }
    }
    return "";
}

std::string ChatHistory::GetLastAssistantMessage() const
{
    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it) {
        if ((*it)->getValue<std::string>("role") == "assistant") {
            return (*it)->getValue<std::string>("content");
        }
    }
    return "";
}

bool ChatHistory::IsLastMessageRole(const std::string& role) const
{
    if (m_messages.empty()) {
        return false;
    }
    return m_messages.back()->getValue<std::string>("role") == role;
}

// ═══════════════════════════════════════════════════════════════════
//  File Persistence
// ═══════════════════════════════════════════════════════════════════

bool ChatHistory::SaveToFile(const std::string& filePath, const std::string& model)
{
    std::string savePath = filePath.empty() ? m_filePath : filePath;
    if (savePath.empty()) return false;
    if (m_messages.empty()) return false;

    try {
        // Set timestamps
        if (m_createdAt.empty()) {
            m_createdAt = CurrentTimestamp();
        }
        m_updatedAt = CurrentTimestamp();

        // Auto-generate title if empty
        if (m_title.empty()) {
            m_title = GenerateTitle();
        }

        // Build the JSON document
        Poco::JSON::Object::Ptr root = new Poco::JSON::Object;
        root->set("version", CONVERSATION_FORMAT_VERSION);
        root->set("model", model);
        root->set("title", m_title);
        root->set("created_at", m_createdAt);
        root->set("updated_at", m_updatedAt);

        Poco::JSON::Array::Ptr messagesArray = new Poco::JSON::Array;
        for (const auto& msg : m_messages) {
            // Only save messages with content (skip empty placeholders)
            std::string content = msg->getValue<std::string>("content");
            if (!content.empty()) {
                messagesArray->add(msg);
            }
        }
        root->set("messages", messagesArray);

        // Write to file with pretty formatting
        std::ostringstream oss;
        Poco::JSON::Stringifier::stringify(root, oss, 2);

        std::ofstream file(savePath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;

        file << oss.str();
        file.close();

        m_filePath = savePath;
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ChatHistory::LoadFromFile(const std::string& filePath, std::string& outModel)
{
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        file.close();

        if (content.empty()) return false;

        Poco::JSON::Parser parser;
        auto result = parser.parse(content);
        auto root = result.extract<Poco::JSON::Object::Ptr>();

        // Read metadata
        if (root->has("model")) {
            outModel = root->getValue<std::string>("model");
        }
        if (root->has("title")) {
            m_title = root->getValue<std::string>("title");
        }
        if (root->has("created_at")) {
            m_createdAt = root->getValue<std::string>("created_at");
        }
        if (root->has("updated_at")) {
            m_updatedAt = root->getValue<std::string>("updated_at");
        }

        // Read messages
        m_messages.clear();
        if (root->has("messages")) {
            auto messagesArray = root->getArray("messages");
            for (size_t i = 0; i < messagesArray->size(); ++i) {
                auto msgObj = messagesArray->getObject(i);
                std::string role = msgObj->getValue<std::string>("role");
                std::string msgContent = msgObj->getValue<std::string>("content");
                m_messages.push_back(CreateMessage(role, msgContent));
            }
        }

        m_filePath = filePath;
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string ChatHistory::GenerateTitle() const
{
    // Use the first user message, truncated to ~60 chars
    for (const auto& msg : m_messages) {
        if (msg->getValue<std::string>("role") == "user") {
            std::string content = msg->getValue<std::string>("content");
            if (content.empty()) continue;

            // Strip newlines
            for (auto& c : content) {
                if (c == '\n' || c == '\r') c = ' ';
            }

            // Truncate
            if (content.size() > 60) {
                content = content.substr(0, 57) + "...";
            }
            return content;
        }
    }
    return "Untitled conversation";
}

std::string ChatHistory::GetConversationsDir()
{
    wxString userDataDir = wxStandardPaths::Get().GetUserDataDir();
    wxFileName dir(userDataDir + wxFileName::GetPathSeparator() + "conversations"
        + wxFileName::GetPathSeparator());

    if (!dir.DirExists()) {
        dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }

    return dir.GetPath().ToStdString();
}

std::string ChatHistory::GenerateFilePath()
{
    std::string dir = GetConversationsDir();

    // Generate a unique filename using UUID
    std::string uuid = Poco::UUIDGenerator::defaultGenerator().createRandom().toString();

    // Use just the first 8 chars for a shorter filename
    std::string shortId = uuid.substr(0, 8);

    return dir + std::string(1, (char)wxFileName::GetPathSeparator()) + "chat_" + shortId + ".json";
}

std::string ChatHistory::CurrentTimestamp()
{
    Poco::Timestamp now;
    return Poco::DateTimeFormatter::format(now, Poco::DateTimeFormat::ISO8601_FORMAT);
}
