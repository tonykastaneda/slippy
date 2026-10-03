// Slippy, the mascot - a frog's face: a green head with two eye bumps, the
// eyes in the bumps, no mouth. Asleep (‿ ‿, Z's) when nothing's happening,
// happy (^ ^, a hop) on the first call, awake (solid eyes, looking around -
// faster when busy) while working, ouch (> <, red flash, shake) on an error,
// and he dozes off again after a quiet spell. Plain DOM and a small tween loop,
// so it runs the same in Photoshop's UXP and in a browser.

(function () {
	const SLEEP_AFTER = 6000;   // quiet ms before dozing
	const COLORS = { read: "#30b0c7", edit: "#ff9500", error: "#ff3b30" };
	const SLIPPY = "#92f607";

	function el(cls, parent, html) {
		const d = document.createElement("div");
		d.className = cls;
		if (html) d.innerHTML = html;
		parent.appendChild(d);
		return d;
	}

	class Mascot {
		constructor(box) {
			this.box = box;
			this.frog = el("frog", box);
			this.head = el("head", this.frog);
			this.bumps = [el("bump l", this.frog), el("bump r", this.frog)];
			this.eyes = this.bumps.map((b) => el("eye", b));
			this.zs = [[16, 58, 11], [8, 72, 14], [2, 88, 18]].map(([x, y, size]) => {
				const z = el("z", box, "Z");
				z.style.left = x + "px"; z.style.bottom = y + "px"; z.style.fontSize = size + "px";
				return z;
			});
			this.face = null;
			this.mood = "asleep";
			this.lastCall = 0;
			this.busy = 0;          // 0..1, recent call rate
			this.anims = [];
			this.state = { hopY: 0, shakeX: 0, tilt: 0, sqX: 1, sqY: 1, lookX: 0 };
			this.setFace("sleep");
			setInterval(() => this.tick(), 33);
		}

		setFace(face) {
			if (face === this.face) return;
			this.face = face;
			this.eyes.forEach((e, i) => {
				const html = face === "awake" ? '<div class="awake"></div>' : face === "happy" ? '<div class="happy"></div>'
					: face === "ouch" ? `<div class="ouch">${i === 0 ? "&gt;" : "&lt;"}</div>` : '<div class="sleep"></div>';
				e.innerHTML = html;
			});
		}

		// A tween: set(t) from 0 to 1 over ms, eased.
		play(ms, set, done) {
			this.anims.push({ start: Date.now(), ms, set, done });
		}

		hop(height = 9) {
			this.play(420, (t) => {
				this.state.hopY = -height * Math.sin(Math.PI * Math.min(1, t / 0.75));
				const k = 0.08 * Math.sin(Math.PI * t * 2);
				this.state.sqX = 1 + (t < 0.15 || t > 0.8 ? k : -k * 0.6);
				this.state.sqY = 1 - (t < 0.15 || t > 0.8 ? k : -k * 0.6);
			}, () => { this.state.hopY = 0; this.state.sqX = this.state.sqY = 1; });
		}

		shake() {
			this.play(420, (t) => { this.state.shakeX = 5 * Math.sin(t * Math.PI * 5) * (1 - t); },
				() => { this.state.shakeX = 0; });
		}

		flash() {
			this.head.style.background = COLORS.error;
			this.bumps.forEach((b) => (b.style.background = COLORS.error));
			setTimeout(() => {
				this.head.style.background = SLIPPY;
				this.bumps.forEach((b) => (b.style.background = SLIPPY));
			}, 260);
		}

		ripple(color) {
			const r = el("ripple", this.box);
			r.style.borderColor = color;
			this.play(700, (t) => {
				const s = 1 + 0.55 * t;
				r.style.transform = `scale(${s.toFixed(3)})`;
				r.style.opacity = String((1 - t) * 0.9);
			}, () => r.remove());
		}

		// A call happened. kind: read | edit | error.
		callArrived(kind) {
			const now = Date.now();
			this.busy = Math.min(1, this.busy * 0.8 + 0.25);
			this.lastCall = now;
			this.ripple(COLORS[kind] || COLORS.read);
			if (this.mood === "asleep" || this.mood === "dozing") {
				this.mood = "waking";
				this.setFace("happy");
				this.hop(12);
				setTimeout(() => { if (this.mood === "waking") { this.mood = "working"; this.setFace("awake"); } }, 650);
			}
			if (kind === "error") {
				this.setFace("ouch");
				this.flash();
				this.shake();
				setTimeout(() => { if (this.face === "ouch") this.setFace(this.mood === "asleep" ? "sleep" : "awake"); }, 800);
			} else if (kind === "edit" && this.mood === "working") this.hop(6);
		}

		tick() {
			const now = Date.now();
			this.anims = this.anims.filter((a) => {
				const t = Math.min(1, (now - a.start) / a.ms);
				a.set(t);
				if (t >= 1) { if (a.done) a.done(); return false; }
				return true;
			});
			this.busy *= 0.995;
			if (this.mood === "working" && now - this.lastCall > SLEEP_AFTER) { this.mood = "asleep"; this.setFace("sleep"); }
			// Looking around while working, faster when busy; breathing always.
			const s = this.state;
			s.lookX = this.mood === "working" ? 3.5 * Math.sin(now / (900 - 500 * this.busy)) : 0;
			this.eyes.forEach((e) => (e.style.transform = `translate(${s.lookX.toFixed(2)}px, 0px)`));
			const breath = this.mood === "asleep" ? 0.025 * Math.sin(now / 900) : 0.01 * Math.sin(now / 600);
			this.frog.style.transform =
				`translate(${s.shakeX.toFixed(2)}px, ${s.hopY.toFixed(2)}px) scale(${(s.sqX).toFixed(3)}, ${(s.sqY * (1 + breath)).toFixed(3)})`;
			// Z's float up while he sleeps.
			this.zs.forEach((z, i) => {
				if (this.mood !== "asleep") { z.style.opacity = "0"; return; }
				const t = ((now / 2400) + i / 3) % 1;
				z.style.opacity = String(Math.sin(Math.PI * t) * 0.9);
				z.style.transform = `translate(${(-4 * t).toFixed(1)}px, ${(-10 * t).toFixed(1)}px)`;
			});
		}
	}

	globalThis.SlippyMascot = Mascot;
	if (typeof module !== "undefined") module.exports = { Mascot };
})();
