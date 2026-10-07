// Slippy's docked panel in Photoshop: the front end. Slippy itself is the
// native plug-in (Slippy.plugin / Slippy.8li) - the server, the commands, the
// History steps; this panel only shows what it reports and passes clicks back.
// They talk through Photoshop's plug-in messaging: the native side sends
// {slippyJSON: "<json>"} here, and this sends the same shape to "Slippy".
// Outside Photoshop (a browser) it plays made-up calls, to preview the look.

(function () {
	const NATIVE = "Slippy";   // the native plug-in's component name (its PiPL)
	const panel = new globalThis.SlippyPanel(document);
	let ps = null;
	try { ps = typeof require === "function" ? require("photoshop") : null; } catch (e) { ps = null; }

	let connection = "";
	let paused = false;
	let heard = false;

	function send(message) {
		try { ps.messaging.sendSDKPluginMessage(NATIVE, { slippyJSON: JSON.stringify(message) }); } catch (e) { /* not loaded yet */ }
	}

	function show(m) {
		heard = true;
		if (m.type === "state") {
			connection = m.connection || "";
			if (m.version) document.getElementById("version").textContent = "v." + m.version.split(".").slice(0, 2).join(".");
			setPaused(!!m.paused);
			panel.setStatus(m.paused ? "Paused - agents' calls are refused" : m.status || "", !!m.listening && !m.paused);
		}
		else if (m.type === "call") {
			const first = String(m.line || m.method).split("\n")[0];
			panel.call(first, m.ok ? (m.edit ? "edit" : "read") : "error", m.method, m.ms || 0, m.agent || "");
		}
	}

	function setPaused(p) {
		paused = p;
		try { require("uxp").entrypoints.getPanel("slippy").menuItems.getItem("pause").checked = p; } catch (e) {}
	}

	document.getElementById("copy").addEventListener("click", async () => {
		const button = document.getElementById("copy");
		try {
			if (!connection) throw new Error("not connected");
			if (navigator.clipboard.setContent) await navigator.clipboard.setContent({ "text/plain": connection });
			else await navigator.clipboard.writeText(connection);
			button.textContent = "Copied";
		} catch (e) { button.textContent = "Not yet"; }
		setTimeout(() => (button.textContent = "Copy connection"), 1500);
	});

	if (!ps || !ps.messaging) { preview(); return; }

	ps.messaging.addSDKMessagingListener((o) => {
		let m = null;
		try { m = JSON.parse((o && o.message && o.message.slippyJSON) || "null"); } catch (e) { return; }
		if (m) show(m);
	});
	try {
		require("uxp").entrypoints.setup({
			panels: { slippy: { invokeMenu(id) { if (id === "pause") send({ type: "pause", paused: !paused }); } } },
		});
	} catch (e) {}

	// Say hello until the native plug-in answers (it may still be starting).
	panel.setStatus("Looking for Slippy…", false);
	const hello = () => {
		send({ type: "hello" });
		if (!heard) {
			panel.setStatus("Slippy's plug-in isn't running - is Slippy.plugin installed?", false);
			setTimeout(hello, 3000);
		}
	};
	setTimeout(hello, 300);

	// ---- browser preview
	function preview() {
		show({ type: "state", status: "Preview · not running in Photoshop", listening: false, version: "1.2.1" });
		const script = [
			["document.open", "Opened “ballot.jpeg”", true, false],
			["layer.tree", "Looked over the layers", true, false],
			["select.ellipse", "Selected an ellipse", true, true],
			["edit.fill", "Filled the selection", true, true],
			["filter.blur", "Blurred 22 px", true, true],
			["layer.get", "Couldn't find that layer", false, false],
			["document.export", "Exported “ballot.jpg”", true, true],
		];
		let i = 0;
		const next = () => {
			const [method, line, ok, edit] = script[i++ % script.length];
			show({ type: "call", method, line, ok, edit, ms: 20 + Math.random() * 180, agent: i % 5 === 0 ? "Codex" : "Claude" });
			setTimeout(next, i % script.length === 0 ? 9000 : 600 + Math.random() * 1400);
		};
		setTimeout(next, 2000);
	}
})();
