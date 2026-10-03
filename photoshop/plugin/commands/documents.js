// Documents, history and the app itself.

const { app, action, constants } = require("photoshop");
const { localFileSystem } = require("uxp").storage;
const { define, requireDocument, fail } = require("./registry.js");

const PLUGIN_VERSION = "0.1.0";

function docSummary(d) {
	return {
		id: d.id, name: d.name, path: d.path || null,
		width: Number(d.width), height: Number(d.height), resolution: Number(d.resolution),
		mode: String(d.mode), bitsPerChannel: String(d.bitsPerChannel),
		active: !!app.activeDocument && app.activeDocument.id === d.id,
	};
}

function findDocument(p) {
	if (p.id !== undefined) {
		const d = app.documents.find((x) => x.id === Number(p.id));
		if (!d) throw fail("not_found", `No open document with id ${p.id}`, "document.list shows the open ones.");
		return d;
	}
	if (p.name !== undefined) {
		const d = app.documents.find((x) => x.name === p.name);
		if (!d) throw fail("not_found", `No open document named ${p.name}`, "document.list shows the open ones.");
		return d;
	}
	return requireDocument();
}

async function fileEntry(path, create) {
	if (!path || !path.startsWith("/") && !/^[A-Za-z]:[\\/]/.test(path)) throw fail("invalid_params", "path must be absolute");
	const url = "file:" + path;
	return create ? localFileSystem.createEntryWithUrl(url, { overwrite: true }) : localFileSystem.getEntryWithUrl(url);
}

define("app.status", {
	kind: "read", description: "Photoshop and plug-in versions, open documents, the active one.", params: {},
	run: async () => ({
		photoshop: app.version, plugin: PLUGIN_VERSION,
		documents: app.documents.map((d) => ({ id: d.id, name: d.name })),
		active: app.activeDocument ? app.activeDocument.name : null,
	}),
});

define("document.info", {
	kind: "read", description: "The active document (or 'id'): name, path, size in pixels, resolution, mode, layer count.",
	params: { id: "number (optional)" },
	run: async (p) => {
		const d = findDocument(p);
		return { ...docSummary(d), layerCount: d.layers.length, activeLayers: d.activeLayers.map((l) => l.id),
			coordinates: "pixels from the top-left, y down" };
	},
});

define("document.list", {
	kind: "read", description: "Open documents.", params: {},
	run: async () => app.documents.map(docSummary),
});

define("document.activate", {
	kind: "write", description: "Make a document (by 'id' or 'name') the active one.", params: { id: "number", name: "string" },
	run: async (p) => { const d = findDocument(p); app.activeDocument = d; return docSummary(d); },
});

define("document.new", {
	kind: "write", description: "New document without a dialog: width, height (pixels), resolution, name, fill (white | transparent | background).",
	params: { width: "number", height: "number", resolution: "number (default 72)", name: "string", fill: "white | transparent | background" },
	run: async (p) => {
		const fill = { white: constants.DocumentFill.WHITE, transparent: constants.DocumentFill.TRANSPARENT,
			background: constants.DocumentFill.BACKGROUNDCOLOR }[p.fill || "white"];
		const d = await app.documents.add({ width: p.width || 1000, height: p.height || 1000, resolution: p.resolution || 72,
			name: p.name, fill });
		return docSummary(d);
	},
});

define("document.open", {
	kind: "write", description: "Open a file by absolute path, without a dialog.", params: { path: "string - absolute" },
	run: async (p) => {
		let entry;
		try { entry = await fileEntry(p.path, false); }
		catch (e) { throw fail("not_found", `Can't open ${p.path}`, "Check the path exists and is absolute."); }
		return docSummary(await app.open(entry));
	},
});

define("document.save", {
	kind: "write", description: "Save the active document; with 'path', save as a PSD there (the document moves to it).",
	params: { path: "string - optional absolute .psd path" },
	run: async (p) => {
		const d = requireDocument();
		if (p.path) await d.saveAs.psd(await fileEntry(p.path, true), {}, false);
		else {
			if (!d.path) throw fail("invalid_params", "This document was never saved", "Pass 'path' for the first save.");
			await d.save();
		}
		return { saved: true, path: p.path || d.path };
	},
});

// Never leaves Photoshop to answer "Save changes?" (Slippy's Illustrator
// close once saved because the hidden prompt's default was Save).
define("document.close", {
	kind: "write", description: "Close a document (default: active). Discards changes unless save=true.",
	params: { id: "number", name: "string", save: "boolean (default false)" },
	run: async (p) => {
		const d = findDocument(p);
		const name = d.name;
		await d.close(p.save ? constants.SaveOptions.SAVECHANGES : constants.SaveOptions.DONOTSAVECHANGES);
		return { closed: name, saved: !!p.save };
	},
});

define("history.undo", {
	kind: "write", description: "Step back in the active document's history ('steps', default 1).", params: { steps: "number" },
	run: async (p) => {
		requireDocument();
		const steps = Math.max(1, Math.min(50, Number(p.steps) || 1));
		for (let i = 0; i < steps; i++)
			await action.batchPlay([{ _obj: "select", _target: [{ _ref: "historyState", _enum: "ordinal", _value: "previous" }] }],
				{ dialogOptions: "dontDisplay" });
		return { undone: steps };
	},
});

module.exports = { fileEntry, docSummary };
