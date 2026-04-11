// chat_display.cpp
#include "chat_display.h"
#include "markdown_renderer.h"
#include "theme.h"
#include <wx/clipbrd.h>

ChatDisplay::ChatDisplay(wxRichTextCtrl* displayCtrl)
    : m_displayCtrl(displayCtrl)
    , m_markdownRenderer(new MarkdownRenderer(displayCtrl))
    , m_userColor(108, 180, 238)       // Soft blue (#6CB4EE)
    , m_assistantColor(125, 212, 160)  // Mint green (#7DD4A0)
    , m_systemColor(136, 136, 136)     // Medium gray (#888888)
    , m_thoughtColor(154, 154, 154)    // Light gray (#9A9A9A)
    , m_isInThoughtBlock(false)
    , m_isFirstAssistantDelta(true)
    , m_hasRenderedAssistantContent(false)
    , m_activeAssistantColor(125, 212, 160)
{
    // Configure markdown renderer colors to match theme
    m_markdownRenderer->SetCodeColor(wxColour(232, 184, 77));      // Warm amber (#E8B84D)
    m_markdownRenderer->SetHeadingColor(wxColour(232, 232, 232));  // Near-white (#E8E8E8)

    // ── Code block copy: click handler ────────────────────────────
    m_displayCtrl->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        event.Skip();  // Let normal text selection proceed

        long pos = 0;
        auto hit = m_displayCtrl->HitTest(event.GetPosition(), &pos);
        if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
            int blockIdx = m_markdownRenderer->HitTestCopyLink(pos);
            if (blockIdx >= 0) {
                const std::string& code = m_markdownRenderer->GetCodeBlock(static_cast<size_t>(blockIdx));
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(code)));
                    wxTheClipboard->Close();
                }
            }
        }
    });

    // ── Code block copy: hand cursor on hover ─────────────────────
    m_displayCtrl->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        long pos = 0;
        auto hit = m_displayCtrl->HitTest(event.GetPosition(), &pos);
        bool overLink = false;
        if (hit == wxTE_HT_ON_TEXT || hit == wxTE_HT_BEFORE) {
            overLink = (m_markdownRenderer->HitTestCopyLink(pos) >= 0);
        }
        if (overLink) {
            m_displayCtrl->SetCursor(wxCursor(wxCURSOR_HAND));
            // Don't Skip — prevents wxRichTextCtrl from resetting cursor
        } else {
            m_displayCtrl->SetCursor(wxCursor(wxCURSOR_IBEAM));
            event.Skip();
        }
    });
}

ChatDisplay::~ChatDisplay()
{
    delete m_markdownRenderer;
}

void ChatDisplay::DisplayUserMessage(const std::string& text,
                                     const std::string& target)
{
    SetInsertionPointToEnd();

    wxRichTextAttr prefixAttr;
    prefixAttr.SetTextColour(m_userColor);
    prefixAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
    m_displayCtrl->BeginStyle(prefixAttr);

    if (!target.empty()) {
        // Shorten the target name for display:
        // "pidrilkin/gemma3:Q4_K_M" → "gemma3:Q4_K_M"
        std::string shortTarget = target;
        size_t slash = shortTarget.rfind('/');
        if (slash != std::string::npos && slash + 1 < shortTarget.size())
            shortTarget = shortTarget.substr(slash + 1);

        m_displayCtrl->WriteText(wxString::FromUTF8(
            "You \xe2\x86\x92 " + shortTarget + ": "));  // → arrow
    }
    else {
        m_displayCtrl->WriteText("You: ");
    }
    m_displayCtrl->EndStyle();

    wxRichTextAttr textAttr;
    textAttr.SetTextColour(m_userColor);
    textAttr.SetFontWeight(wxFONTWEIGHT_NORMAL);
    m_displayCtrl->BeginStyle(textAttr);
    m_displayCtrl->WriteText(wxString::FromUTF8(text + "\n\n"));
    m_displayCtrl->EndStyle();

    EnsureVisibleAtEnd();
}

void ChatDisplay::DisplaySystemMessage(const std::string& text)
{
    SetInsertionPointToEnd();

    wxRichTextAttr attr;
    attr.SetTextColour(m_systemColor);
    attr.SetFontStyle(wxFONTSTYLE_ITALIC);
    m_displayCtrl->BeginStyle(attr);
    m_displayCtrl->WriteText(wxString::FromUTF8(text + "\n\n"));
    m_displayCtrl->EndStyle();

    EnsureVisibleAtEnd();
}

void ChatDisplay::DisplayAssistantPrefix(const std::string& modelName)
{
    DisplayAssistantPrefix(modelName, m_assistantColor);
}

void ChatDisplay::DisplayAssistantPrefix(const std::string& modelName, const wxColour& accentColor)
{
    SetInsertionPointToEnd();

    // Reset state for the new message
    m_isInThoughtBlock = false;
    m_isFirstAssistantDelta = true;
    m_hasRenderedAssistantContent = false;
    m_activeAssistantColor = accentColor;
    m_markdownRenderer->Reset();

    wxFont baseFont = m_displayCtrl->GetFont();
    int baseSize = baseFont.GetPointSize();
    if (baseSize <= 0) baseSize = 14;

    wxRichTextAttr prefixAttr;
    prefixAttr.SetTextColour(accentColor);
    prefixAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
    prefixAttr.SetFontStyle(wxFONTSTYLE_NORMAL);
    prefixAttr.SetFontSize(baseSize);

    if (!baseFont.GetFaceName().empty()) {
        prefixAttr.SetFontFaceName(baseFont.GetFaceName());
    }

    m_displayCtrl->BeginStyle(prefixAttr);
    m_displayCtrl->WriteText(wxString::FromUTF8(modelName + ": "));
    m_displayCtrl->EndStyle();
}

void ChatDisplay::DisplayAssistantDelta(const std::string& delta)
{
    SetInsertionPointToEnd();
    std::string remainingDelta = delta;

    const auto trimLeadingWhitespace = [](std::string& text)
        {
            size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                text.clear();
            }
            else if (first > 0) {
                text.erase(0, first);
            }
        };

    const auto hasVisibleChars = [](const std::string& text) -> bool
        {
            return text.find_first_not_of(" \t\r\n") != std::string::npos;
        };

    // Markers for different reasoning models
    const std::string thought_start_marker = "<think>";
    const std::string thought_end_marker = "</think>";

    // On the first delta, check if this is a "thinking" model response
    if (m_isFirstAssistantDelta) {
        m_isFirstAssistantDelta = false;
        if (remainingDelta.rfind(thought_start_marker, 0) == 0) {
            m_isInThoughtBlock = true;
            remainingDelta.erase(0, thought_start_marker.length());
        }
    }

    if (m_isInThoughtBlock) {
        size_t end_pos = remainingDelta.find(thought_end_marker);
        if (end_pos != std::string::npos) {
            // End marker found. Part is thought, rest is answer.
            std::string thought_part = remainingDelta.substr(0, end_pos);
            std::string answer_part = remainingDelta.substr(end_pos + thought_end_marker.length());

            // If nothing visible has been rendered yet, strip leading blank lines/spaces.
            if (!m_hasRenderedAssistantContent) {
                trimLeadingWhitespace(thought_part);
            }

            // Only render thought text if it actually contains visible characters.
            if (hasVisibleChars(thought_part)) {
                AppendFormattedText(thought_part, m_thoughtColor);
                m_hasRenderedAssistantContent = true;
            }

            m_isInThoughtBlock = false;

            // Trim leading blank space before the first visible answer text.
            if (!m_hasRenderedAssistantContent) {
                trimLeadingWhitespace(answer_part);
            }

            if (!answer_part.empty()) {
                m_markdownRenderer->ProcessDelta(answer_part, m_activeAssistantColor);
                if (hasVisibleChars(answer_part)) {
                    m_hasRenderedAssistantContent = true;
                }
            }
        }
        else {
            // Still in thought block, no end marker in this delta
            if (!m_hasRenderedAssistantContent) {
                trimLeadingWhitespace(remainingDelta);
            }

            if (hasVisibleChars(remainingDelta)) {
                AppendFormattedText(remainingDelta, m_thoughtColor);
                m_hasRenderedAssistantContent = true;
            }
        }
    }
    else {
        // Normal answer text — trim leading blank space only at the very start
        if (!m_hasRenderedAssistantContent) {
            trimLeadingWhitespace(remainingDelta);
        }

        if (!remainingDelta.empty()) {
            m_markdownRenderer->ProcessDelta(remainingDelta, m_activeAssistantColor);
            if (hasVisibleChars(remainingDelta)) {
                m_hasRenderedAssistantContent = true;
            }
        }
    }

    EnsureVisibleAtEnd();
}

void ChatDisplay::DisplayAssistantComplete()
{
    // Flush any remaining buffered text in the markdown renderer
    if (m_isInThoughtBlock) {
        // Message ended while still in thought block (unusual but handle it)
        AppendFormattedText("\n\n", m_thoughtColor);
    }
    else {
        m_markdownRenderer->Flush(m_activeAssistantColor);
        AppendFormattedText("\n\n", m_activeAssistantColor);
    }

    m_isInThoughtBlock = false;
    m_isFirstAssistantDelta = true;
    m_hasRenderedAssistantContent = false;
}

void ChatDisplay::DisplayAssistantMessage(const std::string& modelName,
    const std::string& content,
    const wxColour& accentColor)
{
    DisplayAssistantPrefix(modelName, accentColor);

    m_markdownRenderer->Reset();

    if (!content.empty()) {
        // Strip leading whitespace/newlines so the first paragraph renders
        // flush with the prefix — matches the trim that DisplayAssistantDelta
        // performs on the streaming path.
        std::string trimmed = content;
        size_t first = trimmed.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            trimmed.clear();
        } else if (first > 0) {
            trimmed.erase(0, first);
        }

        // Render the whole saved/final message in one shot,
        // not through the streaming path.
        if (!trimmed.empty()) {
            m_markdownRenderer->ProcessDelta(trimmed, accentColor);
            m_markdownRenderer->Flush(accentColor);
        }
    }

    AppendFormattedText("\n\n", accentColor);

    m_isInThoughtBlock = false;
    m_isFirstAssistantDelta = true;
    m_hasRenderedAssistantContent = false;

    EnsureVisibleAtEnd();
}

void ChatDisplay::Clear()
{
    if (m_displayCtrl) {
        m_displayCtrl->Clear();
    }
    if (m_markdownRenderer) {
        m_markdownRenderer->Reset();
        m_markdownRenderer->ClearCodeBlocks();
    }
}

void ChatDisplay::ScrollToBottom()
{
    EnsureVisibleAtEnd();
}

void ChatDisplay::SetUserColor(const wxColour& color)
{
    m_userColor = color;
}

void ChatDisplay::SetAssistantColor(const wxColour& color)
{
    m_assistantColor = color;
}

void ChatDisplay::SetSystemColor(const wxColour& color)
{
    m_systemColor = color;
}

void ChatDisplay::SetThoughtColor(const wxColour& color)
{
    m_thoughtColor = color;
}

void ChatDisplay::SetFont(const wxFont& font)
{
    if (m_displayCtrl) {
        m_displayCtrl->SetFont(font);
    }
}

void ChatDisplay::ApplyTheme(const ThemeData& theme)
{
    m_userColor = theme.chatUser;
    m_assistantColor = theme.chatAssistant;
    m_systemColor = theme.chatSystem;
    m_thoughtColor = theme.chatThought;

    if (m_markdownRenderer) {
        m_markdownRenderer->SetCodeColor(theme.mdCode);
        m_markdownRenderer->SetHeadingColor(theme.mdHeading);
        m_markdownRenderer->SetCodeLabelColor(theme.mdCodeLabel);
        m_markdownRenderer->SetHorizontalRuleColor(theme.mdHorizontalRule);
    }
}

// Private helper methods

void ChatDisplay::AppendFormattedText(const std::string& text, const wxColour& color,
    bool bold, bool italic)
{
    if (text.empty()) return;

    SetInsertionPointToEnd();

    wxRichTextAttr attr;
    attr.SetTextColour(color);
    if (bold) attr.SetFontWeight(wxFONTWEIGHT_BOLD);
    if (italic) attr.SetFontStyle(wxFONTSTYLE_ITALIC);
    if (!bold) attr.SetFontWeight(wxFONTWEIGHT_NORMAL);

    m_displayCtrl->BeginStyle(attr);
    m_displayCtrl->WriteText(wxString::FromUTF8(text));
    m_displayCtrl->EndStyle();
}

void ChatDisplay::SetInsertionPointToEnd()
{
    if (m_displayCtrl) {
        m_displayCtrl->SetInsertionPointEnd();
    }
}

void ChatDisplay::EnsureVisibleAtEnd()
{
    if (m_displayCtrl) {
        m_displayCtrl->ShowPosition(m_displayCtrl->GetLastPosition());
    }
}