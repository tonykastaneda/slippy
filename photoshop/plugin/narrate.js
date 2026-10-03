// What a call did, in plain English - for the feed and for history names.
// Returns { line, kind } where kind is read | edit | error.

const q = (s) => `“${String(s).length > 28 ? String(s).slice(0, 27) + "…" : s}”`;
const file = (p) => String(p || "").split(/[\\/]/).pop();

const LINES = {
	"app.status": () => "Checked on Photoshop",
	"document.info": () => "Looked at the document",
	"document.list": () => "Listed open documents",
	"document.activate": (p, r) => `Switched to ${r ? q(r.name) : "another document"}`,
	"document.new": (p) => `Made a new ${p.width || 1000} × ${p.height || 1000} document`,
	"document.open": (p) => `Opened ${q(file(p.path))}`,
	"document.save": (p) => p.path ? `Saved as ${q(file(p.path))}` : "Saved the document",
	"document.close": (p, r) => `Closed ${r ? q(r.closed) : "the document"}${p.save ? " (saved)" : ""}`,
	"document.export": (p) => `Exported ${q(file(p.path))}`,
	"history.undo": (p) => `Undid ${Number(p.steps) > 1 ? p.steps + " steps" : "a step"}`,
	"layer.tree": () => "Looked over the layers",
	"layer.get": (p, r) => `Inspected ${r ? q(r.name) : "a layer"}`,
	"layer.select": () => "Selected layers",
	"layer.set": (p, r) => {
		const who = r ? q(r.name) : "a layer";
		if (p.name !== undefined) return `Renamed a layer to ${q(p.name)}`;
		if (p.visible !== undefined) return `${p.visible ? "Showed" : "Hid"} ${who}`;
		if (p.opacity !== undefined) return `Set ${who} to ${Math.round(p.opacity)}% opacity`;
		if (p.blendMode !== undefined) return `Set ${who} to ${p.blendMode}`;
		if (p.locked !== undefined) return `${p.locked ? "Locked" : "Unlocked"} ${who}`;
		return `Changed ${who}`;
	},
	"layer.create": (p) => `Added ${p.kind === "group" ? "a group" : "a layer"}${p.name ? " " + q(p.name) : ""}`,
	"layer.delete": (p) => `Deleted ${p.ids && p.ids.length > 1 ? p.ids.length + " layers" : "a layer"}`,
	"layer.duplicate": (p, r) => `Duplicated ${r ? q(r.name) : "a layer"}`,
	"layer.transform": (p, r) => {
		const what = [p.dx || p.dy ? "Moved" : "", p.scale !== undefined ? "scaled" : "", p.rotate ? "rotated" : ""].filter(Boolean).join(", ");
		return `${what ? what[0].toUpperCase() + what.slice(1) : "Transformed"} ${r ? q(r.name) : "a layer"}`;
	},
	"layer.move": (p, r) => `Restacked ${r ? q(r.name) : "a layer"}`,
	"text.create": (p) => `Added text ${q(p.contents || "")}`,
	"ps.batchplay": (p) => `Ran ${Array.isArray(p.descriptors) ? p.descriptors.map((d) => d._obj).join(", ") : "an action"}`,
	"ps.get": () => "Read a Photoshop property",
};

const READS = new Set(["app.status", "document.info", "document.list", "layer.tree", "layer.get", "ps.get"]);

// "Couldn't …" wants the plain verb: Inspected → inspect, Undid → undo.
const PLAIN = { checked: "check", looked: "look", listed: "list", switched: "switch", made: "make", opened: "open",
	saved: "save", closed: "close", exported: "export", undid: "undo", inspected: "inspect", selected: "select",
	renamed: "rename", showed: "show", hid: "hide", set: "set", locked: "lock", unlocked: "unlock", changed: "change",
	added: "add", deleted: "delete", duplicated: "duplicate", moved: "move", scaled: "scale", rotated: "rotate",
	transformed: "transform", restacked: "restack", ran: "run", read: "read" };
const plain = (line) => line.replace(/^\w+(, \w+)*/, (words) =>
	words.split(", ").map((w) => PLAIN[w.toLowerCase()] || w.toLowerCase()).join(", "));

function narrate(method, params, result, error) {
	const make = LINES[method];
	let line;
	try { line = make ? make(params || {}, result) : method; } catch (e) { line = method; }
	if (error) return { line: `Couldn't ${plain(line)}: ${error.message || error}`, kind: "error" };
	return { line, kind: READS.has(method) ? "read" : "edit" };
}

globalThis.SlippyNarrate = narrate;
if (typeof module !== "undefined") module.exports = { narrate };
