# Slippy

A native Adobe Illustrator plug-in (C++) that lets agents call Illustrator
directly without ExtendScript/JSX. Agents send JSON-RPC over loopback HTTP, and
Slippy runs each call through the Illustrator SDK on Illustrator's main thread.
It has a docked, animated panel that shows what agents are doing live.

```
agent (MCP) ──────────▶ 127.0.0.1:7331/mcp ─┐
shell ─▶ client/slippy ─▶ 127.0.0.1:7331/rpc ─┼─▶ Slippy.aip ─▶ Illustrator SDK
curl / any HTTP ──────▶ 127.0.0.1:7331/rpc ─┘   (token)     (main thread, undoable)
```

MCP is built into the plug-in, so an agent connects straight to Illustrator with
no bridge process and no Python.

## Build and install (macOS)

It needs only the Xcode **Command Line Tools** (no Xcode, no Visual Studio) and
the Illustrator 2026 SDK at `~/Developer/AdobeIllustratorSDK` (override with
`SDK=...`).

```sh
make                 # build/Slippy.aip, arm64, signed ad hoc
sudo make install    # first time only: the Plug-ins folder is root-owned
make install         # later updates, no sudo
```

Quit Illustrator before installing, then restart it. Open **Window > Utilities >
Slippy** to show the panel.

## The panel and Slippy

Slippy the mascot is a frog's face: a green head with two eye bumps on top
and solid black eyes, no mouth. Slippy reacts to what agents are doing:

| Mood | Face | When |
|---|---|---|
| Asleep | `— —`, Z's drifting up past the left eye, slow slumped breathing, the odd snuffle | Nothing happening, paused, or server down (dimmed, no Z's) |
| Waking | `^ ^`, a stretch, then a hop that squashes on landing | The first call after he's been asleep |
| Working | solid black eyes springing around together, blinks (sometimes double), a curious head tilt, a little bounce on each edit | Calls are coming in; he looks around faster the busier it gets |
| Ouch | `> <`, a flinch, a shake that dies away, a red flash | A call failed |
| Dozing | `^ ^`, nods off, catches himself, then `— —` | About 6 s after the last call |

Click Slippy to wake him up: he stays awake looking around for 20 seconds
(awake already, he hops happily). Every face change happens through a blink. Moves ease in and out and start
from wherever Slippy is on screen, so one never cuts another off with a jump:
hops, squash and stretch, and breathing each run on their own layer. Each call also sends a ripple out
from Slippy (teal = read, amber = edit, red = error), kicks its command group's
bar, and slides into the feed as a plain-English line ("Rotated “Slippy” 15°", "Couldn't find that object"), with the time of day. Hover a line for the technical command, how long it took and any full error. The panel also has
**Pause agents**, which refuses calls, and **Copy connection**, which copies
the `claude mcp add` line with the token. With macOS Reduce Motion on, Slippy
moves about a third as much, feed lines fade in without sliding, and the Z's
light up in place one after another instead of drifting. Launch Illustrator
with `SLIPPY_FULL_MOTION=1` in its environment to get full motion anyway.

## Seeing what Slippy touches

On the canvas, each call draws a green box (with corner handles) around the art
it touched, and Slippy's triangle cursor glides there, labelled with the call's
plain-English line (red when the call failed). A batch gets one box around
everything it touched. The box fades after a few quiet seconds, then the
label, then the cursor. It's drawn by an annotator, like Illustrator's own
selection highlights, so it's never part of the artwork, the file or the undo
history.

To watch Slippy without Illustrator, run `make preview`. It opens the panel in a
window and feeds it made-up bursts of calls with quiet spells in between, so
he dozes off.

## Connecting an agent

Slippy keeps one token in `~/Library/Application Support/Slippy/token` (mode 0600).
It stays the same across launches, so saved agent configs keep working. Delete
the file to rotate it; the next launch makes a new one.
`~/Library/Application Support/Slippy/session.json` has the current URLs and
token.

**Claude Code:** click **Copy connection** in the Slippy panel and paste. It
copies:

```sh
claude mcp add --transport http slippy http://127.0.0.1:7331/mcp --header "Authorization: Bearer <token>"
```

**Other MCP clients** (Cursor, VS Code, Gemini CLI, ...): add an HTTP MCP
server with the URL `http://127.0.0.1:7331/mcp` and the header
`Authorization: Bearer <token>`. Every command is a tool (`art.transform`
becomes `art_transform`). There are also `slippy_batch`, which runs many calls as
one undo step, and `slippy_status`. For a client that only launches stdio
servers, use an adapter such as
`npx mcp-remote http://127.0.0.1:7331/mcp --header "Authorization: Bearer <token>"`.

**Shell:**

```sh
client/slippy app.info
client/slippy commands
client/slippy shape.rect x=0 y=0 width=200 height=100 fill='"#FF3366"'
client/slippy art.transform '{"rotate": 15, "scale": 1.5}'   # acts on the selection
client/slippy menu.run command=group
```

**Raw HTTP (JSON-RPC):**

```sh
TOKEN=$(cat ~/Library/Application\ Support/Slippy/token)
curl -s -H "Authorization: Bearer $TOKEN" localhost:7331/rpc \
  -d '{"jsonrpc":"2.0","id":1,"method":"document.info"}'
```

Send a JSON array of calls to `/rpc` to run them as a **batch**: they execute
back to back as one undo step. A batch stops at the first error unless that
call sets `"stopOnError": false`. The `timeout` field (seconds, default 60)
sets how long the client waits.

## Commands

| Group | Methods |
|---|---|
| app | `app.info`, `commands.list` (full parameter docs) |
| document | `document.list`, `.info`, `.new`, `.open`, `.activate`, `.save`, `.export`, `.formats`, `.close`, `.redraw` |
| layer | `layer.list`, `.create`, `.set` (rename / visible / locked / current / delete) |
| art | `art.tree`, `.get`, `.selection`, `.select`, `.set`, `.transform`, `.duplicate`, `.arrange`, `.delete` |
| structure | `art.move` (into a group or above / below an object), `art.group`, `art.ungroup`, `art.clip`, `art.unclip` |
| files | `art.place` (link or embed an image / PDF / .ai, into a group or next to an object, scaled to `fitTo` another object) |
| create | `shape.rect`, `shape.ellipse`, `path.create` (corners or Bézier anchors), `text.create` |
| escape hatches | `menu.run` (any menu command by name, like `app.executeMenuCommand`), `action.play` (any action event with typed params) |
| history | `history.undo`, `history.redo` |

- **Exporting:** `document.export {path}` picks the format from the extension
  (or `format`: png, jpg, pdf, svg, tiff, psd, webp, eps...). PNG and JPEG
  render at any size (`scale` 2 = 144 dpi, or `dpi`), of the artboard, all the
  art (`area: "art"`), or one object (`id`); PNGs are transparent unless
  `transparent: false`. They're drawn from a PDF copy Illustrator writes, so the
  document is never touched.
- **Saving:** with a `path`, a native `.ai` save is a Save As (the document moves to that path). Any other `format` (names come from `document.formats`, e.g. `"PDF File Format"`, `"svg file format"`) writes a copy. Slippy never shows a save dialog.
- **Structure without menus:** `art.move`, `art.clip`, `art.ungroup` and
  `art.place` work on ids, not the selection. For example, to swap art in for a
  placeholder inside a clipping group, send one batch:
  `art.place {path, above: <placeholder>, fitTo: <placeholder>}` then
  `art.delete {id: <placeholder>}`. `art.tree` marks clipping groups
  (`clipped`), their masks (`clipMask`) and placed files (`file`).
- **No alerts:** Illustrator's alerts are off while agent calls run, so a
  command that doesn't apply can't freeze Illustrator behind a dialog.
  `menu.run` reports `changed: false` (with a note) when nothing happened.
- **Undo:** every call is one step on Edit > Undo (a batch is one step for all its calls), and `history.undo` works on them.
- **Coordinates** are Illustrator artwork points, with y growing upward.
  Bounds, artboards and positions all use this one space; `document.info`
  lists the artboard bounds.
- **Art ids** are Illustrator's own UUIDs. They stay stable for the life of
  the object, and a stale id returns an error instead of crashing Illustrator.
- **Paint** is `"#RRGGBB"`, `"none"`, `{"rgb":[r,g,b]}` (0 to 255),
  `{"cmyk":[c,m,y,k]}` (0 to 100) or `{"gray":n}` (0 to 100).
- **Errors** are JSON-RPC codes: -32602 bad params, -32601 unknown method,
  -32000 Illustrator error (`data.aiError` holds its code), -32001 timeout,
  -32002 unavailable (no document, paused, shutting down), -32004 id not found.

## Security

- The server binds `127.0.0.1` only.
- `/rpc` and `/mcp` require the token (`Authorization: Bearer` or `X-Slippy-Token`).
- Requests that carry an `Origin` header are refused, so a web page can't
  drive Illustrator.
- The token is stored in a file only you can read. Delete the file to rotate it.

## How it works

| File | Role |
|---|---|
| `Source/Server.*` | Loopback HTTP server on its own threads; never touches the SDK |
| `Source/SlippyPlugin.*` | Plug-in entry. Queues each request, wakes the main thread (GCD), runs the queue in a one-tick timer message, a normal plug-in context |
| `Source/Commands.*` | The command table: every SDK call, JSON in and out, undo labels |
| `Source/Narrate.*` | Turns each call into the feed's plain-English line |
| `Source/Overlay.*` | The canvas overlay: box, cursor and label for what each call touched |
| `Source/Mcp.*` | MCP over HTTP: handshake, tool list built from the command table, tool calls |
| `Source/SlippyPanelView.*` | The panel and Slippy (Cocoa + Core Animation, no SDK) |
| `Source/SlippyPanel.*` | Puts the panel view into Illustrator's docked panel |
| `Source/Preview.mm` | `make preview`: the panel in a plain window with made-up calls |
| `Source/SlippySuites.*` | Suite imports; everything except the core suites is optional and checked before use |
| `Source/Json.*` | Self-contained JSON (no third-party runtime dependency) |
| `client/` | `slippy` CLI and `slippy_client.py` library (stdlib only, optional) |

Findings from testing on Illustrator 30.2:

- Timer messages record no undo history, and 30.2's timer suite (version 5) can't ask for one. So Slippy runs calls through its own menu command, "Slippy Run Agent Calls", which is hidden from the Window menu. It invokes that command when calls arrive, and Illustrator gives menu commands a normal undo context. The timer is only a fallback.
- `TransformArt` on a group moves it but records no undo step, so groups are transformed through their contents.
- The native AI writer shows its Options dialog even with `kFileFormatSuppressUI`, so `.ai` saves play `adobe_saveDocumentAs` with the dialog off.
- `GetSelectedArt` includes layer groups as "partially selected". `art.selection`, and every command that defaults to the selection, uses only fully selected, top-level objects.
- A new-document preset struct must be filled from a real preset, or the artboards come out as garbage.

Lessons carried over from RAGE:

- The timer is added on first use, because adding one at startup makes
  Illustrator refuse the plug-in.
- The timer suite is picked by version, because 30.2 ships an older layout.
- The bundle must contain an empty `Slippy.rsrc`.
