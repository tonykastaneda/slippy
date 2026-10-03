// The MCP tools agents see: the everyday ones by name, and slippy_find /
// slippy_call / slippy_batch for the whole catalog the panel reports.

import { z } from "zod";
import { errorResult, okResult } from "./errors.js";

export const INSTRUCTIONS =
	"Slippy drives Adobe Photoshop through its panel. Coordinates are document pixels from the top-left, y down. " +
	"Layer ids are numbers from layer_tree / creation results, stable while the document is open. " +
	"Only the everyday commands are tools; find the rest with slippy_find and run them with slippy_call. " +
	"Every call is one step on Edit > Undo; slippy_batch makes several calls one step (saves and exports run on their own). " +
	"ps.batchplay runs any Photoshop action descriptor when nothing else fits.";

const id = z.union([z.number(), z.string()]).describe("layer id (from layer_tree)");
const optId = id.optional();

// name -> [panel method, description, input schema]
const EVERYDAY = {
	slippy_status: ["app.status", "Is Photoshop reachable: versions, open documents, the active one.", {}],
	document_info: ["document.info", "The active document: name, path, size in pixels, resolution, color mode, layer count.", {}],
	layer_tree: ["layer.tree", "Every layer, nested in its groups: id, name, kind, visible, opacity, blend mode, bounds.",
		{ depth: z.number().optional().describe("levels of groups to open (default all)") }],
	layer_get: ["layer.get", "One layer in detail (default: the active layer).", { id: optId }],
	layer_set: ["layer.set", "Change a layer: name, visible, opacity 0-100, blendMode, locked, fillOpacity.",
		{ id: optId, name: z.string().optional(), visible: z.boolean().optional(), opacity: z.number().optional(),
		  blendMode: z.string().optional().describe("normal, multiply, screen, overlay, ..."), locked: z.boolean().optional(),
		  fillOpacity: z.number().optional() }],
	layer_create: ["layer.create", "New pixel layer or group above the active layer.",
		{ kind: z.enum(["pixel", "group"]).optional(), name: z.string().optional() }],
	layer_transform: ["layer.transform", "Move (dx, dy pixels), scale (percent) and / or rotate (degrees) a layer.",
		{ id: optId, dx: z.number().optional(), dy: z.number().optional(), scale: z.union([z.number(), z.array(z.number())]).optional(),
		  rotate: z.number().optional() }],
	text_create: ["text.create", "New point text layer at (x, y) pixels.",
		{ contents: z.string(), x: z.number().optional(), y: z.number().optional(), size: z.number().optional().describe("points"),
		  font: z.string().optional().describe("PostScript name, e.g. Helvetica-Bold"), color: z.string().optional().describe("#RRGGBB"),
		  name: z.string().optional() }],
	document_export: ["document.export", "Export a copy as PNG or JPEG (from the path's extension). Never changes the document.",
		{ path: z.string().describe("absolute path ending .png or .jpg"), quality: z.number().optional().describe("JPEG 1-12") }],
	history_undo: ["history.undo", "Step back in the document's history.", { steps: z.number().optional() }],
};

export function registerTools(server, link, agentOf) {
	for (const [name, [method, description, shape]] of Object.entries(EVERYDAY)) {
		server.registerTool(name, { description, inputSchema: shape }, async (args, extra) => {
			try { return okResult(await link.call(method, args, agentOf(extra))); }
			catch (e) { return errorResult(e); }
		});
	}

	server.registerTool("slippy_find", {
		description: "Search the full command catalog by words (\"layer mask\", \"smart object\", \"export\"): each match's method, what it does, its parameters. No search: the command families.",
		inputSchema: { search: z.string().optional() },
	}, async ({ search }) => {
		try {
			const catalog = await link.catalog();
			if (!search) {
				const families = {};
				for (const c of catalog) families[c.method.split(".")[0]] = (families[c.method.split(".")[0]] || 0) + 1;
				return okResult({ families, run: "slippy_call {method, params}" });
			}
			const words = search.toLowerCase().split(/\s+/).filter(Boolean);
			const hits = catalog.filter((c) => {
				const hay = `${c.method} ${c.description} ${Object.keys(c.params || {}).join(" ")}`.toLowerCase();
				return words.every((w) => hay.includes(w));
			});
			return okResult({ commands: hits, run: "slippy_call {method, params}",
				...(hits.length ? {} : { hint: "nothing matched every word - try fewer or broader words" }) });
		} catch (e) { return errorResult(e); }
	});

	server.registerTool("slippy_call", {
		description: "Run any command by its method name from slippy_find (e.g. \"layer.mask.add\", \"ps.batchplay\").",
		inputSchema: { method: z.string(), params: z.record(z.any()).optional() },
	}, async ({ method, params }, extra) => {
		try { return okResult(await link.call(method, params || {}, agentOf(extra))); }
		catch (e) { return errorResult(e); }
	});

	server.registerTool("slippy_batch", {
		description: "Run several commands back to back as ONE undo step; stops at the first error. Each call is {method, params}. " +
			"Saves and exports split the batch: the edits before them are one step, the write runs on its own.",
		inputSchema: { calls: z.array(z.object({ method: z.string(), params: z.record(z.any()).optional() })) },
	}, async ({ calls }, extra) => {
		try { return okResult(await link.batch(calls, agentOf(extra))); }
		catch (e) { return errorResult(e); }
	});
}
