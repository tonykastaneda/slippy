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
	if (m == "art.fit") return {"Fitted " + who + " to another object", "fit that"};
	if (m == "art.duplicate") return {"Duplicated " + who, "duplicate that"};
	if (m == "art.copyTo") return {"Copied " + who + " to another document", "copy that to another document"};
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
	if (m == "art.clipTo") return {"Clipped art to a piece, with its outline on top" + Named(p, r), "clip art to that piece"};
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
	if (m == "plugin.message") return {"Sent " + p.str("plugin") + " " + Quote(p.str("selector")), "message " + p.str("plugin")};
	if (m == "menu.list") return {p.has("search") ? "Looked up menu commands for " + Quote(p.str("search")) : "Listed the menu commands", "look up menu commands"};
	if (m == "action.list") return {"Listed the actions", "list actions"};
	if (m == "action.describe") return {"Looked up the action " + Quote(p.str("event")), "look up " + Quote(p.str("event"))};
	if (m == "tool.list") return {"Listed the tools", "list tools"};
	if (m == "tool.current") return {"Checked the current tool", "check the current tool"};
	if (m == "tool.select") return {"Switched to " + (r.get("title").isString() ? Quote(r.get("title").asString()) : Quote(p.str("name"))), "switch to " + Quote(p.str("name"))};
	if (m == "symbol.list") return {"Listed the symbols", "list symbols"};
	if (m == "symbol.instances") return {"Found the symbol instances", "find symbol instances"};
	if (m == "symbol.create") return {"Made the symbol " + Quote(r.str("symbol")), "make a symbol"};
	if (m == "symbol.place") return {"Placed " + Quote(p.str("symbol")), "place " + Quote(p.str("symbol"))};
	if (m == "symbol.replace") return {"Swapped " + Target(p, r) + " to " + Quote(p.str("symbol")), "swap to " + Quote(p.str("symbol"))};
	if (m == "symbol.break") return {"Broke the link to a symbol", "break a symbol link"};
	if (m == "symbol.edit") return {"Editing the symbol " + Quote(r.str("editing")), "edit a symbol"};
	if (m == "symbol.finish") return {(r.get("saved").isBool() && !r.get("saved").asBool() ? "Discarded changes to " : "Saved the symbol ") + Quote(r.str("symbol")), "finish the symbol edit"};
	if (m == "symbol.redefine") return {"Redefined " + Quote(p.str("symbol")), "redefine " + Quote(p.str("symbol"))};
	if (m == "symbol.rename") return {"Renamed a symbol to " + Quote(p.str("name")), "rename a symbol"};
	if (m == "symbol.delete") return {"Deleted the symbol " + Quote(r.str("deleted")), "delete a symbol"};
	if (m == "isolation.state") return {"Checked isolation mode", "check isolation mode"};
	if (m == "isolation.enter") return {"Isolated " + Target(p, json::Value()), "isolate " + Target(p, json::Value())};
	if (m == "isolation.exit") return {"Left isolation mode", "leave isolation mode"};
	if (m == "view.get") return {"Checked the view", "check the view"};
	if (m == "view.set") return {"Changed the view", "change the view"};
	if (m == "view.fit") return {"Zoomed to fit", "zoom to fit"};
	if (m == "view.screenshot") return {"Took a screenshot of the window", "take a screenshot"};
	if (m == "art.distance") return {"Measured how close " + Target(p, json::Value()) + " comes to a path", "measure a distance"};
	if (m == "art.above") return {r.get("covered").isBool() && r.get("covered").asBool() ? std::string("Found art stacked above an object") : std::string("Nothing is stacked above that object"), "check what's on top"};
	if (m == "hit.test") return {r.get("hit").isBool() && r.get("hit").asBool() ? std::string("Found ") + KindOf(r.get("art").str("type")) + " at a point" : "Found nothing at that point", "look at a point"};
	if (m == "swatch.list") return {"Looked at the swatches", "list swatches"};
	if (m == "swatch.create") return {"Made the swatch " + Quote(p.str("name")), "make a swatch"};
	if (m == "swatch.set") return {"Changed the swatch " + Quote(p.str("name")), "change a swatch"};
	if (m == "swatch.delete") return {"Deleted the swatch " + Quote(p.str("name")), "delete a swatch"};
	if (m == "swatch.group.create") return {"Made the swatch group " + Quote(p.str("name")), "make a swatch group"};
	if (m == "spot.list") return {"Looked at the spot colors", "list spot colors"};
	if (m == "spot.create") return {"Made the color " + Quote(p.str("name")), "make a color"};
	if (m == "spot.delete") return {"Deleted the color " + Quote(p.str("name")), "delete a color"};
	if (m == "gradient.list") return {"Looked at the gradients", "list gradients"};
	if (m == "gradient.create") return {"Made the gradient " + Quote(p.str("name")), "make a gradient"};
	if (m == "gradient.set") return {"Changed the gradient " + Quote(p.str("name")), "change a gradient"};
	if (m == "gradient.delete") return {"Deleted the gradient " + Quote(p.str("name")), "delete a gradient"};
	if (m == "pattern.list") return {"Looked at the patterns", "list patterns"};
	if (m == "pattern.create") return {"Made the pattern " + Quote(p.str("name")), "make a pattern"};
	if (m == "pattern.delete") return {"Deleted the pattern " + Quote(p.str("name")), "delete a pattern"};
	if (m == "color.used") return {"Looked at the colors in use", "list colors"};
	if (m == "color.replace") return {"Replaced a color", "replace a color"};
	if (m == "color.adjust") return {"Recolored (" + p.str("mode") + ")", "recolor"};
	if (m == "appearance.get") return {"Looked at the appearance of " + Target(p, json::Value()), "read an appearance"};
	if (m == "appearance.set") {
		if (p.has("opacity")) return {"Set " + Target(p, json::Value()) + " to " + Num(p.get("opacity").asNumber()) + "% opacity", "change opacity"};
		return {"Changed the transparency of " + Target(p, json::Value()), "change transparency"};
	}
	if (m == "appearance.add") return {"Added a " + p.str("kind") + " to " + Target(p, json::Value()), "add a " + p.str("kind")};
	if (m == "appearance.remove") return {std::string(p.boolean("empty", false) ? "Removed empty fills and strokes from " : "Removed a fill or stroke from ") + Target(p, json::Value()), "remove a fill or stroke"};
	if (m == "appearance.clear") return {"Cleared effects from " + Target(p, json::Value()), "clear effects"};
	if (m == "appearance.copy") return {"Copied an appearance to " + Target(p, json::Value()), "copy an appearance"};
	if (m == "effect.list") return {"Looked at the effects", "list effects"};
	if (m == "effect.apply") return {"Added " + Quote(p.str("effect")) + " to " + Target(p, json::Value()), "add " + Quote(p.str("effect"))};
	if (m == "style.list") return {"Looked at the graphic styles", "list graphic styles"};
	if (m == "style.apply") return {"Applied the style " + Quote(p.str("name")), "apply " + Quote(p.str("name"))};
	if (m == "style.create") return {"Made the graphic style " + Quote(r.str("name")), "make a graphic style"};
	if (m == "style.redefine") return {"Redefined the style " + Quote(p.str("name")), "redefine a style"};
	if (m == "style.delete") return {"Deleted the style " + Quote(p.str("name")), "delete a style"};
	if (m.rfind("pathfinder.", 0) == 0) return {"Pathfinder " + m.substr(11) + " on " + Target(p, json::Value()), "pathfinder " + m.substr(11)};
	if (m == "shape.roundedRect") return {"Drew a rounded rectangle" + Named(p, r), "draw a rounded rectangle"};
	if (m == "shape.polygon") return {"Drew a polygon" + Named(p, r), "draw a polygon"};
	if (m == "shape.star") return {"Drew a star" + Named(p, r), "draw a star"};
	if (m == "shape.spiral") return {"Drew a spiral" + Named(p, r), "draw a spiral"};
	if (m == "shape.pie") return {"Drew a pie" + Named(p, r), "draw a pie"};
	if (m == "compound.make") return {"Made a compound path", "make a compound path"};
	if (m == "compound.release") return {"Released a compound path", "release a compound path"};
	if (m == "path.measure") return {"Measured " + Target(p, json::Value()), "measure a path"};
	if (m == "path.points") return {"Read key points on a path", "read path points"};
	if (m == "path.pointAt") return {"Found a point along a path", "find a point on a path"};
	if (m == "path.reverse") return {"Reversed " + Target(p, json::Value()), "reverse a path"};
	if (m == "path.setClosed") return {p.boolean("closed", true) ? "Closed " + Target(p, json::Value()) : "Opened " + Target(p, json::Value()), "close a path"};
	if (m == "path.simplify") return {"Simplified " + Target(p, json::Value()), "simplify a path"};
	if (m == "path.offset") return {"Offset " + Target(p, json::Value()) + " by " + Num(p.num("distance", 0)) + " pt", "offset a path"};
	if (m == "path.outlineStroke") return {"Outlined the stroke of " + Target(p, json::Value()), "outline a stroke"};
	if (m == "path.join") return {"Joined paths", "join paths"};
	if (m == "path.addAnchors") return {"Added anchor points", "add anchor points"};
	if (m == "path.removeAnchors") return {"Removed anchor points", "remove anchor points"};
	if (m == "path.setSegments") return {"Reshaped " + Target(p, json::Value()), "reshape a path"};
	if (m == "path.editPoint") return {"Moved a point on " + Target(p, json::Value()), "edit a point"};
	if (m == "path.insertPoint") return {"Added a point to " + Target(p, json::Value()), "add a point"};
	if (m == "path.deletePoints") return {"Deleted points from " + Target(p, json::Value()), "delete points"};
	if (m == "path.selectPoints") return {"Selected points on " + Target(p, json::Value()), "select points"};
	if (m == "art.outline") return {"Outlined " + Target(p, json::Value()), "outline art"};
	if (m == "art.toPaths") return {"Converted " + Target(p, json::Value()) + " to paths", "convert to paths"};
	if (m == "art.expand") return {"Expanded " + Target(p, json::Value()), "expand"};
	if (m == "art.expandAppearance") return {"Expanded the appearance of " + Target(p, json::Value()), "expand an appearance"};
	if (m == "envelope.warp") return {"Warped " + Target(p, json::Value()) + " (" + p.str("style", "arc") + ")", "warp"};
	if (m == "envelope.fromTop") return {"Fit art into the top shape", "make an envelope"};
	if (m == "envelope.release") return {"Released an envelope", "release an envelope"};
	if (m == "envelope.expand") return {"Expanded an envelope", "expand an envelope"};
	if (m.rfind("repeat.", 0) == 0) return {"Made a " + m.substr(7) + " repeat", "make a repeat"};
	if (m == "blend.make") return {"Blended " + Target(p, json::Value()), "make a blend"};
	if (m == "blend.release") return {"Released a blend", "release a blend"};
	if (m == "blend.expand") return {"Expanded a blend", "expand a blend"};
	if (m.rfind("livePaint.", 0) == 0) return {"Live Paint: " + m.substr(10), "live paint " + m.substr(10)};
	if (m == "text.get") return {"Read " + Target(p, json::Value()), "read text"};
	if (m == "text.format") return {"Formatted " + Target(p, json::Value()), "format text"};
	if (m == "text.area") return {"Added area text " + Quote(p.str("contents")), "add area text"};
	if (m == "text.onPath") return {"Put text on a path " + Quote(p.str("contents")), "put text on a path"};
	if (m == "text.outline") return {"Outlined text", "outline text"};
	if (m == "text.link") return {"Threaded text", "thread text"};
	if (m == "text.unlink") return {"Unthreaded text", "unthread text"};
	if (m == "text.find") return {"Searched text for " + Quote(p.str("search")), "search text"};
	if (m == "text.replace") return {"Replaced " + Quote(p.str("search")) + " with " + Quote(p.str("replace")), "replace text"};
	if (m == "font.list") return {"Looked up fonts", "look up fonts"};
	if (m == "charStyle.list" || m == "paraStyle.list") return {"Looked at the text styles", "list text styles"};
	if (m == "charStyle.create" || m == "paraStyle.create") return {"Made the text style " + Quote(p.str("name")), "make a text style"};
	if (m == "charStyle.apply" || m == "paraStyle.apply") return {"Applied the text style " + Quote(p.str("name")), "apply a text style"};
	if (m == "charStyle.delete" || m == "paraStyle.delete") return {"Deleted the text style " + Quote(p.str("name")), "delete a text style"};
	if (m == "artboard.list") return {"Looked at the artboards", "list artboards"};
	if (m == "artboard.add") return {"Added an artboard" + Named(p, r), "add an artboard"};
	if (m == "artboard.set") return {"Changed an artboard", "change an artboard"};
	if (m == "artboard.delete") return {"Deleted an artboard", "delete an artboard"};
	if (m == "artboard.fit") return {"Fit an artboard to the art", "fit an artboard"};
	if (m == "document.settings") return {"Checked the document settings", "change document settings"};
	if (m == "document.xmp") return {"Read the document metadata", "read metadata"};
	if (m == "document.recent") return {"Looked at recent files", "list recent files"};
	if (m == "document.print") return {"Printed the document", "print"};
	if (m == "layer.tree") return {"Looked at the layers", "list layers"};
	if (m == "select.matching") return {"Selected matching art", "select matching art"};
	if (m == "select.same") return {"Selected the same " + p.str("what"), "select the same " + p.str("what")};
	if (m == "select.all") return {"Selected everything", "select all"};
	if (m == "select.none") return {"Deselected everything", "deselect"};
	if (m == "select.inverse") return {"Inverted the selection", "invert the selection"};
	if (m == "edit.copy") return {"Copied " + Target(p, json::Value()), "copy"};
	if (m == "edit.cut") return {"Cut " + Target(p, json::Value()), "cut"};
	if (m == "edit.paste") return {"Pasted", "paste"};
	if (m == "guide.create") return {"Added a " + p.str("orientation") + " guide", "add a guide"};
	if (m == "guide.list") return {"Looked at the guides", "list guides"};
	if (m == "guide.make") return {"Made guides", "make guides"};
	if (m == "guide.release") return {"Released guides", "release guides"};
	if (m == "guide.clear") return {"Cleared the guides", "clear guides"};
	if (m == "image.info") return {"Looked at images", "look at images"};
	if (m == "image.embed") return {"Embedded images", "embed images"};
	if (m == "image.relink") return {"Relinked an image", "relink an image"};
	if (m == "image.trace") return {"Traced an image", "trace an image"};
	if (m == "art.rasterize") return {"Rasterized " + Target(p, json::Value()), "rasterize"};
	if (m == "art.transformAgain") return {"Transformed again", "transform again"};
	if (m == "data") return {p.has("set") ? "Saved data on " + std::string(p.has("id") ? "an object" : "the document") : "Read saved data", "read data"};
	if (m == "preference") return {p.has("value") ? "Changed a preference" : "Read a preference", "read a preference"};
	if (m == "app.log") return {"Read the call log", "read the call log"};
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
