#include "Mcp.h"
#include "Commands.h"
#include "KAGEID.h"

#include <string>

namespace kage {

namespace {

const char* kProtocol = "2025-06-18";

const char* kInstructions =
	"KAGE drives Adobe Illustrator directly through a native plug-in (no JSX). "
	"Coordinates are Illustrator artwork points with y growing upward; call document_info first for the artboard bounds. "
	"Art ids are strings from art_tree / art_selection / creation results. Commands that take ids act on the selection when none are given. "
	"Every call is one step on Edit > Undo; use kage_batch to make several calls one step.";

json::Value Error(const json::Value& id, int code, const std::string& message)
{
	json::Value r;
	r["jsonrpc"] = "2.0";
	r["id"] = id;
	r["error"]["code"] = code;
	r["error"]["message"] = message;
	return r;
}

json::Value Result(const json::Value& id, json::Value result)
{
	json::Value r;
	r["jsonrpc"] = "2.0";
	r["id"] = id;
	r["result"] = std::move(result);
	return r;
}

std::string ToolName(std::string method)
{
	for (char& c : method) if (c == '.') c = '_';
	return method;
}

// Parameter docs are "type - meaning"; the leading type becomes JSON Schema
// where it's a plain one. Anything fancier (unions, points) stays untyped
// and the description carries it.
json::Value SchemaFor(const json::Value& params)
{
	json::Value schema;
	schema["type"] = "object";
	schema["properties"] = json::Value::MakeObject();
	for (const auto& kv : params.asObject()) {
		const std::string& doc = kv.second.asString();
		std::string head = doc.substr(0, doc.find(' '));
		json::Value p;
		p["description"] = doc;
		if (head == "string" || head == "number" || head == "boolean") p["type"] = head;
		else if (head == "string[]") { p["type"] = "array"; p["items"]["type"] = "string"; }
		schema["properties"][kv.first] = p;
	}
	return schema;
}

json::Value ToolList()
{
	json::Value tools = json::Value::MakeArray();
	{
		json::Value t;
		t["name"] = "kage_status";
		t["description"] = "Check that Illustrator and KAGE are reachable: versions, open document count, undo steps.";
		t["inputSchema"]["type"] = "object";
		t["inputSchema"]["properties"] = json::Value::MakeObject();
		tools.push(t);
	}
	{
		json::Value t;
		t["name"] = "kage_batch";
		t["description"] = "Run several KAGE commands back to back as ONE undo step; stops at the first error. "
			"Each call is {method, params} with the dotted method names (shape.rect, art.transform, ...).";
		json::Value call;
		call["type"] = "object";
		call["properties"]["method"]["type"] = "string";
		call["properties"]["params"]["type"] = "object";
		call["required"] = json::Array{"method"};
		t["inputSchema"]["type"] = "object";
		t["inputSchema"]["properties"]["calls"]["type"] = "array";
		t["inputSchema"]["properties"]["calls"]["items"] = call;
		t["inputSchema"]["required"] = json::Array{"calls"};
		tools.push(t);
	}
	json::Value commands = Describe();   // keep it alive while iterating
	for (const json::Value& c : commands.asArray()) {
		const std::string& method = c.get("method").asString();
		if (method == "commands.list") continue;   // tools/list already is that
		json::Value t;
		t["name"] = ToolName(method);
		t["description"] = c.get("description").asString() + (c.boolean("changesDocument") ? " (changes the document)" : "");
		t["inputSchema"] = SchemaFor(c.get("params"));
		if (!c.boolean("changesDocument")) t["annotations"]["readOnlyHint"] = true;
		tools.push(t);
	}
	return tools;
}

// A command's JSON-RPC response as an MCP tool result.
json::Value ToolResult(const json::Value& response)
{
	json::Value out;
	const json::Value& err = response.get("error");
	json::Value text;
	text["type"] = "text";
	if (!err.isNull()) {
		std::string message = err.get("message").isString() ? err.get("message").asString() : "error";
		if (err.get("code").isNumber()) message = "KAGE error " + std::to_string(err.get("code").asInt()) + ": " + message;
		if (err.get("data").isObject()) message += " " + err.get("data").dump();
		text["text"] = message;
		out["isError"] = true;
	}
	else {
		const json::Value& result = response.get("result");
		text["text"] = result.dump(2);
		if (result.isObject()) out["structuredContent"] = result;
		out["isError"] = false;
	}
	out["content"] = json::Array{text};
	return out;
}

json::Value CallTool(const json::Value& params, const RunCall& run)
{
	std::string name = params.str("name");
	const json::Value& args = params.get("arguments");
	json::Value arguments = args.isObject() ? args : json::Value::MakeObject();

	if (name == "kage_batch") {
		const json::Value& calls = arguments.get("calls");
		if (!calls.isArray() || calls.size() == 0) {
			json::Value e;
			e["error"]["message"] = "'calls' must be a non-empty array of {method, params}";
			return ToolResult(e);
		}
		json::Value batch = json::Value::MakeArray();
		int i = 0;
		for (const json::Value& c : calls.asArray()) {
			json::Value req;
			req["jsonrpc"] = "2.0";
			req["id"] = i++;
			req["method"] = c.get("method");
			req["params"] = c.get("params").isObject() ? c.get("params") : json::Value::MakeObject();
			batch.push(req);
		}
		json::Value responses = run(batch);
		bool failed = !responses.isArray();
		if (!failed) for (const json::Value& r : responses.asArray()) if (r.has("error")) failed = true;
		json::Value out;
		json::Value text;
		text["type"] = "text";
		text["text"] = responses.dump(2);
		out["content"] = json::Array{text};
		out["isError"] = failed;
		return out;
	}

	std::string method = name == "kage_status" ? "app.info" : name;
	if (name != "kage_status") {
		size_t dot = method.find('_');   // group_action -> group.action
		if (dot != std::string::npos) method[dot] = '.';
	}
	json::Value req;
	req["jsonrpc"] = "2.0";
	req["id"] = 1;
	req["method"] = method;
	req["params"] = arguments;
	return ToolResult(run(req));
}

} // namespace

json::Value HandleMcp(const json::Value& message, const RunCall& run)
{
	if (!message.isObject() || !message.get("method").isString()) {
		// A response to something we asked (we never ask), or junk.
		return message.has("id") && !message.has("result") && !message.has("error")
			? Error(message.get("id"), kErrInvalidRequest, "expected a JSON-RPC request") : json::Value();
	}
	const std::string& method = message.get("method").asString();
	const json::Value& id = message.get("id");
	if (!message.has("id")) return json::Value();   // notification (initialized, cancelled...)
	const json::Value& params = message.get("params");

	if (method == "initialize") {
		json::Value r;
		std::string asked = params.str("protocolVersion", kProtocol);
		// Answer in the client's version when it's one this server speaks.
		r["protocolVersion"] = asked == "2025-06-18" || asked == "2025-03-26" ? asked : std::string(kProtocol);
		r["capabilities"]["tools"]["listChanged"] = false;
		r["serverInfo"]["name"] = "kage";
		r["serverInfo"]["title"] = "KAGE for Adobe Illustrator";
		r["serverInfo"]["version"] = kKAGEVersion;
		r["instructions"] = kInstructions;
		return Result(id, r);
	}
	if (method == "ping") return Result(id, json::Value::MakeObject());
	if (method == "tools/list") {
		json::Value r;
		r["tools"] = ToolList();
		return Result(id, r);
	}
	if (method == "tools/call") {
		if (!params.isObject() || !params.get("name").isString()) return Error(id, kErrInvalidParams, "tools/call needs a tool 'name'");
		return Result(id, CallTool(params, run));
	}
	return Error(id, kErrMethodNotFound, "unsupported MCP method '" + method + "'");
}

} // namespace kage
