#ifndef __KAGE_MCP_H__
#define __KAGE_MCP_H__

// MCP (Model Context Protocol) over KAGE's HTTP server: POST /mcp, the
// streamable HTTP transport, answered as plain JSON (no SSE stream). Every
// KAGE command is a tool (art.transform -> art_transform), plus kage_batch
// (many calls, one undo step) and kage_status. Stateless: no session ids.
//
// Runs on a server thread. Tool calls go through `run`, which hands them to
// Illustrator's main thread the same way /rpc calls go.

#include "Json.h"

#include <functional>

namespace kage {

using RunCall = std::function<json::Value(const json::Value& jsonRpcRequest)>;

// One MCP message in; its response out, or null for a notification.
json::Value HandleMcp(const json::Value& message, const RunCall& run);

} // namespace kage

#endif // __KAGE_MCP_H__
