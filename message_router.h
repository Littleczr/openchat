// message_router.h
// Determines whether a user message is directed at a specific model
// or is a group question for all active models.
//
// Supported addressing patterns (must appear at the very start):
//   @modelname  what are your strengths?
//   modelname,  what are your strengths?
//   modelname:  what are your strengths?
//
// Explicit group keywords (override to group even in directed syntax):
//   @all   / @everyone / @both
//
// Model matching is case-insensitive and prefix-based, so the user
// can type "qwen" to address "qwen3.5:latest", or "gemma" to address
// "gemma4:e2b-it-q4_K_M".  Namespace prefixes (before '/') are
// stripped before matching, so "gemma3" matches
// "pidrilkin/gemma3_27b_abliterated:Q4_K_M".

#pragma once

#include <string>
#include <vector>

struct RouteResult
{
    std::string targetModel;   // Full Ollama model name, or empty for group turn
    std::string cleanedBody;   // Message with the address prefix stripped
    bool        isDirected;    // Convenience: !targetModel.empty()
};

// Inspect a user message and determine routing.
// activeModels: the models currently loaded (Model A first, then Model B, etc.)
// Returns a RouteResult with either a specific target or empty for group.
RouteResult RouteMessage(const std::string& message,
                         const std::vector<std::string>& activeModels);
