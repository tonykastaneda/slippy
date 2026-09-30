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

namespace slippy {

using RunCall = std::function<json::Value(const json::Value& jsonRpcRequest)>;

// One MCP message in; its response out, or null for a notification.
json::Value HandleMcp(const json::Value& message, const RunCall& run, bool allTools);

} // namespace slippy

#endif // __SLIPPY_MCP_H__
