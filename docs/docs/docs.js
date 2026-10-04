// Slippy's docs: a copy button on every code block, and the phone menu.
(function () {
	document.querySelectorAll("main pre").forEach((pre) => {
		const b = document.createElement("button");
		b.className = "copy";
		b.type = "button";
		b.textContent = "copy";
		b.addEventListener("click", async () => {
			try { await navigator.clipboard.writeText(pre.querySelector("code").innerText.trim()); b.textContent = "copied"; }
			catch (e) { b.textContent = "select + copy"; }
			setTimeout(() => (b.textContent = "copy"), 1400);
		});
		pre.appendChild(b);
	});
	const menu = document.querySelector(".menu");
	if (menu) menu.addEventListener("click", () => document.body.classList.toggle("menu-open"));
})();
