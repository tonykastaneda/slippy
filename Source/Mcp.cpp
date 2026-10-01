#include "Mcp.h"
#include "Commands.h"
#include "SlippyID.h"

#include <map>
#include <mutex>
#include <string>

namespace slippy {

namespace {

const char* kProtocol = "2025-06-18";

const char* kInstructions =
	"Slippy drives Adobe Illustrator directly through a native plug-in (no JSX). It has about 190 commands - symbols, swatches, "
	"gradients, appearance and effects, pathfinder, text and styles, artboards, layers, images and more. Only the everyday ones are "
	"tools here: find the rest with slippy_find (search words like \"symbol\", \"gradient\", \"artboard\") and run them with slippy_call. "
	"Coordinates are Illustrator artwork points with y growing upward; call document_info first for the artboard bounds. "
	"Art ids are strings from art_tree / art_selection / creation results. Commands that take ids act on the selection when none are given. "
	"Every call is one step on Edit > Undo; use slippy_batch to make several calls one step.";

// The tools every agent gets directly; everything else is one slippy_find away.
const char* const kEveryday[] = {"document.info", "art.tree", "art.get", "art.selection", "art.select", "art.set", "art.transform",
	"art.delete", "shape.rect", "path.create", "text.create", "document.export", "history.undo"};

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

// Each client's MCP introduction (clientInfo name / version), by its
// User-Agent: initialize comes once, tool calls come with only the header.
std::mutex gClientsMutex;
std::map<std::string, std::string> gClients;

std::string Introduced(const std::string& userAgent)
{
	std::lock_guard<std::mutex> lock(gClientsMutex);
	auto it = gClients.find(userAgent);
	return it == gClients.end() ? "" : it->second;
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

json::Value CommandTool(const json::Value& c)
{
	json::Value t;
	t["name"] = ToolName(c.get("method").asString());
	t["description"] = c.get("description").asString() + (c.boolean("changesDocument") ? " (changes the document)" : "");
	t["inputSchema"] = SchemaFor(c.get("params"));
	if (!c.boolean("changesDocument")) t["annotations"]["readOnlyHint"] = true;
	return t;
}

// allTools: every command as its own tool (for clients that take ~200 tools);
// otherwise the everyday ones plus slippy_find / slippy_call.
json::Value ToolList(bool allTools)
{
	json::Value tools = json::Value::MakeArray();
	{
		json::Value t;
		t["name"] = "slippy_status";
		t["description"] = "Check that Illustrator and Slippy are reachable: versions, open document count, undo steps.";
		t["inputSchema"]["type"] = "object";
		t["inputSchema"]["properties"] = json::Value::MakeObject();
		t["annotations"]["readOnlyHint"] = true;
		tools.push(t);
	}
	{
		json::Value t;
		t["name"] = "slippy_find";
		t["description"] = "Search Slippy's ~190 commands by words (\"symbol edit\", \"gradient\", \"artboard\", \"opacity\", \"font\"): "
			"returns each match's method, what it does and its parameters. Run one with slippy_call. No search: the command families.";
		t["inputSchema"]["type"] = "object";
		t["inputSchema"]["properties"]["search"]["type"] = "string";
		t["inputSchema"]["properties"]["search"]["description"] = "words to look for in command names, descriptions and parameters";
		t["annotations"]["readOnlyHint"] = true;
		tools.push(t);
	}
	{
		json::Value t;
		t["name"] = "slippy_call";
		t["description"] = "Run any Slippy command by its method name from slippy_find (e.g. \"symbol.edit\", \"swatch.create\"), with its params.";
		t["inputSchema"]["type"] = "object";
		t["inputSchema"]["properties"]["method"]["type"] = "string";
		t["inputSchema"]["properties"]["method"]["description"] = "dotted method name, e.g. \"gradient.create\"";
		t["inputSchema"]["properties"]["params"]["type"] = "object";
		t["inputSchema"]["properties"]["params"]["description"] = "the command's parameters, as slippy_find lists them";
		t["inputSchema"]["required"] = json::Array{"method"};
		tools.push(t);
	}
	{
		json::Value t;
		t["name"] = "slippy_batch";
		t["description"] = "Run several Slippy commands back to back as ONE undo step; stops at the first error. "
			"Each call is {method, params} with the dotted method names (shape.rect, art.transform, ...). "
			"Opening, closing, creating or switching documents splits the batch there: Illustrator finishes that "
			"before the rest runs (results still come back together). Batch edits that belong together.";
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
		bool everyday = false;
		for (const char* e : kEveryday) if (method == e) everyday = true;
		if (allTools || everyday) tools.push(CommandTool(c));
	}
	return tools;
}

std::string LowerCase(std::string s)
{
	for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
	return s;
}

// Every word must appear in the method, its description or its parameters.
json::Value Find(const std::string& search)
{
	json::Value commands = Describe();
	json::Value out;
	if (search.empty()) {
		json::Value families = json::Value::MakeObject();
		for (const json::Value& c : commands.asArray()) {
			std::string m = c.get("method").asString();
			std::string family = m.substr(0, m.find('.'));
			families[family] = (families.get(family).isNumber() ? families.get(family).asNumber() : 0) + 1;
		}
		out["families"] = families;
		out["next"] = "slippy_find {search: \"<family or words>\"} for the commands and their parameters";
		return out;
	}
	std::vector<std::string> words;
	for (size_t i = 0, j; i < search.size(); i = j + 1) {
		j = search.find(' ', i);
		if (j == std::string::npos) j = search.size();
		if (j > i) words.push_back(LowerCase(search.substr(i, j - i)));
	}
	json::Value list = json::Value::MakeArray();
	for (const json::Value& c : commands.asArray()) {
		std::string hay = LowerCase(c.get("method").asString() + " " + c.get("description").asString() + " " + c.get("params").dump());
		bool all = true;
		for (const std::string& w : words) if (hay.find(w) == std::string::npos) all = false;
		if (all) list.push(c);
	}
	out["commands"] = list;
	out["run"] = "slippy_call {method, params}";
	if (list.size() == 0) out["hint"] = "nothing matched every word - try fewer or broader words, or no search for the families";
	return out;
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
		if (err.get("code").isNumber()) message = "Slippy error " + std::to_string(err.get("code").asInt()) + ": " + message;
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

json::Value CallTool(const json::Value& params, const RunCall& run, const std::string& agent)
{
	std::string name = params.str("name");
	const json::Value& args = params.get("arguments");
	json::Value arguments = args.isObject() ? args : json::Value::MakeObject();

	if (name == "slippy_batch") {
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
			req["agent"] = agent;
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

	if (name == "slippy_find") {
		json::Value r;
		r["result"] = Find(arguments.str("search", ""));
		return ToolResult(r);
	}

	json::Value req;
	req["jsonrpc"] = "2.0";
	req["id"] = 1;
	req["agent"] = agent;
	if (name == "slippy_call") {
		if (!arguments.get("method").isString()) {
			json::Value e;
			e["error"]["message"] = "slippy_call needs 'method' - find one with slippy_find";
			return ToolResult(e);
		}
		req["method"] = arguments.get("method");
		req["params"] = arguments.get("params").isObject() ? arguments.get("params") : json::Value::MakeObject();
		return ToolResult(run(req));
	}
	// A command's own tool: its name back to the dotted method (swatch_group_create -> swatch.group.create).
	std::string method = name == "slippy_status" ? "app.info" : "";
	if (method.empty()) {
		json::Value commands = Describe();
		for (const json::Value& c : commands.asArray())
			if (ToolName(c.get("method").asString()) == name) method = c.get("method").asString();
		if (method.empty()) {
			json::Value e;
			e["error"]["message"] = "unknown tool '" + name + "' - use slippy_find, then slippy_call";
			return ToolResult(e);
		}
	}
	req["method"] = method;
	req["params"] = arguments;
	return ToolResult(run(req));
}

} // namespace

std::string AgentName(const std::string& clientInfo)
{
	std::string s = LowerCase(clientInfo);
	static const std::pair<const char*, const char*> known[] = {
		{"claude", "Claude"}, {"anthropic", "Claude"}, {"codex", "Codex"}, {"openai", "Codex"}, {"chatgpt", "ChatGPT"},
		{"cursor", "Cursor"}, {"gemini", "Gemini"}, {"antigravity", "Gemini"}, {"qwen", "Qwen"}, {"kimi", "Kimi"}, {"moonshot", "Kimi"},
		{"grok", "Grok"}, {"xai", "Grok"}, {"copilot", "Copilot"}, {"visual studio code", "VS Code"}, {"vscode", "VS Code"},
		{"windsurf", "Windsurf"}, {"codeium", "Windsurf"}, {"cline", "Cline"}, {"roo", "Roo Code"}, {"opencode", "OpenCode"},
		{"zed", "Zed"}, {"goose", "Goose"}, {"mcp-remote", "mcp-remote"}, {"python", "Script"}, {"curl", "Script"}};
	for (auto& k : known) if (s.find(k.first) != std::string::npos) return k.second;
	return "";
}

json::Value HandleMcp(const json::Value& message, const RunCall& run, bool allTools, const std::string& userAgent)
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
		const json::Value& info = params.get("clientInfo");
		if (info.isObject()) {
			std::string who = info.str("name", "") + " " + info.str("version", "");
			std::lock_guard<std::mutex> lock(gClientsMutex);
			gClients[userAgent] = who;
		}
		json::Value r;
		std::string asked = params.str("protocolVersion", kProtocol);
		// Answer in the client's version when it's one this server speaks.
		r["protocolVersion"] = asked == "2025-06-18" || asked == "2025-03-26" ? asked : std::string(kProtocol);
		r["capabilities"]["tools"]["listChanged"] = false;
		r["serverInfo"]["name"] = "slippy";
		r["serverInfo"]["title"] = "Slippy for Adobe Illustrator";
		r["serverInfo"]["version"] = kSlippyVersion;
		r["instructions"] = kInstructions;
		return Result(id, r);
	}
	if (method == "ping") return Result(id, json::Value::MakeObject());
	if (method == "tools/list") {
		json::Value r;
		r["tools"] = ToolList(allTools);
		return Result(id, r);
	}
	if (method == "tools/call") {
		if (!params.isObject() || !params.get("name").isString()) return Error(id, kErrInvalidParams, "tools/call needs a tool 'name'");
		std::string introduced = Introduced(userAgent);
		std::string agent = AgentName(introduced + " " + userAgent);
		if (agent.empty()) agent = introduced.empty() ? userAgent : introduced;
		return Result(id, CallTool(params, run, agent));
	}
	return Error(id, kErrMethodNotFound, "unsupported MCP method '" + method + "'");
}

} // namespace slippy
