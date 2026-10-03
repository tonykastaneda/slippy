// The bridge end to end: a real MCP client on one side, a stand-in for the
// Photoshop panel on the other.

import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import WebSocket from "ws";

process.env.SLIPPY_PS_PORT = "47791";
const base = "http://127.0.0.1:47791";
let httpServer, mcp, panel;
const seen = [];

const tool = async (name, args = {}) => {
	const r = await mcp.callTool({ name, arguments: args });
	return { error: !!r.isError, value: JSON.parse(r.content[0].text) };
};

// What the stand-in panel answers.
function answer(m) {
	if (m.type === "catalog") return { ok: true, result: [
		{ method: "layer.tree", kind: "read", description: "Every layer" },
		{ method: "layer.set", kind: "edit", description: "Change a layer" },
		{ method: "smartobject.edit", kind: "edit", description: "Open a smart object" }] };
	if (m.type === "batch") return { ok: true, result: m.calls.map((c) => ({ ok: true, result: { method: c.method } })) };
	if (m.method === "layer.get") return { ok: false, error: { code: "not_found", message: "No layer with id 99", hint: "layer_tree lists ids." } };
	if (m.method === "slow.thing") return null;   // never answers
	return { ok: true, result: { method: m.method, params: m.params } };
}

before(async () => {
	({ httpServer } = await import("../src/server.js"));
	await new Promise((r) => (httpServer.listening ? r() : httpServer.once("listening", r)));
	mcp = new Client({ name: "claude-code", version: "1" });
	await mcp.connect(new StreamableHTTPClientTransport(new URL(base + "/mcp")));
});

after(async () => {
	panel?.close();
	await mcp.close();
	httpServer.close();
	httpServer.closeAllConnections?.();
});

test("without the panel, calls say so in Slippy's error shape", async () => {
	const r = await tool("slippy_status");
	assert.equal(r.error, true);
	assert.equal(r.value.code, "photoshop_not_connected");
	assert.ok(r.value.hint);
});

test("the panel connects and calls reach it with the agent's name", async () => {
	panel = new WebSocket(base.replace("http", "ws") + "/plugin");
	panel.on("message", (d) => {
		const m = JSON.parse(d);
		seen.push(m);
		const reply = answer(m);
		if (reply) panel.send(JSON.stringify({ type: "result", id: m.id, ...reply }));
	});
	await new Promise((r) => panel.once("open", r));
	panel.send(JSON.stringify({ type: "hello", version: "0.1.0", photoshop: "27.0.0" }));
	await new Promise((r) => setTimeout(r, 100));
	const status = await (await fetch(base + "/status")).json();
	assert.equal(status.photoshop.photoshop, "27.0.0");

	const r = await tool("layer_set", { id: 3, opacity: 50 });
	assert.equal(r.error, false);
	assert.deepEqual(r.value.params, { id: 3, opacity: 50 });
	const sent = seen.at(-1);
	assert.equal(sent.method, "layer.set");
	assert.equal(sent.agent, "Claude");
});

test("panel errors come back as {code, message, hint}", async () => {
	const r = await tool("layer_get", { id: 99 });
	assert.equal(r.error, true);
	assert.deepEqual(r.value, { code: "not_found", message: "No layer with id 99", hint: "layer_tree lists ids." });
});

test("slippy_find lists families, and searches the catalog", async () => {
	const families = await tool("slippy_find");
	assert.deepEqual(families.value.families, { layer: 2, smartobject: 1 });
	const found = await tool("slippy_find", { search: "smart object" });
	assert.deepEqual(found.value.commands.map((c) => c.method), ["smartobject.edit"]);
});

test("slippy_call and slippy_batch pass through", async () => {
	const call = await tool("slippy_call", { method: "smartobject.edit", params: { id: 4 } });
	assert.deepEqual(call.value, { method: "smartobject.edit", params: { id: 4 } });
	const batch = await tool("slippy_batch", { calls: [{ method: "layer.set", params: { id: 1, visible: false } }, { method: "layer.tree" }] });
	assert.equal(seen.at(-1).type, "batch");
	assert.equal(batch.value.length, 2);
});

test("a panel that disconnects mid-call fails the call instead of hanging", async () => {
	const pending = tool("slippy_call", { method: "slow.thing" });
	await new Promise((r) => setTimeout(r, 100));
	panel.close();
	const r = await pending;
	assert.equal(r.error, true);
	assert.equal(r.value.code, "photoshop_disconnected");
});
