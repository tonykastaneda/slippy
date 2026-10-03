// Layers and groups.

const { app, constants } = require("photoshop");
const { define, requireDocument, fail } = require("./registry.js");

function bounds(b) {
	return b ? { left: Number(b.left), top: Number(b.top), right: Number(b.right), bottom: Number(b.bottom),
		width: Number(b.right) - Number(b.left), height: Number(b.bottom) - Number(b.top) } : null;
}

function summary(l, depth) {
	const v = {
		id: l.id, name: l.name, kind: String(l.kind), visible: l.visible, opacity: l.opacity, blendMode: String(l.blendMode),
		locked: !!l.allLocked, bounds: bounds(l.bounds),
	};
	if (l.layers && l.layers.length !== undefined) {
		v.childCount = l.layers.length;
		if (depth !== 0) v.children = l.layers.map((c) => summary(c, depth - 1));
	}
	return v;
}

function allLayers(list, out = []) {
	for (const l of list) { out.push(l); if (l.layers) allLayers(l.layers, out); }
	return out;
}

function findLayer(p, doc = requireDocument()) {
	if (p.id === undefined || p.id === null) {
		const active = doc.activeLayers;
		if (!active.length) throw fail("invalid_params", "No layer given and none is active", "Pass 'id' (layer_tree lists them).");
		return active[0];
	}
	const l = allLayers(doc.layers).find((x) => x.id === Number(p.id));
	if (!l) throw fail("not_found", `No layer with id ${p.id} in ${doc.name}`, "layer_tree lists the layers and their ids.");
	return l;
}

function blendMode(name) {
	const want = String(name).toLowerCase().replace(/[\s_-]/g, "");
	for (const k of Object.keys(constants.BlendMode))
		if (k.toLowerCase().replace(/_/g, "") === want || String(constants.BlendMode[k]).toLowerCase().replace(/[\s_-]/g, "") === want)
			return constants.BlendMode[k];
	throw fail("invalid_params", `Unknown blend mode ${name}`, "e.g. normal, multiply, screen, overlay, softLight, darken, lighten.");
}

define("layer.tree", {
	kind: "read", description: "Every layer, nested in its groups: id, name, kind, visible, opacity, blend mode, bounds (pixels).",
	params: { depth: "number - levels of groups to open (default all)" },
	run: async (p) => {
		const doc = requireDocument();
		const depth = p.depth === undefined ? -1 : Number(p.depth);
		return { document: doc.name, layers: doc.layers.map((l) => summary(l, depth)) };
	},
});

define("layer.get", {
	kind: "read", description: "One layer in detail (default: the active layer).", params: { id: "number" },
	run: async (p) => {
		const l = findLayer(p);
		const v = summary(l, 0);
		v.fillOpacity = l.fillOpacity;
		v.parent = l.parent ? l.parent.id : null;
		if (String(l.kind) === "text" && l.textItem) {
			try { v.text = { contents: l.textItem.contents, size: Number(l.textItem.characterStyle.size), font: l.textItem.characterStyle.font }; } catch (e) { /* older hosts */ }
		}
		return v;
	},
});

define("layer.select", {
	kind: "write", description: "Make layers active ('id' or 'ids'; add=true keeps the current ones).", params: { id: "number", ids: "number[]", add: "boolean" },
	run: async (p) => {
		const doc = requireDocument();
		const ids = p.ids || (p.id !== undefined ? [p.id] : []);
		const layers = ids.map((id) => findLayer({ id }, doc));
		if (!p.add) for (const l of doc.activeLayers) l.selected = false;
		for (const l of layers) l.selected = true;
		return doc.activeLayers.map((l) => summary(l, 0));
	},
});

define("layer.set", {
	kind: "edit", description: "Change a layer (default: active): name, visible, opacity 0-100, fillOpacity, blendMode, locked.",
	params: { id: "number", name: "string", visible: "boolean", opacity: "number", fillOpacity: "number", blendMode: "string", locked: "boolean" },
	run: async (p) => {
		const l = findLayer(p);
		if (p.name !== undefined) l.name = String(p.name);
		if (p.visible !== undefined) l.visible = !!p.visible;
		if (p.opacity !== undefined) l.opacity = Math.max(0, Math.min(100, Number(p.opacity)));
		if (p.fillOpacity !== undefined) l.fillOpacity = Math.max(0, Math.min(100, Number(p.fillOpacity)));
		if (p.blendMode !== undefined) l.blendMode = blendMode(p.blendMode);
		if (p.locked !== undefined) l.allLocked = !!p.locked;
		return summary(l, 0);
	},
});

define("layer.create", {
	kind: "edit", description: "New pixel layer (default) or group above the active layer.", params: { kind: "pixel | group", name: "string" },
	run: async (p) => {
		const doc = requireDocument();
		const l = p.kind === "group" ? await doc.createLayerGroup({ name: p.name || "Group" }) : await doc.createLayer({ name: p.name || "Layer" });
		return summary(l, 0);
	},
});

define("layer.delete", {
	kind: "edit", description: "Delete layers ('id' or 'ids').", params: { id: "number", ids: "number[]" },
	run: async (p) => {
		const doc = requireDocument();
		const ids = p.ids || [p.id];
		const layers = ids.map((id) => findLayer({ id }, doc));
		for (const l of layers) l.delete();
		return { deleted: layers.length };
	},
});

define("layer.duplicate", {
	kind: "edit", description: "Duplicate a layer (default: active); returns the copy.", params: { id: "number", name: "string" },
	run: async (p) => {
		const copy = await findLayer(p).duplicate();
		if (p.name) copy.name = p.name;
		return summary(copy, 0);
	},
});

define("layer.transform", {
	kind: "edit", description: "Move (dx, dy pixels), scale (percent, or [x, y]) and / or rotate (degrees, clockwise) a layer about its center.",
	params: { id: "number", dx: "number", dy: "number", scale: "number | [x, y] percent", rotate: "number - degrees" },
	run: async (p) => {
		const l = findLayer(p);
		if (p.scale !== undefined) {
			const [sx, sy] = Array.isArray(p.scale) ? p.scale : [p.scale, p.scale];
			await l.scale(Number(sx), Number(sy), constants.AnchorPosition.MIDDLECENTER);
		}
		if (p.rotate) await l.rotate(Number(p.rotate), constants.AnchorPosition.MIDDLECENTER);
		if (p.dx || p.dy) await l.translate(Number(p.dx) || 0, Number(p.dy) || 0);
		return summary(l, 0);
	},
});

define("layer.move", {
	kind: "edit", description: "Restack a layer: right 'above' or 'below' another layer, or 'into' a group (at its top).",
	params: { id: "number", above: "number", below: "number", into: "number - group id" },
	run: async (p) => {
		const doc = requireDocument();
		const l = findLayer(p, doc);
		if (p.into !== undefined) l.move(findLayer({ id: p.into }, doc), constants.ElementPlacement.PLACEINSIDE);
		else if (p.above !== undefined) l.move(findLayer({ id: p.above }, doc), constants.ElementPlacement.PLACEBEFORE);
		else if (p.below !== undefined) l.move(findLayer({ id: p.below }, doc), constants.ElementPlacement.PLACEAFTER);
		else throw fail("invalid_params", "Pass 'above', 'below' or 'into'");
		return summary(l, 0);
	},
});

module.exports = { findLayer, summary, allLayers };
