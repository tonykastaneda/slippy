#!/usr/bin/env node
// Slippy for Photoshop's bridge. Agents connect over MCP (Streamable HTTP) at
// http://127.0.0.1:47710/mcp; the Photoshop panel connects in over a WebSocket
// at ws://127.0.0.1:47710/plugin and runs the calls. Local only.

import http from "node:http";
import { randomUUID } from "node:crypto";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { isInitializeRequest } from "@modelcontextprotocol/sdk/types.js";
import { PluginLink } from "./plugin-link.js";
import { registerTools, INSTRUCTIONS } from "./tools.js";

const VERSION = "0.1.0";
const HOST = "127.0.0.1";
const PORT = Number(process.env.SLIPPY_PS_PORT || 47710);

const link = new PluginLink();
const sessions = new Map();   // MCP session id -> { transport, agent }

// Who's calling, as the panel shows it ("Claude", "Codex"...): the MCP client's name.
function agentName(clientInfo) {
	const n = (clientInfo?.name || "").toLowerCase();
	const known = [["claude", "Claude"], ["codex", "Codex"], ["cursor", "Cursor"], ["gemini", "Gemini"], ["copilot", "Copilot"],
		["windsurf", "Windsurf"], ["cline", "Cline"], ["vscode", "VS Code"], ["zed", "Zed"], ["opencode", "OpenCode"]];
	for (const [k, v] of known) if (n.includes(k)) return v;
	return clientInfo?.name || "";
}

function newSession() {
	const server = new McpServer({ name: "slippy-photoshop", version: VERSION }, { instructions: INSTRUCTIONS });
	const state = { agent: "" };
	registerTools(server, link, () => state.agent);
	const transport = new StreamableHTTPServerTransport({
		sessionIdGenerator: () => randomUUID(),
		onsessioninitialized: (sid) => sessions.set(sid, { transport, state }),
	});
	transport.onclose = () => { if (transport.sessionId) sessions.delete(transport.sessionId); };
	server.server.oninitialized = () => { state.agent = agentName(server.server.getClientVersion()); };
	server.connect(transport);
	return transport;
}

async function readBody(req) {
	const chunks = [];
	for await (const c of req) chunks.push(c);
	const text = Buffer.concat(chunks).toString("utf8");
	return text ? JSON.parse(text) : undefined;
}

const httpServer = http.createServer(async (req, res) => {
	const url = new URL(req.url, `http://${req.headers.host}`);
	try {
		if (url.pathname === "/" || url.pathname === "/status") {
			res.writeHead(200, { "content-type": "application/json" });
			res.end(JSON.stringify({ name: "Slippy for Photoshop", version: VERSION, mcp: `http://${HOST}:${PORT}/mcp`,
				photoshop: link.connected ? (link.hello || { connected: true }) : null }));
			return;
		}
		if (url.pathname !== "/mcp") { res.writeHead(404).end(); return; }
		const sid = req.headers["mcp-session-id"];
		if (req.method === "POST") {
			const body = await readBody(req);
			let transport = sid && sessions.get(sid)?.transport;
			if (!transport) {
				if (sid || !isInitializeRequest(body)) {
					res.writeHead(400, { "content-type": "application/json" });
					res.end(JSON.stringify({ jsonrpc: "2.0", error: { code: -32000, message: "No session - send initialize first" }, id: null }));
					return;
				}
				transport = newSession();
			}
			await transport.handleRequest(req, res, body);
			return;
		}
		if (req.method === "GET" || req.method === "DELETE") {
			const transport = sid && sessions.get(sid)?.transport;
			if (!transport) { res.writeHead(400).end("No session"); return; }
			await transport.handleRequest(req, res);
			return;
		}
		res.writeHead(405).end();
	} catch (e) {
		console.error("[bridge]", e);
		if (!res.headersSent) res.writeHead(500).end(String(e?.message || e));
	}
});

httpServer.on("upgrade", (req, socket, head) => {
	if (new URL(req.url, `http://${req.headers.host}`).pathname === "/plugin") link.upgrade(req, socket, head);
	else socket.destroy();
});

httpServer.listen(PORT, HOST, () => {
	console.log(`[bridge] Slippy for Photoshop ${VERSION}`);
	console.log(`[bridge] agents:    http://${HOST}:${PORT}/mcp`);
	console.log(`[bridge] Photoshop: ws://${HOST}:${PORT}/plugin (waiting for the panel)`);
	console.log(`[bridge] add to Claude Code: claude mcp add --transport http slippy-photoshop http://${HOST}:${PORT}/mcp`);
});

export { httpServer, link };
