#include "Narrate.h"

#include <cmath>
#include <string>

// Slippy for Photoshop's plain-English lines: what a call did ("Renamed a
// layer to “Logo”"), or what it tried. They also name each History step
// ("Slippy: Blurred the selection").

namespace slippy {

namespace {

std::string Q(std::string s)
{
	for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
	if (s.size() > 28) {
		size_t cut = 27;
		while (cut > 0 && ((unsigned char) s[cut] & 0xC0) == 0x80) cut--;   // whole UTF-8 characters
		s = s.substr(0, cut) + "…";
	}
	return "“" + s + "”";
}

std::string File(const std::string& path)
{
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Num(double v)
{
	char buf[32];
	snprintf(buf, sizeof buf, std::fabs(v - std::round(v)) < 0.05 ? "%.0f" : "%.1f", v);
	return buf;
}

struct Phrase {
	std::string done;      // "Renamed a layer to “Logo”"
	std::string attempt;   // "rename a layer" - after "Couldn't "
};

std::string Name(const json::Value& r, const char* fallback)
{
	return r.isObject() && r.get("name").isString() ? Q(r.get("name").asString()) : fallback;
}

Phrase Describe(const std::string& m, const json::Value& p, const json::Value& r)
{
	if (m == "app.info") return {"Checked on Photoshop", "reach Photoshop"};
	if (m == "commands.list") return {"Listed Slippy's commands", "list the commands"};
	if (m == "app.log") return {"Read Slippy's call log", "read the call log"};
	if (m == "document.info") return {"Looked at the document", "look at the document"};
	if (m == "document.list") return {"Listed open documents", "list the documents"};
	if (m == "document.activate") return {"Switched to " + Name(r, "another document"), "switch documents"};
	if (m == "document.new") return {"Made a new " + Num(p.num("width", 1000)) + " × " + Num(p.num("height", 1000)) + " document", "make a document"};
	if (m == "document.open") return {"Opened " + Q(File(p.str("path"))), "open " + Q(File(p.str("path")))};
	if (m == "document.save") return {p.has("path") ? "Saved as " + Q(File(p.str("path"))) : "Saved the document", "save the document"};
	if (m == "document.close") {
		std::string who = r.isObject() && r.get("closed").isString() ? Q(r.get("closed").asString()) : "the document";
		return {"Closed " + who + (p.boolean("save") ? " (saved)" : ""), "close the document"};
	}
	if (m == "document.export") return {"Exported " + Q(File(p.str("path"))), "export " + Q(File(p.str("path")))};
	if (m == "document.crop") return {"Cropped the document", "crop the document"};
	if (m == "history.undo") return {p.num("steps", 1) > 1 ? "Undid " + Num(p.num("steps")) + " steps" : "Undid a step", "undo"};
	if (m == "history.redo") return {p.num("steps", 1) > 1 ? "Redid " + Num(p.num("steps")) + " steps" : "Redid a step", "redo"};
	if (m == "layer.tree") return {"Looked over the layers", "read the layers"};
	if (m == "layer.get") return {"Inspected " + Name(r, "a layer"), "inspect the layer"};
	if (m == "layer.select") return {"Selected layers", "select layers"};
	if (m == "layer.set") {
		std::string who = Name(r, "a layer");
		if (p.has("name")) return {"Renamed a layer to " + Q(p.str("name")), "rename the layer"};
		if (p.has("visible")) return {std::string(p.boolean("visible") ? "Showed " : "Hid ") + who, p.boolean("visible") ? "show the layer" : "hide the layer"};
		if (p.has("opacity")) return {"Set " + who + " to " + Num(p.num("opacity")) + "% opacity", "set the opacity"};
		if (p.has("blendMode")) return {"Set " + who + " to " + p.str("blendMode"), "set the blend mode"};
		if (p.has("locked")) return {std::string(p.boolean("locked") ? "Locked " : "Unlocked ") + who, "lock the layer"};
		return {"Changed " + who, "change the layer"};
	}
	if (m == "layer.create") {
		std::string what = p.str("kind") == "group" ? "a group" : "a layer";
		return {"Added " + what + (p.has("name") ? " " + Q(p.str("name")) : ""), "add " + what};
	}
	if (m == "layer.delete") {
		bool many = p.get("ids").isArray() && p.get("ids").size() > 1;
		return {many ? "Deleted " + std::to_string(p.get("ids").size()) + " layers" : "Deleted a layer", "delete the layer"};
	}
	if (m == "layer.duplicate") return {"Duplicated " + Name(r, "a layer"), "duplicate the layer"};
	if (m == "layer.transform") {
		std::string what;
		if (p.num("dx") || p.num("dy")) what = "Moved";
		if (p.has("scale")) what += what.empty() ? "Scaled" : ", scaled";
		if (p.num("rotate")) what += what.empty() ? "Rotated" : ", rotated";
		return {(what.empty() ? "Transformed" : what) + " " + Name(r, "a layer"), "transform the layer"};
	}
	if (m == "layer.move") return {"Restacked " + Name(r, "a layer"), "restack the layer"};
	if (m == "layer.perspective") return {"Straightened the perspective", "straighten the perspective"};
	if (m == "text.create") return {"Added text " + Q(p.str("contents")), "add text"};
	if (m == "select.rect") return {"Selected a rectangle", "select a rectangle"};
	if (m == "select.ellipse") return {"Selected an ellipse", "select an ellipse"};
	if (m == "select.all") return {"Selected everything", "select all"};
	if (m == "select.none") return {"Deselected", "deselect"};
	if (m == "edit.fill") return {"Filled the selection", "fill the selection"};
	if (m == "filter.blur") return {"Blurred " + std::string(p.has("radius") ? Num(p.num("radius")) + " px" : "the selection"), "blur"};
	if (m == "ps.get") return {"Read a Photoshop property", "read the property"};
	if (m == "ps.batchplay") {
		std::string events;
		const json::Value& d = p.get("descriptors");
		if (d.isArray()) for (const json::Value& e : d.asArray()) {
			if (!e.isObject() || !e.get("_obj").isString()) continue;
			if (!events.empty()) events += ", ";
			if (events.size() > 40) { events += "…"; break; }
			events += e.get("_obj").asString();
		}
		return {"Ran " + (events.empty() ? std::string("an action") : events), "run the action"};
	}
	return {m, "run " + m};
}

// Errors that say it better than "Couldn't <attempt>".
std::string Why(const std::string& error)
{
	if (error.find("No document is open") != std::string::npos) return "No document is open";
	if (error.find("no layer with id") != std::string::npos) return "Couldn't find that layer";
	if (error.find("Photoshop is busy") != std::string::npos) return "Photoshop is busy";
	return "";
}

} // namespace

std::string Narrate(const std::string& method, const json::Value& params, const json::Value& result, const std::string& error)
{
	json::Value p = params.isObject() ? params : json::Value::MakeObject();
	Phrase phrase = Describe(method, p, error.empty() ? result : json::Value());
	if (error.empty()) return phrase.done;
	std::string why = Why(error);
	return (why.empty() ? "Couldn't " + phrase.attempt : why) + "\n" + error;
}

} // namespace slippy
