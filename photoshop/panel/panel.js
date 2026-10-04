// The panel around Slippy: counts, the agent's logo, a bar for each command
// group, the connection row and the feed - as in Slippy's Illustrator panel.

(function () {
	const COLORS = { read: "#40c8e0", edit: "#ff9f0a", error: "#ff453a" };
	const AGENTS = { Claude: "#D97757", Codex: "#385FF0", ChatGPT: "#10A37F", Cursor: "#9A9A9A", Gemini: "#4C8DF6",
		Copilot: "#8957E5", "VS Code": "#23A9F2", Windsurf: "#09B6A2", Cline: "#F2A93B", Zed: "#5A8DEE", OpenCode: "#B0B0B0" };
	// One bar per command group (a method's first word); the last takes the rest.
	const GROUPS = ["document", "layer", "select", "edit", "filter", "text", "ps", "history", "commands", "app"];
	const MAX_ROWS = 200;
	const LOGOS = { Claude: "Claude", Codex: "Codex", ChatGPT: "ChatGPT", Cursor: "Cursor", Gemini: "Gemini", Copilot: "Copilot",
		"VS Code": "VS_Code", Windsurf: "Windsurf", Cline: "Cline", OpenCode: "OpenCode", Grok: "Grok", Kimi: "Kimi", Qwen: "Qwen",
		Goose: "Goose", "Roo Code": "Roo_Code" };

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
			this.bars = GROUPS.map((g) => {
				const slot = doc.createElement("div");
				slot.className = "slot";
				const bar = doc.createElement("div");
				bar.className = "bar";
				const label = doc.createElement("div");
				label.className = "bar-label";
				label.textContent = g.slice(0, 3);
				slot.append(bar, label);
				this.barsEl.appendChild(slot);
				return { bar, level: 0, kind: null };
			});
			this.calls = 0; this.errors = 0;
			// Each bar jumps on its group's calls and settles back, like the native panel's.
			setInterval(() => this.drawBars(), 50);
		}

		setStatus(text, on) {
			this.statusText.textContent = text;
			this.statusEl.className = on ? "status on" : "status";
		}

		setAgent(name) {
			if (!name) { this.agentEl.style.display = "none"; return; }
			this.agentEl.style.display = "inline-block";
			this.agentEl.title = `Last call from ${name}`;
			this.agentEl.textContent = "";
			const letter = () => {
				this.agentEl.className = "agent letter";
				this.agentEl.style.background = AGENTS[name] || "#8e8e93";
				this.agentEl.textContent = name.charAt(0);
			};
			if (!LOGOS[name]) { letter(); return; }
			this.agentEl.className = "agent";
			this.agentEl.style.background = "";
			const img = document.createElement("img");
			img.src = `agents/${LOGOS[name]}.svg`;
			img.onerror = letter;
			this.agentEl.appendChild(img);
		}

		// One call: the line in plain English, read | edit | error.
		call(line, kind, method, ms, agent) {
			this.calls++;
			if (kind === "error") this.errors++;
			this.countsEl.textContent = `${this.calls} call${this.calls === 1 ? "" : "s"} • ${this.errors} error${this.errors === 1 ? "" : "s"}`;
			if (agent) this.setAgent(agent);
			const group = method.split(".")[0];
			const b = this.bars[GROUPS.indexOf(group) >= 0 ? GROUPS.indexOf(group) : GROUPS.length - 1];
			b.level = Math.min(1, b.level + 0.6);
			b.kind = kind;
			this.mascot.callArrived(kind);
			this.addRow(line, kind, `${method} · ${Math.round(ms)} ms`);
		}

		addRow(line, kind, tip) {
			if (this.emptyEl) { this.emptyEl.remove(); this.emptyEl = null; }
			const row = document.createElement("div");
			row.className = "row" + (kind === "error" ? " error" : "");
			row.title = `${line}\n${tip}`;
			const dot = document.createElement("div"); dot.className = "dot"; dot.style.background = COLORS[kind];
			const text = document.createElement("div"); text.className = "line"; text.textContent = line;
			const time = document.createElement("div"); time.className = "time";
			time.textContent = new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }).replace(" ", "\u2009");
			row.append(dot, text, time);
			this.feedEl.insertBefore(row, this.feedEl.firstChild);
			while (this.feedEl.children.length > MAX_ROWS) this.feedEl.lastChild.remove();
		}

		drawBars() {
			for (const b of this.bars) {
				b.level *= 0.94;
				if (b.level < 0.01) { b.level = 0; b.kind = null; }
				b.bar.style.height = `${(3 + b.level * 29).toFixed(1)}px`;   // 3 at rest, up to 32
				const lit = b.kind && b.level > 0.02;
				b.bar.style.background = lit ? COLORS[b.kind] : "";
				b.bar.style.opacity = lit ? "1" : "";
			}
		}
	}

	globalThis.SlippyPanel = Panel;
	if (typeof module !== "undefined") module.exports = { Panel };
})();
