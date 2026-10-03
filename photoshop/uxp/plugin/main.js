// Starts the panel. In Photoshop: loads the commands and keeps a WebSocket open
// to the local bridge (UXP can't listen, so the panel dials out), running what
// the bridge sends and answering on the same socket. In a plain browser there's
// no Photoshop, so it plays some made-up calls to preview the panel.

(function () {
	const BRIDGE = "ws://127.0.0.1:47710/plugin";
	const CONNECT_COMMAND = "claude mcp add --transport http slippy-photoshop http://127.0.0.1:47710/mcp";
	const RETRY_MS = 3000;

	const panel = new globalThis.SlippyPanel(document);
	const narrate = globalThis.SlippyNarrate;
	let inPhotoshop = false;
	try { inPhotoshop = typeof require === "function" && !!require("photoshop"); } catch (e) { inPhotoshop = false; }

	document.getElementById("copy").addEventListener("click", async () => {
		const button = document.getElementById("copy");
		try {
			if (navigator.clipboard && navigator.clipboard.setContent) await navigator.clipboard.setContent({ "text/plain": CONNECT_COMMAND });
			else await navigator.clipboard.writeText(CONNECT_COMMAND);
			button.textContent = "Copied";
		} catch (e) { button.textContent = "Couldn't copy"; }
		setTimeout(() => (button.textContent = "Copy connection"), 1500);
	});

	function show(method, params, result, error, ms, agent) {
		const { line, kind } = narrate(method, params, result, error);
		panel.call(line, kind, method, ms, agent);
	}

	if (!inPhotoshop) { preview(); return; }

	const registry = require("./commands/registry.js");
	require("./commands/documents.js");
	require("./commands/layers.js");
	require("./commands/text-export-raw.js");
	const { app } = require("photoshop");
	let PLUGIN_VERSION = "0.1.0";
	try { PLUGIN_VERSION = require("uxp").versions.plugin || PLUGIN_VERSION; } catch (e) {}
	document.getElementById("version").textContent = "v." + PLUGIN_VERSION.split(".").slice(0, 2).join(".");

	const errorJSON = (e) => registry.translate(e).toJSON();

	async function handle(msg) {
		const started = Date.now();
		if (msg.type === "catalog") return { ok: true, result: registry.catalog() };
		if (msg.type === "call") {
			try {
				const result = await registry.call(msg.method, msg.params);
				show(msg.method, msg.params, result, null, Date.now() - started, msg.agent);
				return { ok: true, result: result === undefined ? null : result };
			} catch (e) {
				show(msg.method, msg.params, null, e, Date.now() - started, msg.agent);
				return { ok: false, error: errorJSON(e) };
			}
		}
		if (msg.type === "batch") {
			const calls = Array.isArray(msg.calls) ? msg.calls : [];
			const results = await registry.batch(calls);
			const ms = (Date.now() - started) / Math.max(1, calls.length);
			results.forEach((r, i) => {
				if (r.error && r.error.code === "skipped") return;
				show(calls[i].method, calls[i].params, r.result, r.ok ? null : r.error, ms, msg.agent);
			});
			return { ok: true, result: results };
		}
		return { ok: false, error: { code: "invalid_params", message: `Unknown message type ${msg.type}` } };
	}

	let socket = null;
	function connect() {
		if (!socket) panel.setStatus("Looking for the Slippy bridge…", false);
		try { socket = new WebSocket(BRIDGE); } catch (e) {
			panel.setStatus("Can't reach the bridge: " + ((e && e.message) || e), false);
			console.error("[slippy] WebSocket failed", e);
			setTimeout(connect, RETRY_MS); return;
		}
		socket.onopen = () => {
			panel.setStatus("Connected to the bridge · 127.0.0.1:47710", true);
			socket.send(JSON.stringify({ type: "hello", version: PLUGIN_VERSION, photoshop: app.version || (require("uxp").host && require("uxp").host.version) }));
		};
		socket.onmessage = async (event) => {
			let msg;
			try { msg = JSON.parse(event.data); } catch (e) { return; }
			const reply = await handle(msg);
			if (socket && socket.readyState === 1) socket.send(JSON.stringify({ type: "result", id: msg.id, ...reply }));
		};
		socket.onclose = (event) => {
			socket = null;
			const why = event && event.code && event.code !== 1000 ? ` (closed ${event.code}${event.reason ? ": " + event.reason : ""})` : "";
			panel.setStatus("Bridge not running · cd bridge && npm start" + why, false);
			setTimeout(connect, RETRY_MS);
		};
		socket.onerror = (e) => console.error("[slippy] WebSocket error", (e && e.message) || e);   // onclose follows and retries
	}
	connect();

	// ---- browser preview
	function preview() {
		panel.setStatus("Preview · not running in Photoshop", false);
		const script = [
			["document.info", {}, { name: "poster.psd" }],
			["layer.tree", {}, null],
			["layer.create", { name: "Background glow" }, { name: "Background glow" }],
			["layer.set", { opacity: 60 }, { name: "Background glow" }],
			["text.create", { contents: "Summer Sale" }, null],
			["layer.transform", { dx: 40, scale: 120 }, { name: "Summer Sale" }],
			["layer.get", { id: 99 }, null, { message: "No layer with id 99" }],
			["document.export", { path: "/Users/me/Desktop/poster.png" }, null],
		];
		let i = 0;
		const next = () => {
			const [method, params, result, error] = script[i++ % script.length];
			show(method, params, result, error || null, 20 + Math.random() * 180, i % 5 === 0 ? "Codex" : "Claude");
			setTimeout(next, i % script.length === 0 ? 9000 : 600 + Math.random() * 1400);
		};
		setTimeout(next, 2500);
	}
})();
