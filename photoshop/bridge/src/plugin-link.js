// The Photoshop panel's connection: one WebSocket at /plugin. Calls go out as
// {type, id, ...}; the panel answers {type: "result", id, ok, result | error}.

import { WebSocketServer } from "ws";
import { fail } from "./errors.js";

const CALL_TIMEOUT_MS = Number(process.env.SLIPPY_PS_TIMEOUT_MS || 60000);

export class PluginLink {
	constructor() {
		this.socket = null;
		this.hello = null;          // what the panel said about itself when it connected
		this.pending = new Map();   // id -> {resolve, reject, timer}
		this.nextId = 1;
		this.wss = new WebSocketServer({ noServer: true });
		this.wss.on("connection", (ws) => this.#attach(ws));
	}

	// Called from the HTTP server for upgrade requests to /plugin.
	upgrade(req, socket, head) {
		this.wss.handleUpgrade(req, socket, head, (ws) => this.wss.emit("connection", ws, req));
	}

	get connected() { return !!this.socket && this.socket.readyState === this.socket.OPEN; }

	#attach(ws) {
		// One panel at a time: the newest connection wins (a reloaded panel reconnects).
		if (this.socket && this.socket !== ws) this.socket.close(4000, "replaced by a newer panel connection");
		this.socket = ws;
		ws.on("message", (data) => this.#receive(data));
		ws.on("close", () => {
			if (this.socket !== ws) return;
			this.socket = null;
			this.hello = null;
			for (const [id, p] of this.pending) {
				clearTimeout(p.timer);
				p.reject(fail("photoshop_disconnected", "Photoshop's Slippy panel disconnected during the call",
					"The call may or may not have completed - check the document before retrying."));
				this.pending.delete(id);
			}
		});
		console.log("[bridge] Photoshop panel connected");
	}

	#receive(data) {
		let msg;
		try { msg = JSON.parse(String(data)); } catch { return; }
		if (msg.type === "hello") { this.hello = msg; console.log(`[bridge] panel: Photoshop ${msg.photoshop}, plug-in ${msg.version}`); return; }
		if (msg.type !== "result") return;
		const p = this.pending.get(msg.id);
		if (!p) return;
		this.pending.delete(msg.id);
		clearTimeout(p.timer);
		if (msg.ok) p.resolve(msg.result);
		else p.reject(fail(msg.error?.code || "photoshop_error", msg.error?.message || "Photoshop reported an error", msg.error?.hint));
	}

	// Send one request to the panel and wait for its answer.
	request(payload, timeoutMs = CALL_TIMEOUT_MS) {
		if (!this.connected) {
			return Promise.reject(fail("photoshop_not_connected", "Slippy's Photoshop panel isn't connected",
				"Open Photoshop and its Slippy panel (Plugins > Slippy). It connects to this bridge on its own."));
		}
		const id = this.nextId++;
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => {
				this.pending.delete(id);
				reject(fail("timeout", `Photoshop didn't answer within ${Math.round(timeoutMs / 1000)} s`,
					"Photoshop may be busy (a dialog or long operation). The call may still complete - check before retrying."));
			}, timeoutMs);
			this.pending.set(id, { resolve, reject, timer });
			this.socket.send(JSON.stringify({ ...payload, id }));
		});
	}

	call(method, params, agent) { return this.request({ type: "call", method, params: params || {}, agent }); }
	batch(calls, agent) { return this.request({ type: "batch", calls, agent }, CALL_TIMEOUT_MS * 2); }
	catalog() { return this.request({ type: "catalog" }); }
}
