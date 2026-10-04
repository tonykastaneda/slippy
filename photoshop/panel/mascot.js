// Slippy, the mascot - the native panel's frog (shared/SlippyPanelView.mm),
// in the docked panel: a green head with two eye bumps, the eyes in the
// bumps, no mouth.
//
//   asleep   — — eyes, Z's rising past his left eye, slow deep breathing
//   waking   a blink to ^ ^, a stretch, a hop that squashes on landing
//   working  solid eyes glancing around (faster when busy), blinks, a small
//            bounce on edits
//   ouch     > <, a flinch, a shake that dies away, a red flash
//   dozing   ^ ^, nods off, then — — (about 6 s after the last call)
//
// Click him: asleep, he wakes and stays up 20 s; awake, a happy hop.
//
// How it moves without twitching: one motion owns each property at a time,
// and every move eases from wherever he is now, so nothing jumps; a hop
// never starts while another is in the air; faces change only behind a
// blink. Photoshop's UXP doesn't animate opacity set from code, so fades go
// through colors and SVG attributes, never style.opacity.

(function () {
	const SLEEP_AFTER = 6000;    // quiet ms before dozing
	const CLICK_AWAKE = 20000;   // a click keeps him up this long
	const COLORS = { read: "#40c8e0", edit: "#ff9f0a", error: "#ff453a" };
	const SLIPPY = "#92f607";
	const OUTLINE = "M23 41C11 41 4 34 4 26C4 21 6.5 17 11 14C10 9 12 5 17 5C21 5 23 8.5 24 13C25 8.5 27 5 31 5C36 5 39 9 38 14C42 17 44 21 44 26C44 34 35 41 23 41Z";
	const EYE_W = 16, EYE_H = 16;   // each eye's SVG box, centered in its bump
	const FACES = {   // drawn in the eye box, black
		awake: '<ellipse cx="8" cy="8" rx="6.1" ry="6.65" fill="#000"/>',
		sleep: '<path d="M2 7.5 Q8 12.5 14 7.5" fill="none" stroke="#000" stroke-width="2.6" stroke-linecap="round"/>',
		happy: '<path d="M2.5 10 L8 4.5 L13.5 10" fill="none" stroke="#000" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>',
		ouchL: '<path d="M3.5 3 L12 8 L3.5 13" fill="none" stroke="#000" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>',
		ouchR: '<path d="M12.5 3 L4 8 L12.5 13" fill="none" stroke="#000" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>',
	};

	const reduceMotion = (() => {
		try { return !!(window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches); } catch (e) { return false; }
	})();
	const MOTION = reduceMotion ? 0.3 : 1;

	const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
	const rand = (a, b) => a + Math.random() * (b - a);
	const ease = {
		out: (t) => 1 - Math.pow(1 - t, 3),
		in: (t) => t * t * t,
		inOut: (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2),
		sine: (t) => -(Math.cos(Math.PI * t) - 1) / 2,
	};

	function el(cls, parent, html) {
		const d = document.createElement("div");
		d.className = cls;
		if (html) d.innerHTML = html;
		parent.appendChild(d);
		return d;
	}

	// "#rrggbb" + alpha -> "rgba(...)".
	function rgba(color, a) {
		const m = /rgba?\((\d+),\s*(\d+),\s*(\d+)/.exec(color);
		let r, g, b;
		if (m) { r = +m[1]; g = +m[2]; b = +m[3]; }
		else { const n = parseInt(String(color).replace("#", ""), 16); r = (n >> 16) & 255; g = (n >> 8) & 255; b = n & 255; }
		return `rgba(${r}, ${g}, ${b}, ${clamp(a, 0, 1).toFixed(3)})`;
	}

	class Mascot {
		constructor(box) {
			this.box = box;
			this.frog = el("frog", box);
			this.head = el("head", this.frog);
			this.bumps = [el("bump l", this.frog), el("bump r", this.frog)];
			this.eyes = this.bumps.map((b) => el("eye", b));
			this.zs = [0, 1, 2].map(() => el("z", box, "Z"));
			this.textColor = "#888";
			try { this.textColor = getComputedStyle(document.body).color || this.textColor; } catch (e) {}

			// Every animated value, and the one motion (if any) that owns it.
			this.v = { hopY: 0, sqX: 1, sqY: 1, shakeX: 0, lookX: 0, lookY: 0, blink: 1, breathAmp: 0.03, flash: 0 };
			this.motions = {};
			this.mood = "asleep";
			this.face = null;
			this.lastCall = 0;
			this.awakeUntil = 0;
			this.busy = 0;   // 0..1, recent call rate
			this.inAir = false;
			this.lastBounce = 0;
			this.lastOuch = 0;
			this.nextGlance = 0;
			this.nextBlink = Date.now() + rand(2500, 4500);
			this.ripples = [];
			this.timers = [];

			this.showFace("sleep");
			box.addEventListener("click", () => this.clicked());
			this.start = Date.now();
			setInterval(() => this.frame(), 16);
		}

		// ---------------------------------------------------------------- motion primitives

		// Moves v[key] to 'to' over ms from wherever it is now; a newer move of
		// the same key replaces this one without a jump.
		tween(key, to, ms, curve = ease.inOut) {
			return new Promise((done) => {
				this.motions[key] = { from: this.v[key], to, start: Date.now(), ms: Math.max(1, ms), curve, done };
			});
		}

		async keyframes(steps) {
			for (const [key, to, ms, curve] of steps) await this.tween(key, to, ms, curve);
		}

		later(ms, fn) {
			const id = setTimeout(() => { this.timers = this.timers.filter((t) => t !== id); fn(); }, ms);
			this.timers.push(id);
		}

		// ---------------------------------------------------------------- faces

		showFace(face) {
			if (face === this.face) return;
			this.face = face;
			this.eyes.forEach((e, i) => {
				const shape = face === "ouch" ? FACES[i === 0 ? "ouchL" : "ouchR"] : FACES[face];
				e.innerHTML = `<svg width="${EYE_W}" height="${EYE_H}" viewBox="0 0 16 16">${shape}</svg>`;
			});
		}

		// A blink: eyes close, the face changes (if asked) while they're shut, they open.
		async blink(face) {
			if (this.blinking) { if (face) this.pendingFace = face; return; }
			this.blinking = true;
			this.pendingFace = face;
			await this.tween("blink", 0.08, 70, ease.in);
			if (this.pendingFace) this.showFace(this.pendingFace);
			this.pendingFace = null;
			await this.tween("blink", 1, 110, ease.out);
			this.blinking = false;
		}

		// ---------------------------------------------------------------- moves

		// Anticipation, up, down, a squash on landing, settle. Never two at once.
		async hop(height) {
			if (this.inAir) return;
			this.inAir = true;
			const h = height * MOTION;
			await Promise.all([this.tween("sqY", 0.92, 80, ease.out), this.tween("sqX", 1.06, 80, ease.out)]);
			await Promise.all([
				this.tween("hopY", -h, 190, ease.out), this.tween("sqY", 1.05, 120, ease.out), this.tween("sqX", 0.97, 120, ease.out),
			]);
			await Promise.all([this.tween("hopY", 0, 170, ease.in), this.tween("sqY", 1, 150), this.tween("sqX", 1, 150)]);
			await Promise.all([this.tween("sqY", 0.9, 70, ease.out), this.tween("sqX", 1.08, 70, ease.out)]);
			await Promise.all([this.tween("sqY", 1, 220, ease.out), this.tween("sqX", 1, 220, ease.out)]);
			this.inAir = false;
		}

		async shake() {
			const amp = 4 * MOTION;
			for (const [x, ms] of [[-amp, 50], [amp * 0.8, 70], [-amp * 0.55, 70], [amp * 0.3, 70], [0, 90]])
				await this.tween("shakeX", x, ms, ease.sine);
		}

		flashRed() {
			this.v.flash = 1;
			this.tween("flash", 0, 420, ease.in);
		}

		// A ring in his outline growing out and fading (SVG attributes, so it
		// fades in UXP too). At most three at once.
		ripple(color) {
			if (this.ripples.length >= 3) return;
			const r = el("ripple", this.box);
			r.innerHTML = `<svg width="70" height="57.3" viewBox="4 5 40 36"><path d="${OUTLINE}" fill="none" stroke="${color}" stroke-width="2" stroke-linejoin="round"/></svg>`;
			this.ripples.push({ el: r, svg: r.firstChild, path: r.firstChild.firstChild, start: Date.now() });
		}

		// ---------------------------------------------------------------- moods

		wake(stayFor) {
			this.mood = "waking";
			this.awakeUntil = Math.max(this.awakeUntil, Date.now() + (stayFor || 0));
			this.blink("happy");
			this.tween("breathAmp", 0.012, 600);
			this.later(160, () => this.hop(11));
			this.later(900, () => {
				if (this.mood !== "waking") return;
				this.mood = "working";
				this.blink("awake");
			});
		}

		doze() {
			this.mood = "dozing";
			this.blink("happy");
			this.tween("lookX", 0, 400);
			this.tween("lookY", 0, 400);
			this.keyframes([["hopY", 2.5 * MOTION, 700, ease.inOut], ["hopY", 0, 500, ease.inOut]]);
			this.later(1300, () => {
				if (this.mood !== "dozing") return;
				this.mood = "asleep";
				this.blink("sleep");
				this.tween("breathAmp", 0.03, 1200);
			});
		}

		// A call happened. kind: read | edit | error.
		callArrived(kind) {
			const now = Date.now();
			this.busy = Math.min(1, this.busy * 0.85 + 0.2);
			this.lastCall = now;
			this.ripple(COLORS[kind] || COLORS.read);
			if (this.mood === "asleep" || this.mood === "dozing") {
				this.wake(0);
				if (kind !== "error") return;
			}
			if (kind === "error") {
				if (now - this.lastOuch < 700) return;
				this.lastOuch = now;
				this.blink("ouch");
				this.flashRed();
				this.keyframes([["sqX", 0.93, 60, ease.out], ["sqX", 1, 260, ease.out]]);
				this.shake();
				this.later(950, () => { if (this.face === "ouch") this.blink(this.mood === "asleep" ? "sleep" : this.mood === "waking" ? "happy" : "awake"); });
			} else if (kind === "edit" && this.mood === "working" && now - this.lastBounce > 450) {
				this.lastBounce = now;
				this.hop(4.5);
			}
		}

		clicked() {
			const now = Date.now();
			this.lastCall = Math.max(this.lastCall, now - SLEEP_AFTER + 200);
			if (this.mood === "asleep" || this.mood === "dozing") this.wake(CLICK_AWAKE);
			else {
				this.awakeUntil = now + CLICK_AWAKE;
				if (this.face !== "happy") this.blink("happy");
				this.hop(9);
				this.later(1100, () => { if (this.mood === "working" && this.face === "happy") this.blink("awake"); });
			}
		}

		// ---------------------------------------------------------------- the frame

		frame() {
			const now = Date.now();
			// Motions.
			for (const key of Object.keys(this.motions)) {
				const m = this.motions[key];
				const t = clamp((now - m.start) / m.ms, 0, 1);
				this.v[key] = m.from + (m.to - m.from) * m.curve(t);
				if (t >= 1) { delete this.motions[key]; m.done(); }
			}
			this.busy *= 0.996;

			// Dozing off after a quiet spell (unless a click is keeping him up).
			if (this.mood === "working" && now - this.lastCall > SLEEP_AFTER && now > this.awakeUntil && !this.inAir) this.doze();

			// Glances and blinks while awake: a look somewhere, held, then another.
			if (this.mood === "working") {
				if (now > this.nextGlance) {
					this.tween("lookX", rand(-3.2, 3.2) * MOTION, 140, ease.out);
					this.tween("lookY", rand(-1.4, 1.0) * MOTION, 140, ease.out);
					this.nextGlance = now + rand(700, 2200) * (1 - 0.55 * this.busy);
				}
				if (now > this.nextBlink && !this.blinking) {
					this.blink();
					if (Math.random() < 0.15) this.later(220, () => this.blink());
					this.nextBlink = now + rand(2600, 5200);
				}
			}

			// Breathing: a slow swell, deeper asleep.
			const period = this.mood === "asleep" ? 3400 : 2600;
			const breath = this.v.breathAmp * Math.sin(((now - this.start) / period) * 2 * Math.PI) * MOTION;

			const v = this.v;
			this.frog.style.transform =
				`translate(${v.shakeX.toFixed(2)}px, ${v.hopY.toFixed(2)}px) scale(${v.sqX.toFixed(3)}, ${(v.sqY * (1 + breath)).toFixed(3)})`;
			const eyeT = `translate(${v.lookX.toFixed(2)}px, ${v.lookY.toFixed(2)}px) scale(1, ${v.blink.toFixed(3)})`;
			for (const e of this.eyes) e.style.transform = eyeT;
			const skin = v.flash > 0.01 ? this.mix(SLIPPY, COLORS.error, v.flash) : SLIPPY;
			if (skin !== this.skin) {
				this.skin = skin;
				this.head.style.background = skin;
				for (const b of this.bumps) b.style.background = skin;
			}

			// Ripples: grow 55% and fade over 700 ms.
			this.ripples = this.ripples.filter((r) => {
				const t = clamp((now - r.start) / 700, 0, 1);
				const s = 1 + 0.55 * ease.out(t) * MOTION;
				r.el.style.transform = `scale(${s.toFixed(3)})`;
				r.path.setAttribute("stroke-opacity", ((1 - t) * 0.9).toFixed(3));
				if (t >= 1) { r.el.remove(); return false; }
				return true;
			});

			// Z's: rising past his left eye, drifting left, fading in and out (by color).
			this.zs.forEach((z, i) => {
				const asleep = this.mood === "asleep";
				const t = (((now - this.start) / 3000) + i / 3) % 1;
				const a = asleep ? Math.sin(Math.PI * t) * 0.85 : 0;
				z.style.color = rgba(this.textColor, a);
				z.style.fontSize = `${(8 + 8 * t).toFixed(1)}px`;
				z.style.transform = `translate(${(-12 * t).toFixed(1)}px, ${(-24 * t).toFixed(1)}px)`;
			});
		}

		mix(a, b, t) {
			const p = (c) => [(parseInt(c.slice(1), 16) >> 16) & 255, (parseInt(c.slice(1), 16) >> 8) & 255, parseInt(c.slice(1), 16) & 255];
			const [x, y] = [p(a), p(b)];
			return `rgb(${x.map((c, i) => Math.round(c + (y[i] - c) * t)).join(", ")})`;
		}
	}

	globalThis.SlippyMascot = Mascot;
	if (typeof module !== "undefined") module.exports = { Mascot };
})();
