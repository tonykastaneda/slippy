#ifndef __SLIPPY_COMMANDS_H__
#define __SLIPPY_COMMANDS_H__

// The command table: every method agents can call, run on the app's main
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
	kErrHost = -32000,          // the app refused (data.aiError / data.psError has its code)
	kErrIllustrator = kErrHost,
	kErrTimeout = -32001,
	kErrUnavailable = -32002,   // no document, missing suite, shutting down
	kErrNotFound = -32004,      // art / layer / document id didn't resolve
};

// Runs a call or a batch (main thread only). Opening, closing, creating or
// switching documents ends the run: Illustrator finishes that work only once
// it has control again, and running on in the same event crashed it (closing
// the last document, then opening another in one batch). A batch that reaches
// one with calls to go returns what ran and puts the rest in 'rest', to run on
// a later event; EndsRun says a lone call was one of them.
// Writing the document (save, export, close with save) in a run that also
// edits makes Illustrator roll the run's edits back when it ends (30.2): the
// calls all report success, then the changes are gone. So a write runs alone -
// a batch splits before and after it, and StartsRun says a queued call or
// batch must wait for an event of its own.
// prior holds responses from earlier parts of a document-split batch, so
// references such as $0.id still resolve after Illustrator changes documents.
json::Value Handle(const json::Value& request, json::Value* rest = nullptr, const json::Value* prior = nullptr);
bool EndsRun(const json::Value& call);
bool StartsRun(const json::Value& request);
bool WritesFile(const json::Value& call);
json::Value Describe();                           // the command list with param docs

// Told about every call after it runs (the panel animates from this).
// line: what happened, in plain English (Narrate.h). agent: who sent it
// ("Claude", "Codex"... or what the client called itself; "" if unknown).
using CallObserver = std::function<void(const std::string& method, bool ok, bool changesDocument, double milliseconds,
	const std::string& line, const std::string& agent)>;
void SetCallObserver(CallObserver observer);

} // namespace slippy

#endif // __SLIPPY_COMMANDS_H__
