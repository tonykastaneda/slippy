#ifndef __SLIPPY_COMMANDS_H__
#define __SLIPPY_COMMANDS_H__

// The command table: every method agents can call, run on Illustrator's main
// thread. Requests and responses are JSON-RPC 2.0; an array request is a
// batch that runs back to back in one timer message (one undo step).

#include "Json.h"

#include <functional>
#include <string>

namespace slippy {

enum ErrorCode {
	kErrParse = -32700,
	kErrInvalidRequest = -32600,
	kErrMethodNotFound = -32601,
	kErrInvalidParams = -32602,
	kErrInternal = -32603,
	kErrIllustrator = -32000,   // an SDK call failed; data.aiError has its code
	kErrTimeout = -32001,
	kErrUnavailable = -32002,   // no document, missing suite, shutting down
	kErrNotFound = -32004,      // art / layer / document id didn't resolve
};

json::Value Handle(const json::Value& request);   // main thread only
json::Value Describe();                           // the command list with param docs

// Told about every call after it runs (the panel animates from this).
// line: what happened, in plain English (Narrate.h). agent: who sent it
// ("Claude", "Codex"... or what the client called itself; "" if unknown).
using CallObserver = std::function<void(const std::string& method, bool ok, bool changesDocument, double milliseconds,
	const std::string& line, const std::string& agent)>;
void SetCallObserver(CallObserver observer);

} // namespace slippy

#endif // __SLIPPY_COMMANDS_H__
