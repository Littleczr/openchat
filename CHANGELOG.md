# Changelog

## v1.2.0 — 2026-04-03

Project renamed from **OpenChat** to **LlamaBoss**.

### New Features
- **Multi-model group chat** — invite a second LLM into the conversation via the `+` button in the toolbar. Model A responds first, then Model B automatically responds having seen both the user's message and Model A's full response. Sequential round-robin with full context awareness.
- **Per-model accent colors** — Model A renders in mint green, Model B in warm coral (dark theme) or burnt orange (light theme), making it easy to follow who said what.
- **Per-message model attribution** — each assistant message is tagged with the model that produced it. Conversation files store this metadata for correct replay on load.
- **Group chat context builder** — Model B receives a system prompt explaining the group chat, with Model A's responses injected as attributed user messages (`[modelA]: ...`), working within Ollama's user/assistant/system role constraint.
- **Chat state machine** — replaced the simple boolean streaming flag with a proper state machine (`Idle → SingleStreaming`, `Idle → GroupModelA → GroupModelB → Idle`) for robust orchestration.

### Improvements
- **File format v2** — conversation JSON now includes a `"models"` array and per-message `"model"` fields. Fully backwards-compatible with v1 files.
- **Smarter error handling** — errors during group chat identify which model failed and preserve the other model's completed response.
- **Stop button in group mode** — stopping during Model A cancels everything; stopping during Model B keeps Model A's committed response.

---

## v1.1.0 — 2026-03-31

### New Features
- **Dark & Light themes** — Telegram-inspired dark theme (default) and clean light theme with 26 color fields managed by ThemeManager
- **Live theme switching** — change themes in Settings without restarting; existing conversation re-renders with new colors
- **System theme detection** — "System" option reads Windows dark/light preference from registry

---

## v1.0.0 — 2026-03-30

Initial public release.

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
