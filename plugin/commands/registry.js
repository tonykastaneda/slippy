// The command table and how calls run. Every command is one of:
//   read  - looks, changes nothing; runs straight away
//   edit  - changes the document; runs in executeAsModal as ONE history state
//           named after what it did ("Slippy: Renamed layer to Logo")
//   write - saves / exports / opens / closes; runs on its own, outside any
//           history grouping (Slippy's rule: a write never shares a step)
// A batch is one history state for its edits, split around writes.

const { app, core } = require("photoshop");
const { narrate } = require("../narrate.js");

class SlippyError extends Error {
	constructor(code, message, hint) { super(message); this.code = code; this.hint = hint; }
	toJSON() { return { code: this.code, message: this.message, ...(this.hint ? { hint: this.hint } : {}) }; }
}
const fail = (code, message, hint) => new SlippyError(code, message, hint);

const table = Object.create(null);

// spec: { kind: "read"|"edit"|"write", description, params: {name: "doc"}, run(params) }
function define(method, spec) { table[method] = spec; }

function requireDocument() {
	const doc = app.activeDocument;
	if (!doc) throw fail("no_document", "No document is open", "Open or create one first (document.open / document.new).");
	return doc;
}

// Photoshop's errors, in Slippy's one shape.
function translate(e) {
	if (e instanceof SlippyError) return e;
	const message = String((e && e.message) || e);
	if (/modal/i.test(message))
		return fail("photoshop_busy", "Photoshop is busy - a dialog or another operation has it", "Finish or close it in Photoshop, then retry.");
	if (/not available|is not currently available/i.test(message))
		return fail("command_unavailable", message, "Photoshop can't do that in the current state (selection, layer kind, mode).");
	return fail("photoshop_error", message);
}

function lookup(method) {
	const spec = table[method];
	if (!spec) throw fail("unknown_method", `No command ${method}`, "slippy_find lists every command.");
	return spec;
}

// Run fn as one modal step; with history, as one named history state.
async function modal(name, fn, history) {
	return core.executeAsModal(async (ctx) => {
		const doc = app.activeDocument;
		let suspension = null;
		if (history && doc) suspension = await ctx.hostControl.suspendHistory({ documentID: doc.id, name });
		try { return await fn(ctx); }
		finally { if (suspension !== null) await ctx.hostControl.resumeHistory(suspension, true); }   // keep what ran, even on an error
	}, { commandName: name, interactive: false });
}

function historyName(method, params) {
	const line = narrate(method, params, null).line;
	return `Slippy: ${line.length > 60 ? line.slice(0, 57) + "…" : line}`;
}

// One call.
async function call(method, params) {
	const spec = lookup(method);
	try {
		if (spec.kind === "read") return await spec.run(params || {});
		return await modal(historyName(method, params), () => spec.run(params || {}), spec.kind === "edit");
	} catch (e) { throw translate(e); }
}

// A batch: consecutive edits share one history state; each write runs alone.
// Stops at the first error; the rest come back "skipped".
async function batch(calls) {
	const results = [];
	let i = 0, stopped = false;
	while (i < calls.length) {
		if (stopped) { results.push({ ok: false, error: { code: "skipped", message: "skipped: an earlier call in the batch failed" } }); i++; continue; }
		const spec = table[calls[i].method];
		if (!spec || spec.kind !== "edit") {
			try { results.push({ ok: true, result: await call(calls[i].method, calls[i].params) }); }
			catch (e) { results.push({ ok: false, error: translate(e).toJSON() }); stopped = true; }
			i++;
			continue;
		}
		let j = i;
		while (j < calls.length && table[calls[j].method] && table[calls[j].method].kind === "edit") j++;
		const group = calls.slice(i, j);
		const name = group.length === 1 ? historyName(group[0].method, group[0].params) : `Slippy: ${group.length} changes`;
		const out = [];
		try {
			await modal(name, async () => {
				for (const c of group) {
					try { out.push({ ok: true, result: await table[c.method].run(c.params || {}) }); }
					catch (e) { out.push({ ok: false, error: translate(e).toJSON() }); throw e; }
				}
			}, true);
		} catch (e) {
			if (!out.length || out[out.length - 1].ok) out.push({ ok: false, error: translate(e).toJSON() });
			stopped = true;
		}
		results.push(...out);
		i += out.length;   // after a failure, the group's remaining calls come back "skipped" above
	}
	return results;
}

function catalog() {
	return Object.keys(table).sort().map((method) => ({
		method, kind: table[method].kind, description: table[method].description, params: table[method].params || {},
	}));
}

module.exports = { define, call, batch, catalog, lookup, requireDocument, fail, translate, SlippyError };
