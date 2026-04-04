# Changelog

## v1.3.0 — 2026-04-04

Focused UX and architecture update for the desktop Ollama client, including multi-LLM directed routing, sidebar extraction, rendering fixes, and conversation-history reliability improvements.

### Added
- Multi-LLM directed routing in group chat mode
  - Default behavior remains group discussion in loaded order
  - Explicit targeting now supported with prefixes such as `qwen,`, `qwen:`, `@qwen`, `gemma,`, `gemma:`, and `@gemma`
  - Directed prompts now trigger only the addressed model while the other model stays silent
- Extracted conversation sidebar into its own component
  - Sidebar UI and interaction logic moved out of `openchat.cpp`
  - Cleaner separation between app flow and conversation-list behavior
- Batch deletion for conversation history
  - Multi-select conversation rows
  - Delete multiple saved chats in one action from the sidebar
- Participant-aware request builder for multi-LLM conversations
  - Prevents one model from treating another model's prior assistant replies as its own

### Changed
- Group chat remains the default when no explicit target is detected
- Group replies continue to follow loaded model order (Model A first, then Model B)
- Saved assistant replies are replayed more directly instead of relying only on the streaming path
- Sidebar behavior is now more stable and easier to extend

### Fixed
- Assistant prefix / first-paragraph spacing issue on long replies
- UTF-8 conversation save/load corruption on Windows
  - Prevented conversation titles from falling back to filenames like `chat_xxxxxxxx.json`
  - Fixed long-answer history/title issues caused by unsafe `ToStdString()` usage
- Conversation history reshuffle caused by unnecessary timestamp updates when loading past chats
- Visual replay issues exposed by older long-form city prompts

### Internal
- Added turn-routing state to support group vs directed requests
- Added participant-aware chat history request building
- Improved conversation-sidebar modularity for future features such as richer keyboard support and more advanced selection behavior

---

## v1.2.0 — 2026-03-30

Initial public multi-feature desktop release.

### Features
- Streaming chat with Ollama's `/api/chat` endpoint
- Markdown rendering: bold, italic, inline code, fenced code blocks, headings, bullet/numbered lists, horizontal rules
- `<think>` block support for reasoning models (DeepSeek-R1, QwQ, etc.)
- Image attachment via file picker, drag-and-drop, and Ctrl+V clipboard paste
- Conversation auto-save/load with sidebar browser
- Settings dialog with live model list from Ollama API
- Dark theme (Telegram-inspired)
- Window position/size persistence
- Keyboard shortcuts: Ctrl+N, Ctrl+S, Ctrl+O
- Model auto-unload on switch (frees VRAM)
