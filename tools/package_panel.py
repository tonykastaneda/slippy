#!/usr/bin/env python3
"""Packages Slippy for Photoshop's docked panel (photoshop/panel) as a .ccx -
a zip with the manifest at its root - adding the agents' logos from
shared/resources/agents as agents/<Name>.svg in their brand colors (from
tools/agent_logos.py, as the native panel draws them), the rest in gray.

    python3 tools/package_panel.py <output .ccx>
"""
import json
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_logos import COLORS   # noqa: E402  brand colors, as the native panel uses

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL = os.path.join(ROOT, "photoshop", "panel")
AGENTS = os.path.join(ROOT, "shared", "resources", "agents")
with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f:
    VERSION = f.read().strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(PANEL):
            for name in sorted(files):
                if name.startswith("."):
                    continue
                path = os.path.join(folder, name)
                rel = os.path.relpath(path, PANEL).replace(os.sep, "/")
                if rel == "manifest.json":   # the version comes from VERSION
                    with open(path, encoding="utf-8") as f:
                        manifest = json.load(f)
                    manifest["version"] = VERSION
                    z.writestr(rel, json.dumps(manifest, indent="\t") + "\n")
                    continue
                z.write(path, rel)
        for name in sorted(os.listdir(AGENTS)):
            if name.endswith(".svg"):
                agent = name[:-4].replace("_", " ")
                color = "#%06X" % COLORS[agent] if agent in COLORS else "#9A9A9A"
                with open(os.path.join(AGENTS, name), encoding="utf-8") as f:
                    svg = f.read().replace('fill="currentColor"', 'fill="%s"' % color)
                # Photoshop's SVG renderer doesn't pass the root's fill down: put it on each shape.
                svg = svg.replace("<path ", '<path fill="%s" ' % color).replace('width="1em"', 'width="24"').replace('height="1em"', 'height="24"')
                z.writestr("agents/" + name, svg)


if __name__ == "__main__":
    main()
