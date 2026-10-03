// Finding things to run: every menu command, action event and tool the SDK
// names (tools/sdk_catalog.py builds the list from its headers), checked
// against the running Illustrator, and searchable by name or by the label
// people see. menu.run / action.play / tool.select then run what's found.

#include "Kit.h"

#include <algorithm>
#include <set>

namespace slippy {

namespace {

struct CatalogEntry { const char* name; const char* detail; };
#include "SdkCatalog.inc"

bool Contains(const std::string& haystack, const std::string& needle)
{
	return needle.empty() || Lower(haystack).find(Lower(needle)) != std::string::npos;
}

std::string FourCC(ai::uint32 code)
{
	char c[5] = {(char) ((code >> 24) & 0xFF), (char) ((code >> 16) & 0xFF), (char) ((code >> 8) & 0xFF), (char) (code & 0xFF), 0};
	for (int i = 0; i < 4; i++) if (c[i] < 32 || c[i] > 126) return std::to_string((unsigned long) code);
	return c;
}

// The label Illustrator shows for a command, or "" when it isn't registered.
std::string CommandLabel(const std::string& name, bool& available)
{
	AICommandID id = 0;
	available = sAICommandManager && !sAICommandManager->GetCommandIDFromName(name.c_str(), &id) && id;
	if (!available) return "";
	ai::UnicodeString label;
	if (sAICommandManager->GetCommandLocalizedName(id, label)) return "";
	std::string s = S(label);
	s.erase(std::remove(s.begin(), s.end(), '&'), s.end());   // Windows menu accelerators
	return s;
}

json::Value MenuList(const json::Value& p)
{
	Need(sAICommandManager, "The command manager suite");
	std::string search = p.str("search", "");
	json::Value list = json::Value::MakeArray();
	std::set<std::string> seen;
	for (const CatalogEntry& e : kMenuCatalog) {
		bool available = false;
		std::string label = CommandLabel(e.name, available);
		if (!Contains(e.name, search) && !Contains(e.detail, search) && !Contains(label, search)) continue;
		json::Value c;
		c["command"] = e.name;
		c["menu"] = e.detail;
		if (!label.empty()) c["label"] = label;
		if (!available) c["available"] = false;
		list.push(c);
		seen.insert(e.name);
	}
	// Illustrator's own search over its labels finds plug-in commands the SDK doesn't list.
	if (!search.empty()) {
		ai::UnicodeString found;
		AICommandID id = 0;
		char name[512] = {0};
		if (!sAICommandManager->SearchCommandLocalizedName(U(search), found) && !found.empty() &&
			!sAICommandManager->GetCommandIDFromLocalizedName(found, &id) && id && !sAICommandManager->GetCommandName(id, name) &&
			name[0] && !seen.count(name)) {
			json::Value c;
			c["command"] = name;
			c["label"] = S(found);
			list.push(c);
		}
	}
	json::Value v;
	v["commands"] = list;
	v["count"] = (double) list.size();
	v["run"] = "menu.run {command} with a 'command' from this list";
	return v;
}

json::Value ActionList(const json::Value& p)
{
	Need(sAIActionManager, "The action manager suite");
	std::string search = p.str("search", "");
	json::Value list = json::Value::MakeArray();
	for (const CatalogEntry& e : kActionCatalog) {
		ai::UnicodeString label;
		bool registered = sAIActionManager->IsActionEventRegistered(e.name);
		if (registered) sAIActionManager->GetActionEventLocalizedNameUS(e.name, label);
		if (!Contains(e.name, search) && !Contains(e.detail, search) && !Contains(S(label), search)) continue;
		json::Value a;
		a["event"] = e.name;
		a["group"] = e.detail;
		if (!label.empty()) a["label"] = S(label);
		if (!registered) a["available"] = false;
		list.push(a);
	}
	json::Value v;
	v["events"] = list;
	v["run"] = "action.describe {event} for its parameters, then action.play {event, params}";
	return v;
}

json::Value ActionDescribe(const json::Value& p)
{
	Need(sAIActionManager, "The action manager suite");
	std::string event = ReqStr(p, "event");
	if (!sAIActionManager->IsActionEventRegistered(event.c_str())) Fail(kErrNotFound, "no action event named '" + event + "' - see action.list");
	json::Value v;
	v["event"] = event;
	ai::UnicodeString label;
	if (!sAIActionManager->GetActionEventLocalizedNameUS(event.c_str(), label)) v["label"] = S(label);
	json::Value params = json::Value::MakeArray();
	AIActionParamTypeRef type = nullptr;
	ai::uint32 count = 0;
	if (!sAIActionManager->GetActionEventParamType(event.c_str(), &type) && type && !sAIActionManager->AIActionGetTypeCount(type, &count)) {
		for (ai::uint32 i = 0; i < count; i++) {
			ActionParamKeyID key = 0;
			if (sAIActionManager->AIActionGetTypeKey(type, i, &key)) continue;
			json::Value k;
			k["key"] = FourCC(key);
			ai::UnicodeString name;
			if (!sAIActionManager->AIActionGetNameUS(type, key, name)) k["name"] = S(name);
			ActionParamTypeID t = 0;
			if (!sAIActionManager->AIActionGetType(type, key, &t)) k["type"] = FourCC(t);
			params.push(k);
		}
	}
	v["params"] = params;
	v["note"] = "Pass these to action.play as {\"<key>\": value}; types: long = integer, doub/UntF = real, bool = boolean, TEXT/ustr = string, enum = {\"type\":\"enum\",\"value\":n,\"name\":..}";
	return v;
}

// ---- tools

std::string ToolName(AIToolHandle tool)
{
	char* name = nullptr;
	return !sAITool->GetToolName(tool, &name) && name ? name : "";
}

std::string ToolTitle(AIToolHandle tool)
{
	ai::UnicodeString title;
	return sAITool->GetToolTitle(tool, title) ? "" : S(title);
}

json::Value ToolCurrent(const json::Value&)
{
	Need(sAITool, "The tool suite");
	json::Value v;
	const char* name = nullptr;
	if (!sAITool->GetCurrentToolName(&name) && name) v["name"] = name;
	AIToolHandle tool = nullptr;
	if (!sAITool->GetSelectedTool(&tool) && tool) v["title"] = ToolTitle(tool);
	return v;
}

json::Value ToolList(const json::Value& p)
{
	Need(sAITool, "The tool suite");
	std::string search = p.str("search", "");
	const char* current = nullptr;
	sAITool->GetCurrentToolName(&current);
	json::Value list = json::Value::MakeArray();
	std::set<std::string> seen;
	ai::int32 count = 0;
	sAITool->CountTools(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AIToolHandle tool = nullptr;
		if (sAITool->GetNthTool(i, &tool) || !tool) continue;
		std::string name = ToolName(tool), title = ToolTitle(tool);
		if (name.empty() || seen.count(name) || (!Contains(name, search) && !Contains(title, search))) continue;
		seen.insert(name);
		json::Value t;
		t["name"] = name;
		if (!title.empty()) t["title"] = title;
		if (current && name == current) t["current"] = true;
		list.push(t);
	}
	for (const char* name : kToolCatalog) {   // built-ins the enumeration didn't report
		if (seen.count(name) || !Contains(name, search)) continue;
		AIToolType number = 0;
		if (sAITool->GetToolNumberFromName(name, &number)) continue;
		json::Value t;
		t["name"] = name;
		list.push(t);
	}
	return list;
}

json::Value ToolSelect(const json::Value& p)
{
	Need(sAITool, "The tool suite");
	std::string want = ReqStr(p, "name");
	// Exact tool name first, then a title or name containing the words ("pen" -> Pen Tool).
	std::string name;
	AIToolType number = 0;
	if (!sAITool->GetToolNumberFromName(want.c_str(), &number)) name = want;
	if (name.empty()) {
		ai::int32 count = 0;
		sAITool->CountTools(&count);
		std::string best;
		for (ai::int32 i = 0; i < count && name.empty(); i++) {
			AIToolHandle tool = nullptr;
			if (sAITool->GetNthTool(i, &tool) || !tool) continue;
			std::string n = ToolName(tool), title = ToolTitle(tool);
			if (Lower(title) == Lower(want) || Lower(title) == Lower(want) + " tool") name = n;
			else if (best.empty() && (Contains(title, want) || Contains(n, want))) best = n;
		}
		if (name.empty()) name = best;
	}
	if (name.empty()) Fail(kErrNotFound, "no tool matching '" + want + "' - see tool.list");
	Check(sAITool->SetSelectedToolByName(name.c_str()), "SetSelectedToolByName");
	return ToolCurrent(json::Value::MakeObject());
}

} // namespace

void AddCatalogCommands(CommandTable& t)
{
	t["menu.list"] = {"Every menu command Illustrator knows, with its menu path (\"Object > Group\") and on-screen label; "
		"'search' filters by name, path or label. Run one with menu.run {command}.",
		Params({{"search", "string - words from the name, menu path or label (optional)"}}), MenuList, false};
	t["action.list"] = {"Action events Illustrator can play (the Actions panel's building blocks), with labels. 'search' filters.",
		Params({{"search", "string (optional)"}}), ActionList, false};
	t["action.describe"] = {"An action event's parameters: 4-character keys, names and types, for action.play.",
		Params({{"event", "string - e.g. \"adobe_move\""}}), ActionDescribe, false};
	t["tool.list"] = {"Tools (name, title, current). 'search' filters by name or title.", Params({{"search", "string (optional)"}}), ToolList, false};
	t["tool.current"] = {"The tool the person has selected.", Params({}), ToolCurrent, false};
	t["tool.select"] = {"Switch the active tool, by name (\"Adobe Pen Tool\") or title words (\"pen\", \"rectangle\").",
		Params({{"name", "string"}}), ToolSelect, false};
}

} // namespace slippy
