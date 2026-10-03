#ifndef __SLIPPY_MCP_H__
#define __SLIPPY_MCP_H__

// MCP (Model Context Protocol) over Slippy's HTTP server: POST /mcp, the
// streamable HTTP transport, answered as plain JSON (no SSE stream). Tools:
// slippy_find (search the commands) and slippy_call (run any of them), the
// everyday commands as their own tools (art.transform -> art_transform),
// slippy_batch (many calls, one undo step) and slippy_status. With allTools
// (the URL's ?tools=all) every command is its own tool. Stateless.
//
// Runs on a server thread. Tool calls go through `run`, which hands them to
// Illustrator's main thread the same way /rpc calls go.

#include "Json.h"

#include <functional>
#include <string>
#include <vector>

namespace slippy {

// What MCP tells agents about the app Slippy runs in. The defaults describe
// Illustrator; Slippy for Photoshop sets its own at startup.
struct McpHost {
	std::string title = "Slippy for Adobe Illustrator";
	std::string app = "Illustrator";
	std::string instructions =
		"Slippy drives Adobe Illustrator directly through a native plug-in (no JSX). It has about 190 commands - symbols, swatches, "
		"gradients, appearance and effects, pathfinder, text and styles, artboards, layers, images and more. Only the everyday ones are "
		"tools here: find the rest with slippy_find (search words like \"symbol\", \"gradient\", \"artboard\") and run them with slippy_call. "
		"Coordinates are Illustrator artwork points with y growing upward; call document_info first for the artboard bounds. "
		"Art ids are strings from art_tree / art_selection / creation results. Commands that take ids act on the selection when none are given. "
		"Every call is one step on Edit > Undo; use slippy_batch to make several calls one step.";
	std::string findExamples = "(\"symbol edit\", \"gradient\", \"artboard\", \"opacity\", \"font\")";
	std::string callExamples = "(e.g. \"symbol.edit\", \"swatch.create\")";
	std::string batchExamples = "(shape.rect, art.transform, ...)";
	std::string commandCount = "~190";
	std::vector<std::string> everyday = {"document.info", "art.tree", "art.get", "art.selection", "art.select", "art.set", "art.transform",
		"art.delete", "shape.rect", "path.create", "text.create", "document.export", "history.undo"};
};
void SetMcpHost(McpHost host);

using RunCall = std::function<json::Value(const json::Value& jsonRpcRequest)>;

// One MCP message in; its response out, or null for a notification.
// userAgent: the HTTP client's; with the client's MCP introduction it names the agent.
json::Value HandleMcp(const json::Value& message, const RunCall& run, bool allTools, const std::string& userAgent);

// A friendly agent name - "Claude", "Codex", "Cursor", "Gemini"... - from
// whatever the client said about itself; "" when there's nothing to go on.
std::string AgentName(const std::string& clientInfo);

} // namespace slippy

#endif // __SLIPPY_MCP_H__
