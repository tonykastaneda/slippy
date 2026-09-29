#include "Narrate.h"

#include <cmath>
#include <cstdio>

namespace slippy {

namespace {

std::string Quote(const std::string& s)
{
	std::string t = s;
	for (char& c : t) if (c == '\n' || c == '\r') c = ' ';
	if (t.size() > 22) t = t.substr(0, 20) + "…";
	return "“" + t + "”";
}

std::string Num(double v)
{
	char buf[32];
	if (std::fabs(v - std::round(v)) < 0.05) snprintf(buf, sizeof buf, "%.0f", v);
	else snprintf(buf, sizeof buf, "%.1f", v);
	return buf;
}

std::string Plural(size_t n, const char* one, const char* many)
{
	return n == 1 ? std::string("1 ") + one : std::to_string(n) + " " + many;
}

std::string FileName(const std::string& path)
{
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* KindOf(const std::string& type)
{
	if (type == "text") return "text";
	if (type == "group") return "a group";
	if (type == "path" || type == "compoundPath") return "a shape";
	if (type == "placed" || type == "raster") return "an image";
	if (type == "symbol") return "a symbol";
	return "an object";
}

// Who a call acted on, from its result (art summaries) or its params.
std::string Target(const json::Value& params, const json::Value& result)
{
	if (result.isArray()) {
		size_t n = result.size();
		if (n == 1) {
			const json::Value& a = result.asArray()[0];
			if (a.get("name").isString()) return Quote(a.get("name").asString());
			return KindOf(a.str("type"));
		}
		if (n > 1) return Plural(n, "object", "objects");
	}
	if (result.isObject() && result.get("name").isString()) return Quote(result.get("name").asString());
	if (params.get("ids").isArray() && params.get("ids").size() != 1) return Plural(params.get("ids").size(), "object", "objects");
	if (params.has("id") || params.has("ids")) return "an object";
	return "the selection";
}

std::string Shape(const json::Value& p, const char* round, const char* square)
{
	bool same = p.get("width").isNumber() && p.get("height").isNumber() && p.get("width").asNumber() == p.get("height").asNumber();
	return same ? round : square;
}

std::string Named(const json::Value& p, const json::Value& result)
{
	if (p.get("name").isString()) return " " + Quote(p.get("name").asString());
	if (result.get("name").isString()) return " " + Quote(result.get("name").asString());
	return "";
}

std::string Paint(const json::Value& p)
{
	bool fill = p.has("fill"), stroke = p.has("stroke") || p.has("strokeWidth");
	if (fill && stroke) return "Recolored";
	if (fill) return p.get("fill").isString() && p.get("fill").asString() == "none" ? "Removed the fill of" : "Filled";
	if (stroke) return p.get("stroke").isString() && p.get("stroke").asString() == "none" ? "Removed the outline of" : "Outlined";
	return "";
}

std::string Transform(const json::Value& p, const std::string& who)
{
	std::string parts;
	auto add = [&](const std::string& s) { parts += (parts.empty() ? "" : ", ") + s; };
	double rotate = p.get("rotate").isNumber() ? p.get("rotate").asNumber() : 0;
	const json::Value& scale = p.get("scale");
	const json::Value& move = p.get("translate");
	std::string first;
	if (move.isArray() && move.size() == 2 && (move.asArray()[0].asNumber() != 0 || move.asArray()[1].asNumber() != 0)) first = "Moved";
	if (rotate != 0) { if (first.empty()) first = "Rotated"; else add("rotated"); }
	if (scale.isNumber() && scale.asNumber() != 1) { if (first.empty()) first = "Scaled"; else add("scaled"); }
	else if (scale.isArray()) { if (first.empty()) first = "Scaled"; else add("scaled"); }
	if (first.empty()) return "Transformed " + who;
	std::string line = first + " " + who;
	if (first == "Rotated") line += " " + Num(rotate) + "°";
	if (first == "Scaled" && scale.isNumber()) line += " to " + Num(scale.asNumber() * 100) + "%";
	if (!parts.empty()) line += ", " + parts;
	return line;
}

// What a call does, phrased as done ("Drew a circle") and as tried ("draw a circle").
struct Phrase { std::string done, attempt; };

Phrase Describe(const std::string& m, const json::Value& p, const json::Value& r)
{
	std::string who = Target(p, r);
	if (m == "app.info") return {"Checked in with Illustrator", "check in with Illustrator"};
	if (m == "commands.list") return {"Looked up what Slippy can do", "look up commands"};
	if (m == "document.list") return {"Listed the open documents", "list documents"};
	if (m == "document.info") return {"Looked at the document", "look at the document"};
	if (m == "document.formats") return {"Checked the file formats", "check file formats"};
	if (m == "document.redraw") return {"Refreshed the view", "refresh the view"};
	if (m == "document.new") {
		std::string size = p.get("width").isNumber() && p.get("height").isNumber()
			? " (" + Num(p.get("width").asNumber()) + " × " + Num(p.get("height").asNumber()) + ")" : "";
		std::string title = p.get("title").isString() ? Quote(p.get("title").asString()) : "a new document";
		return {"Created " + title + size, "create a document"};
	}
	if (m == "document.open") return {"Opened " + Quote(FileName(p.str("path"))), "open " + Quote(FileName(p.str("path")))};
	if (m == "document.activate") return {"Switched to " + (r.get("name").isString() ? Quote(r.get("name").asString()) : "another document"), "switch documents"};
	if (m == "document.save") {
		std::string file = p.get("path").isString() ? Quote(FileName(p.get("path").asString())) : "the document";
		return {"Saved " + file, "save " + file};
	}
	if (m == "document.close") return {"Closed " + (r.get("closed").isString() ? Quote(r.get("closed").asString()) : "a document"), "close the document"};
	if (m == "layer.list") return {"Looked at the layers", "look at the layers"};
	if (m == "layer.create") return {"Added a layer" + Named(p, r), "add a layer"};
	if (m == "layer.set") {
		std::string layer = p.get("layer").isString() ? Quote(p.get("layer").asString()) : "a layer";
		if (p.boolean("delete", false)) return {"Deleted layer " + layer, "delete layer " + layer};
		if (p.get("name").isString()) return {"Renamed layer " + layer + " to " + Quote(p.get("name").asString()), "rename layer " + layer};
		if (p.has("visible")) return {std::string(p.boolean("visible") ? "Showed" : "Hid") + " layer " + layer, "change layer " + layer};
		if (p.has("locked")) return {std::string(p.boolean("locked") ? "Locked" : "Unlocked") + " layer " + layer, "change layer " + layer};
		return {"Switched to layer " + layer, "change layer " + layer};
	}
	if (m == "art.tree") return {"Looked over the artwork", "look over the artwork"};
	if (m == "art.get") return {"Inspected " + who, "inspect an object"};
	if (m == "art.selection") return {"Checked what's selected", "check the selection"};
	if (m == "art.select") {
		if (!p.has("id") && !p.has("ids")) return {"Deselected everything", "deselect"};
		return {"Selected " + who, "select that"};
	}
	if (m == "art.set") {
		if (p.get("contents").isString()) return {"Changed text to " + Quote(p.get("contents").asString()), "change the text"};
		std::string paint = Paint(p);
		if (!paint.empty()) return {paint + " " + who, "restyle " + who};
		if (p.get("name").isString()) return {"Named " + (r.isArray() && r.size() > 1 ? who : "it") + " " + Quote(p.get("name").asString()), "rename that"};
		if (p.has("hidden")) return {std::string(p.boolean("hidden") ? "Hid " : "Showed ") + who, "hide that"};
		if (p.has("locked")) return {std::string(p.boolean("locked") ? "Locked " : "Unlocked ") + who, "lock that"};
		return {"Edited " + who, "edit that"};
	}
	if (m == "art.transform") return {Transform(p, who), "move that"};
	if (m == "art.duplicate") return {"Duplicated " + who, "duplicate that"};
	if (m == "art.arrange") return {p.str("to") == "back" ? "Sent " + who + " to the back" : "Brought " + who + " to the front", "rearrange that"};
	if (m == "art.group") {
		size_t n = p.get("ids").isArray() ? p.get("ids").size() : 0;
		std::string as = Named(p, r);
		return {"Grouped " + (n ? Plural(n, "object", "objects") : std::string("the selection")) + (as.empty() ? "" : " as" + as), "group those"};
	}
	if (m == "art.move") {
		std::string where = p.has("into") ? (p.str("position", "top") == "bottom" ? "to the bottom of a group" : "into a group")
			: p.has("above") ? "above another object" : "below another object";
		return {"Moved " + who + " " + where, "move that"};
	}
	if (m == "art.ungroup") return {"Ungrouped" + (r.isArray() ? " " + Plural(r.size(), "object", "objects") : std::string()), "ungroup that"};
	if (m == "art.clip") return {"Made a clipping mask" + Named(p, r), "make the clipping mask"};
	if (m == "art.unclip") return {"Released a clipping mask", "release the mask"};
	if (m == "art.place") {
		std::string path = p.str("path");
		std::string file = path.substr(path.find_last_of('/') + 1);
		return {std::string(p.boolean("link", true) ? "Placed " : "Embedded ") + Quote(file) + (p.has("fitTo") ? ", sized to fit" : ""), "place " + Quote(file)};
	}
	if (m == "art.delete") {
		size_t n = r.get("deleted").isNumber() ? (size_t) r.get("deleted").asNumber() : 1;
		return {"Deleted " + Plural(n, "object", "objects"), "delete that"};
	}
	if (m == "shape.rect") { std::string s = Shape(p, "a square", "a rectangle"); return {"Drew " + s + Named(p, r), "draw " + s}; }
	if (m == "shape.ellipse") { std::string s = Shape(p, "a circle", "an ellipse"); return {"Drew " + s + Named(p, r), "draw " + s}; }
	if (m == "path.create") {
		size_t n = p.get("points").isArray() ? p.get("points").size() : 0;
		std::string s = n == 2 ? "a line" : "a path";
		return {"Drew " + s + Named(p, r), "draw " + s};
	}
	if (m == "text.create") return {"Added text " + Quote(p.str("contents")), "add text"};
	if (m == "menu.run") {
		if (r.get("changed").isBool() && !r.get("changed").asBool())
			return {"Tried " + Quote(p.str("command")) + " - nothing changed", "run " + Quote(p.str("command"))};
		return {"Ran the menu command " + Quote(p.str("command")), "run " + Quote(p.str("command"))};
	}
	if (m == "action.play") return {"Played the action " + Quote(p.str("event")), "play " + Quote(p.str("event"))};
	if (m == "history.undo") {
		int n = p.get("steps").isNumber() ? p.get("steps").asInt() : 1;
		return {n == 1 ? "Undid the last change" : "Undid " + std::to_string(n) + " changes", "undo"};
	}
	if (m == "history.redo") {
		int n = p.get("steps").isNumber() ? p.get("steps").asInt() : 1;
		return {n == 1 ? "Redid the last change" : "Redid " + std::to_string(n) + " changes", "redo"};
	}
	return {"Ran " + m, "run " + m};
}

// A failure, short enough for one feed line; empty when there's no plain way
// to say it (then "Couldn't <attempt>").
std::string Why(const std::string& error)
{
	if (error.rfind("no art with id", 0) == 0) return "Couldn't find that object";
	if (error.rfind("no document is open", 0) == 0) return "No document is open";
	if (error.rfind("Slippy is paused", 0) == 0) return "Refused: Slippy is paused";
	if (error.rfind("no layer", 0) == 0) return "Couldn't find that layer";
	if (error.rfind("pass 'id' or 'ids', or select something", 0) == 0) return "Nothing was selected";
	if (error.rfind("unknown method", 0) == 0) return "Unknown command";
	if (error.rfind("Illustrator didn't run the call", 0) == 0) return "Illustrator was busy";
	return "";
}

} // namespace

std::string Narrate(const std::string& method, const json::Value& params, const json::Value& result, const std::string& error)
{
	json::Value p = params.isObject() ? params : json::Value::MakeObject();
	Phrase phrase = Describe(method, p, error.empty() ? result : json::Value());
	if (error.empty()) return phrase.done;
	std::string why = Why(error);
	// First line shows in the feed; the rest is the row's tooltip.
	return (why.empty() ? "Couldn't " + phrase.attempt : why) + "\n" + error;
}

} // namespace slippy
