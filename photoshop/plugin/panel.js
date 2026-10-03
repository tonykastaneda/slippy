// The panel around Slippy: counts, the agent's dot, activity bars, the
// connection row and the feed - as in Slippy's Illustrator panel.

(function () {
	const COLORS = { read: "#30b0c7", edit: "#ff9500", error: "#ff3b30" };
	const AGENTS = { Claude: "#D97757", Codex: "#385FF0", ChatGPT: "#10A37F", Cursor: "#9A9A9A", Gemini: "#4C8DF6",
		Copilot: "#8957E5", "VS Code": "#23A9F2", Windsurf: "#09B6A2", Cline: "#F2A93B", Zed: "#5A8DEE", OpenCode: "#B0B0B0" };
	const BARS = 30, BUCKET_MS = 2000, MAX_ROWS = 200;

	class Panel {
		constructor(doc) {
			this.mascot = new globalThis.SlippyMascot(doc.getElementById("mascot"));
			this.countsEl = doc.getElementById("counts");
			this.agentEl = doc.getElementById("agent");
			this.statusEl = doc.getElementById("status");
			this.statusText = doc.getElementById("status-text");
			this.feedEl = doc.getElementById("feed");
			this.emptyEl = doc.getElementById("empty");
			this.barsEl = doc.getElementById("bars");
			this.bars = [];
			for (let i = 0; i < BARS; i++) {
				const b = doc.createElement("div");
				b.className = "bar";
				this.barsEl.appendChild(b);
				this.bars.push(b);
			}
			this.calls = 0; this.errors = 0;
			this.events = [];   // {t, kind}
			setInterval(() => this.drawBars(), 500);
			this.drawBars();
		}

		setStatus(text, on) {
			this.statusText.textContent = text;
			this.statusEl.className = on ? "status on" : "status";
		}

		setAgent(name) {
			if (!name) { this.agentEl.style.display = "none"; return; }
			this.agentEl.style.display = "inline-block";
			this.agentEl.style.background = AGENTS[name] || "#8e8e93";
			this.agentEl.textContent = name.charAt(0);
			this.agentEl.title = `Last call from ${name}`;
		}

		// One call: the line in plain English, read | edit | error.
		call(line, kind, method, ms, agent) {
			this.calls++;
			if (kind === "error") this.errors++;
			this.countsEl.textContent = `${this.calls} call${this.calls === 1 ? "" : "s"}` +
				(this.errors ? ` · ${this.errors} error${this.errors === 1 ? "" : "s"}` : "");
			if (agent) this.setAgent(agent);
			this.events.push({ t: Date.now(), kind });
			this.mascot.callArrived(kind);
			this.addRow(line, kind, `${method} · ${Math.round(ms)} ms`);
			this.drawBars();
		}

		addRow(line, kind, tip) {
			if (this.emptyEl) { this.emptyEl.remove(); this.emptyEl = null; }
			const row = document.createElement("div");
			row.className = "row" + (kind === "error" ? " error" : "");
			row.title = `${line}\n${tip}`;
			const dot = document.createElement("div"); dot.className = "dot"; dot.style.background = COLORS[kind];
			const text = document.createElement("div"); text.className = "line"; text.textContent = line;
			const time = document.createElement("div"); time.className = "time";
			time.textContent = new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
			row.append(dot, text, time);
			this.feedEl.insertBefore(row, this.feedEl.firstChild);
			while (this.feedEl.children.length > MAX_ROWS) this.feedEl.lastChild.remove();
		}

		// The last minute in 2 s buckets, each bar the color of what happened most.
		drawBars() {
			const now = Date.now();
			this.events = this.events.filter((e) => now - e.t < BARS * BUCKET_MS);
			const buckets = Array.from({ length: BARS }, () => ({ n: 0, read: 0, edit: 0, error: 0 }));
			for (const e of this.events) {
				const i = BARS - 1 - Math.floor((now - e.t) / BUCKET_MS);
				if (i >= 0) { buckets[i].n++; buckets[i][e.kind]++; }
			}
			const top = Math.max(4, ...buckets.map((b) => b.n));
			buckets.forEach((b, i) => {
				const bar = this.bars[i];
				bar.style.height = `${Math.max(2, Math.round((b.n / top) * 46))}px`;
				const kind = b.error ? "error" : b.edit >= b.read && b.edit ? "edit" : b.read ? "read" : null;
				bar.style.background = kind ? COLORS[kind] : "";
			});
		}
	}

	globalThis.SlippyPanel = Panel;
	if (typeof module !== "undefined") module.exports = { Panel };
})();
