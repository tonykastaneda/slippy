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

## Build and install (Windows)

It needs Visual Studio 2022 (C++ workload), CMake and Python 3, and the
Illustrator 2026 SDK:

```sh
cmake -S . -B build-win -A x64 -DAI_SDK="C:/path/to/AdobeIllustratorSDK"   # or -A ARM64
cmake --build build-win --config Release    # -> build-win/Release/Slippy.aip
```

Quit Illustrator and copy `Slippy.aip` into
`C:\Program Files\Adobe\Adobe Illustrator 2026\Plug-ins`, then restart it.
The Windows build is untested in Illustrator so far: it compiles in CI, but
the panel, the server and PNG/JPEG export haven't been run there yet.

## CI builds

`.github/workflows/build.yml` builds on every push: a macOS `Slippy.aip`
(signed with Developer ID and notarized once the secrets below are set, ad
hoc until then) and Windows x64 / ARM64. Download them from the run's
artifacts. Pushing a `v*` tag also publishes them as a GitHub release. The
Illustrator SDK comes from this private repo's `sdk-illustrator-2026` release.

| Setting | What |
|---|---|
| secret `MACOS_CERT_P12` | base64 of the Developer ID Application certificate, exported as a .p12 |
| secret `MACOS_CERT_PASSWORD` | that .p12's password |
| secret `APPLE_ID` | the Apple ID email |
| secret `APPLE_APP_PASSWORD` | an app-specific password for that Apple ID |
| variable `APPLE_TEAM_ID` | the developer team (GM98GV6S97) |

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
hops, squash and stretch, and breathing each run on their own layer. Each call also sends a ripple (Slippy's own outline) out
from Slippy (teal = read, amber = edit, red = error), kicks its command group's
bar, and slides into the feed as a plain-English line ("Rotated “Slippy” 15°", "Couldn't find that object"), with the time of day. Hover a line for the technical command, how long it took and any full error. The panel also has
**Copy connection**, which copies Slippy's URL (token included) to hand to an
agent. Beside the call counts is the logo of the agent that sent the last call
(Claude, Codex, Cursor, Gemini, Grok...; `Resources/agents`). **Pause agents**,
which refuses calls, is in the panel's flyout menu. The chevron along the bottom
opens a terminal (macOS for now): your own login shell, with Slippy's URL in
`SLIPPY_URL` so an agent started there can connect. It keeps running while
Illustrator does, panel shown or not, and when Illustrator quits its history
and folder are saved and resume on the next launch. Closing it with the
chevron is the one way to end and clear it (it asks first if something is
running). ⌘C / ⌘V / ⌘A work in it. Every call is also logged to
`calls.log` next to the token (read it with `app.log`). With macOS Reduce Motion on (on
Windows: "Show animations in Windows" off), Slippy
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

Slippy keeps one token in `~/Library/Application Support/Slippy/token` (mode 0600;
on Windows `%APPDATA%\Slippy\token`). It stays the same across launches, so
saved agent configs keep working. Delete the file to rotate it; the next
launch makes a new one. `session.json` next to it has the current URLs and
token.

**Give an agent Slippy:** click **Copy connection** in the Slippy panel and
paste what it copies, Slippy's URL with the token in it:

```
http://127.0.0.1:7331/mcp/<token>
```

That's the whole handoff, whatever MCP client the agent runs in: it adds
that URL as an MCP server (streamable HTTP). Most clients load a new
server's tools on a reload or restart. An agent with a shell can also use
it straight away over plain HTTP (`/rpc/<token>`, below). One without the
token is told to ask you for this URL, not to go reading the token file.

**Tools an agent sees:** the everyday commands as their own tools
(`art_tree`, `art_get`, `art_set`, `art_transform`, `shape_rect`,
`document_info`...), plus `slippy_find` to search all ~190 commands by words
("symbol", "gradient", "artboard") and `slippy_call` to run any of them. That's
about 17 tools, which fits every MCP client. Clients that take hundreds of
tools can use `http://127.0.0.1:7331/mcp/<token>?tools=all` to get every
command as its own tool (`art.transform` becomes `art_transform`). There are
also `slippy_batch`, which runs many calls as one undo step, and
`slippy_status`. The token also works as a header (`Authorization: Bearer
<token>` or `X-Slippy-Token`) on `http://127.0.0.1:7331/mcp`. For a client
that only launches stdio servers, use an adapter such as
`npx mcp-remote http://127.0.0.1:7331/mcp/<token>`.

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
curl -s http://127.0.0.1:7331/rpc/<token> \
  -d '{"jsonrpc":"2.0","id":1,"method":"document.info"}'
```

Send a JSON array of calls to `/rpc` to run them as a **batch**: they execute
back to back as one undo step. A batch stops at the first error unless that
call sets `"stopOnError": false`. The `timeout` field (seconds, default 60)
sets how long the client waits.

## Commands

What's covered and what's still to come, suite by suite: [docs/COVERAGE.md](docs/COVERAGE.md).

| Group | Methods |
|---|---|
| app | `app.info`, `commands.list` (full parameter docs) |
| document | `document.list`, `.info`, `.new`, `.open`, `.activate`, `.save`, `.export`, `.formats`, `.close`, `.redraw` |
| layer | `layer.list`, `.create`, `.set` (rename / visible / locked / current / delete) |
| art | `art.tree`, `.get`, `.selection`, `.select`, `.set`, `.transform`, `.duplicate`, `.arrange`, `.delete` |
| structure | `art.move` (into a group or above / below an object), `art.group`, `art.ungroup`, `art.clip`, `art.unclip` |
| files | `art.place` (link or embed an image / PDF / .ai, into a group or next to an object, scaled to `fitTo` another object) |
| create | `shape.rect`, `.ellipse`, `.roundedRect`, `.polygon`, `.star`, `.spiral`, `.pie`, `path.create` (corners or Bézier anchors), `text.create` |
| path operations | `pathfinder.*` (unite, intersect, exclude, minusFront, minusBack, divide, trim, merge, crop, outline), `compound.make/release`, `path.measure/pointAt/reverse/setClosed/simplify/offset/outlineStroke/join/addAnchors/removeAnchors`, `art.outline/toPaths/expand/expandAppearance`, `envelope.*`, `repeat.*`, `blend.*`, `livePaint.*` |
| paint | swatches (`swatch.*`), spot / global colors (`spot.*`), `gradient.*`, `pattern.*`; any color param also takes `{"swatch"}`, `{"spot"}`, `{"gradient"}`, `{"pattern"}` by name; `color.used`, `.replace`, `.adjust` |
| appearance | `appearance.get`, `.set` (opacity, blend mode), `.add` (extra fills / strokes), `.clear`, `.copy`; `effect.list`, `effect.apply`; graphic styles `style.*` |
| text | `text.get`, `.format` (font, size, leading, tracking, color, alignment, indents... on frames or ranges), `.area`, `.onPath`, `.outline`, `.link` / `.unlink`, `.find`, `.replace`; `font.list`; `charStyle.*`, `paraStyle.*` |
| document | `artboard.*` (list, add, set, delete, fit), `document.settings` (units, bleed, color mode), `.xmp`, `.recent`, `.print`, `layer.tree`, `data` (key-value data saved on art or the document), `preference` |
| selection & clipboard | `select.matching`, `.same`, `.all`, `.none`, `.inverse`; `edit.copy`, `.cut`, `.paste`; `guide.*` |
| images | `image.info`, `.embed`, `.relink`, `.trace`, `art.rasterize`, `art.transformAgain` |
| symbols | `symbol.list`, `.instances`, `.create`, `.place`, `.replace`, `.break`, `.edit` / `.finish` (edit a definition in place), `.redefine`, `.rename`, `.delete` |
| isolation | `isolation.state`, `.enter`, `.exit` |
| view | `view.get`, `.set` (zoom, center, screen / preview mode, guides, grid...), `.fit`, `.screenshot`, `hit.test` |
| tools | `tool.list`, `tool.current`, `tool.select` |
| find what to run | `menu.list` (every menu command, with its menu path and label), `action.list`, `action.describe` |
| escape hatches | `menu.run` (any menu command by name or label, like `app.executeMenuCommand`), `action.play` (any action event with typed params), `plugin.message` (send another plug-in a script message, like `app.sendScriptMessage`) |
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
- **Other plug-ins:** `plugin.message {plugin, selector, param}` sends the
  message `app.sendScriptMessage` would and returns the plug-in's text reply.
  Pressing RAGE's MAKE is `plugin.message plugin=RAGE selector=design.make`;
  RAGE defers the work, so poll `selector=design.result` until it stops
  saying `running`. That deferred work isn't part of Slippy's undo step.
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
- `/rpc` and `/mcp` require the token, in the path (`/mcp/<token>`) or a header
  (`Authorization: Bearer` or `X-Slippy-Token`). Slippy's URL is as secret as
  the token: anyone with it can drive Illustrator.
- Requests that carry an `Origin` header are refused, so a web page can't
  drive Illustrator.
- The token is stored in a file only you can read. Delete the file to rotate it.

## How it works

| File | Role |
|---|---|
| `Source/Server.*` | Loopback HTTP server on its own threads; never touches the SDK |
| `Source/SlippyPlugin.*` | Plug-in entry. Queues each request, wakes the main thread, runs the queue in a one-tick timer message, a normal plug-in context |
| `Source/Platform.*` | What differs between macOS and Windows outside the panel: files, randomness, waking the main thread (GCD / a message window) |
| `Source/Raster.*` | `document.export` PNG / JPEG: draws the PDF copy's page (Core Graphics / Windows.Data.Pdf + WIC) |
| `Source/Commands.*` | The command table and the core commands: JSON in and out, undo labels |
| `Source/Kit.h` | What every command file shares: errors, params, art ids, paint |
| `Source/Cmd*.cpp` | Command families: `CmdCatalog` (menu / action / tool discovery), `CmdSymbols` (symbols, isolation), `CmdView` (view, hit test), `CmdPaint` (swatches, spots, gradients, patterns, recolor), `CmdAppearance` (appearance, effects, graphic styles), `CmdShapes` (shapes, pathfinder, path operations, envelopes, repeats), `CmdText` (text, fonts, text styles), `CmdDocument` (artboards, settings, selection, clipboard, guides, images, data) |
| `tools/sdk_catalog.py` | Build step: the menu commands, action events and tools the SDK names, for `menu.list` and friends |
| `Source/Narrate.*` | Turns each call into the feed's plain-English line |
| `Source/Overlay.*` | The canvas overlay: box, cursor and label for what each call touched |
| `Source/Mcp.*` | MCP over HTTP: handshake, tool list built from the command table, tool calls |
| `Source/SlippyPanelView.*` | The panel and Slippy (Cocoa + Core Animation, no SDK) |
| `Source/SlippyPanel.*` | Puts the panel view into Illustrator's docked panel |
| `Source/Terminal.*` | The drawer's terminal: the shell in a pseudo-terminal (one per Illustrator session, saved and resumed), shown with xterm.js (`Resources/terminal`) |
| `Source/SlippyPanelWin.cpp` | The panel and Slippy on Windows (GDI+, with a small keyframe / spring engine standing in for Core Animation) |
| `Source/Preview.mm` | `make preview`: the panel in a plain window with made-up calls |
| `Source/SlippySuites.*` | Suite imports; everything except the core suites is optional and checked before use |
| `Source/Json.*` | Self-contained JSON (no third-party runtime dependency) |
| `client/` | `slippy` CLI and `slippy_client.py` library (stdlib only, optional) |

Findings from testing on Illustrator 30.2:

- Timer messages record no undo history, and 30.2's timer suite (version 5) can't ask for one. So Slippy runs calls through its own menu command, "Slippy Run Agent Calls", which is hidden from the Window menu (on macOS; Windows still lists it, and choosing it just runs any queued calls). It invokes that command when calls arrive, and Illustrator gives menu commands a normal undo context. The timer is only a fallback.
- `TransformArt` on a group moves it but records no undo step, so groups are transformed through their contents.
- The native AI writer shows its Options dialog even with `kFileFormatSuppressUI`, so `.ai` saves play `adobe_saveDocumentAs` with the dialog off.
- `GetSelectedArt` includes layer groups as "partially selected". `art.selection`, and every command that defaults to the selection, uses only fully selected, top-level objects.
- A new-document preset struct must be filled from a real preset, or the artboards come out as garbage.

Lessons carried over from RAGE:

- The timer is added on first use, because adding one at startup makes
  Illustrator refuse the plug-in.
- The timer suite is picked by version, because 30.2 ships an older layout.
- The bundle must contain an empty `Slippy.rsrc`.
