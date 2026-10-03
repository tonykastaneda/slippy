// Text, exports, and the raw batchPlay escape hatch.

const { app, action } = require("photoshop");
const { define, requireDocument, fail } = require("./registry.js");
const { fileEntry } = require("./documents.js");
const { summary } = require("./layers.js");

function solidColor(hex) {
	const c = new app.SolidColor();
	const h = String(hex || "#000000").replace("#", "");
	if (!/^[0-9a-fA-F]{6}$/.test(h)) throw fail("invalid_params", `color must be #RRGGBB, got ${hex}`);
	c.rgb.hexValue = h.toUpperCase();
	return c;
}

define("text.create", {
	kind: "edit", description: "New point text layer: contents at (x, y) pixels (the first baseline's start), size in points, font (PostScript name), color #RRGGBB.",
	params: { contents: "string", x: "number", y: "number", size: "number - points (default 24)", font: "string - PostScript name",
		color: "#RRGGBB", name: "string" },
	run: async (p) => {
		const doc = requireDocument();
		if (!p.contents) throw fail("invalid_params", "contents is required");
		const opts = { contents: String(p.contents), fontSize: Number(p.size) || 24,
			position: { x: Number(p.x) || 0, y: Number(p.y) || Number(p.size) || 24 } };
		if (p.font) opts.fontName = p.font;
		if (p.color) opts.textColor = solidColor(p.color);
		if (p.name) opts.name = p.name;
		return summary(await doc.createTextLayer(opts), 0);
	},
});

// Exports are a copy: the document isn't renamed, moved or marked saved.
define("document.export", {
	kind: "write", description: "Export a copy of the active document as PNG or JPEG (from the path's extension). The document itself is untouched.",
	params: { path: "string - absolute, ending .png / .jpg", quality: "number - JPEG 1-12 (default 10)" },
	run: async (p) => {
		const doc = requireDocument();
		const ext = String(p.path || "").toLowerCase().split(".").pop();
		const entry = await fileEntry(p.path, true);
		if (ext === "png") await doc.saveAs.png(entry, { compression: 6 }, true);
		else if (ext === "jpg" || ext === "jpeg") await doc.saveAs.jpg(entry, { quality: Math.max(1, Math.min(12, Number(p.quality) || 10)) }, true);
		else throw fail("invalid_params", `Can't export .${ext}`, "Use a path ending .png or .jpg.");
		return { path: p.path, format: ext === "png" ? "png" : "jpg", width: Number(doc.width), height: Number(doc.height) };
	},
});

// Any Photoshop action descriptor. Dialogs never show: a descriptor that
// needs one fails instead of blocking Photoshop.
define("ps.batchplay", {
	kind: "edit", description: "Run Photoshop action descriptors (batchPlay) as one undo step: 'descriptors' is an array of {_obj, ...}. For anything the named commands don't cover.",
	params: { descriptors: "object[] - batchPlay descriptors" },
	run: async (p) => {
		if (!Array.isArray(p.descriptors) || !p.descriptors.length) throw fail("invalid_params", "descriptors must be a non-empty array");
		const descs = p.descriptors.map((d) => ({ ...d, _options: { ...(d._options || {}), dialogOptions: "silent" } }));
		const out = await action.batchPlay(descs, { dialogOptions: "silent" });
		const bad = out.find((r) => r && r._obj === "error");
		if (bad) throw fail("photoshop_error", bad.message || "batchPlay failed", "Check the descriptor; recording the step as an action and copying it as JavaScript shows the exact shape.");
		return out;
	},
});

define("ps.get", {
	kind: "read", description: "Read Photoshop properties with batchPlay 'get' (e.g. {\"_ref\": \"document\", \"_enum\": \"ordinal\", \"_value\": \"targetEnum\"}).",
	params: { target: "object[] - _target references", property: "string - one property (optional)" },
	run: async (p) => {
		const target = Array.isArray(p.target) ? p.target : [p.target];
		const desc = { _obj: "get", _target: p.property ? [{ _property: p.property }, ...target] : target };
		return (await action.batchPlay([desc], { synchronousExecution: false }))[0];
	},
});
