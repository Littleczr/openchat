// message_router.cpp
// Implementation of message routing / target detection.

#include "message_router.h"
#include <algorithm>
#include <cctype>

// ═══════════════════════════════════════════════════════════════════
//  Internal helpers
// ═══════════════════════════════════════════════════════════════════

// Case-insensitive string comparison
static bool IEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Case-insensitive prefix check: does `full` start with `prefix`?
static bool StartsWithI(const std::string& full, const std::string& prefix)
{
    if (prefix.size() > full.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(full[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

// Strip the namespace prefix from a model name.
// "pidrilkin/gemma3_27b_abliterated:Q4_K_M" → "gemma3_27b_abliterated:Q4_K_M"
// "qwen3.5:latest" → "qwen3.5:latest" (unchanged)
static std::string StripNamespace(const std::string& model)
{
    size_t slash = model.rfind('/');
    if (slash != std::string::npos && slash + 1 < model.size())
        return model.substr(slash + 1);
    return model;
}

// Check whether a user-typed address matches a model name.
// Matching is case-insensitive and prefix-based.
// Checks against both the full name and the name with namespace stripped.
// Returns the length of the match (0 = no match).
static size_t MatchLength(const std::string& typed, const std::string& fullModel)
{
    // Try matching against the full model name
    if (StartsWithI(fullModel, typed))
        return typed.size();

    // Try matching against the name after the namespace slash
    std::string stripped = StripNamespace(fullModel);
    if (stripped != fullModel && StartsWithI(stripped, typed))
        return typed.size();

    return 0;
}

// Skip leading whitespace, return the position of the first non-space character.
static size_t SkipSpaces(const std::string& s, size_t pos = 0)
{
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
        ++pos;
    return pos;
}

// Extract a "word" from position `pos` up to the first delimiter.
// Delimiters: whitespace, comma, colon.
// Returns the extracted word and sets `endPos` to one past the last char of the word.
static std::string ExtractWord(const std::string& s, size_t pos, size_t& endPos)
{
    endPos = pos;
    while (endPos < s.size()) {
        char c = s[endPos];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ':')
            break;
        ++endPos;
    }
    return s.substr(pos, endPos - pos);
}

// Skip the delimiter(s) and whitespace after an address word.
// Handles patterns like "qwen, " / "qwen:  " / "qwen " / "@qwen ".
static size_t SkipDelimiter(const std::string& s, size_t pos)
{
    if (pos < s.size() && (s[pos] == ',' || s[pos] == ':'))
        ++pos;
    return SkipSpaces(s, pos);
}

// Check if a word is a group keyword (case-insensitive).
static bool IsGroupKeyword(const std::string& word)
{
    return IEquals(word, "all") ||
           IEquals(word, "everyone") ||
           IEquals(word, "both");
}

// ═══════════════════════════════════════════════════════════════════
//  Main routing function
// ═══════════════════════════════════════════════════════════════════

RouteResult RouteMessage(const std::string& message,
                         const std::vector<std::string>& activeModels)
{
    RouteResult result;
    result.isDirected = false;

    // Need at least 2 models for directed routing to matter
    if (activeModels.size() < 2) {
        result.cleanedBody = message;
        return result;
    }

    size_t pos = SkipSpaces(message);
    if (pos >= message.size()) {
        result.cleanedBody = message;
        return result;
    }

    std::string address;
    size_t wordEnd = 0;
    bool isAtPrefix = false;

    // ── Pattern 1: @name ─────────────────────────────────────────
    if (message[pos] == '@') {
        isAtPrefix = true;
        address = ExtractWord(message, pos + 1, wordEnd);
    }
    // ── Pattern 2: name, ... or name: ... ────────────────────────
    // Only treat as an address if followed by comma or colon.
    // "What are your strengths?" should NOT match even if a model
    // name happens to be a prefix of "What".
    else {
        size_t tentativeEnd = 0;
        std::string tentative = ExtractWord(message, pos, tentativeEnd);

        // Must be followed immediately by ',' or ':' (with optional space before body)
        if (tentativeEnd < message.size() &&
            (message[tentativeEnd] == ',' || message[tentativeEnd] == ':'))
        {
            address = tentative;
            wordEnd = tentativeEnd;
        }
    }

    // No address pattern found
    if (address.empty()) {
        result.cleanedBody = message;
        return result;
    }

    // ── Check for group keywords ─────────────────────────────────
    if (IsGroupKeyword(address)) {
        // Explicit group turn — strip the keyword and return group
        size_t bodyStart = SkipDelimiter(message, wordEnd);
        result.cleanedBody = message.substr(bodyStart);
        result.isDirected = false;
        return result;
    }

    // ── Try to match against active models ───────────────────────
    std::string bestModel;
    size_t bestLen = 0;

    for (const auto& model : activeModels) {
        size_t len = MatchLength(address, model);
        if (len > bestLen) {
            bestLen = len;
            bestModel = model;
        }
    }

    if (bestModel.empty()) {
        // Address didn't match any model — treat as normal group turn.
        // Don't strip anything; the text might just start with a name
        // that isn't a model (e.g., "John, can you help?")
        result.cleanedBody = message;
        return result;
    }

    // ── Match found — directed turn ──────────────────────────────
    size_t bodyStart = SkipDelimiter(message, wordEnd);
    result.targetModel = bestModel;
    result.cleanedBody = message.substr(bodyStart);
    result.isDirected = true;
    return result;
}
