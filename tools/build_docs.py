#!/usr/bin/env python3
"""Builds the site's docs pages (docs/docs/*.html) from docs-src/*.html: each
source is a page's content with its title and lede in comments at the top;
this wraps it in the shared top bar, sidebar and page links.

    python3 tools/build_docs.py
"""
import html
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs-src")
OUT = os.path.join(ROOT, "docs", "docs")

# The pages, in sidebar order: (file, sidebar label).
PAGES = [
    ("index.html", "Overview"),
    ("install.html", "Install"),
    ("connect.html", "Connect an agent"),
    ("why.html", "Why Slippy"),
]

TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} - Slippy docs</title>
<meta name="description" content="{lede_attr}">
<link rel="icon" href="../slippy.svg">
<link rel="stylesheet" href="docs.css">
</head>
<body>
<header class="top">
	<a class="logo" href="../" aria-label="Slippy home"><img src="../navlogo.svg" alt="Slippy"></a>
	<nav><a href="./" class="on">Docs</a><a href="../#downloads">Download</a><a href="https://github.com/tonykastaneda/slippy">GitHub</a></nav>
	<button class="menu" type="button">Menu</button>
</header>
<div class="layout">
	<aside class="side">
		<div class="group">
			<h4>Docs</h4>
{sidebar}
		</div>
		<div class="group">
			<h4>Links</h4>
			<a href="../#downloads">Download</a>
			<a href="https://github.com/tonykastaneda/slippy">GitHub</a>
		</div>
	</aside>
	<main>
		<h1>{title}</h1>
		<p class="lede">{lede}</p>
{body}
		<div class="pager">{pager}</div>
	</main>
</div>
<script src="docs.js"></script>
</body>
</html>
"""


def meta(text, key):
    m = re.search(r"<!--\s*%s:\s*(.*?)\s*-->" % key, text)
    return m.group(1) if m else ""


def main():
    os.makedirs(OUT, exist_ok=True)
    for i, (name, label) in enumerate(PAGES):
        with open(os.path.join(SRC, name), encoding="utf-8") as f:
            text = f.read()
        title, lede = meta(text, "title"), meta(text, "lede")
        body = re.sub(r"<!--\s*(title|lede):.*?-->\s*", "", text).strip()
        sidebar = "\n".join('\t\t\t<a href="%s"%s>%s</a>' % (n, ' class="on"' if n == name else "", l) for n, l in PAGES)
        pager = ""
        if i > 0:
            n, l = PAGES[i - 1]
            pager += '<a href="%s"><small>Previous</small>%s</a>' % (n, l)
        if i + 1 < len(PAGES):
            n, l = PAGES[i + 1]
            pager += '<a class="next" href="%s"><small>Next</small>%s</a>' % (n, l)
        page = TEMPLATE.format(title=title, lede=lede, lede_attr=html.escape(re.sub("<[^>]+>", "", lede), quote=True),
                               sidebar=sidebar, body=body, pager=pager)   # unindented: <pre> keeps its whitespace
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(page)
        print("built docs/docs/" + name)


if __name__ == "__main__":
    main()
