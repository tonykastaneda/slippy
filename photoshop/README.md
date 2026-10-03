# Slippy for Photoshop

Slippy the frog in Photoshop: a native C++ plug-in that lets AI agents
(Claude Code, Codex, Cursor...) work in your open Photoshop documents over MCP,
and shows you what they're doing as they do it. It's built from the same code
as Slippy for Illustrator: the server, MCP, JSON and the frog panel come from
[`../shared`](../shared), and the Photoshop side lives here.

```
agent ──MCP──▶ 127.0.0.1:7332/mcp/<token> ─▶ Slippy.plugin (in Photoshop) ─▶ action descriptors
                                              (main thread, one History step per edit)
```

No JavaScript, no Node, nothing to start: Slippy starts with Photoshop.

## Build and install

From the repo root. It needs the Photoshop SDK at `~/Developer/AdobePhotoshopSDK`
(Adobe Developer Console > Downloads > Photoshop).

**macOS** - the Xcode Command Line Tools (override the SDK with `PS_SDK=.../pluginsdk/photoshopapi`):

```sh
make photoshop            # build/ps/Slippy.plugin, arm64, signed ad hoc
make install-photoshop    # into Photoshop's Plug-ins/Slippy folder (quit Photoshop first)
```

Photoshop's Plug-ins folder belongs to root, so once, make a `Slippy` folder
there that's yours (later installs need no sudo):

```sh
sudo mkdir -p "/Applications/Adobe Photoshop 2026/Plug-ins/Slippy" && sudo chown $USER "/Applications/Adobe Photoshop 2026/Plug-ins/Slippy"
```

Restart Photoshop. **Window > Slippy** shows the panel (File > Automate >
Slippy too, when a document is open).

**Windows** - Visual Studio 2022, CMake, Python 3:

```sh
cmake -S . -B build-win -A x64 -DPS_SDK="C:/path/to/AdobePhotoshopSDK"   # or -A ARM64
cmake --build build-win --config Release    # -> build-win/photoshop/Release/Slippy.8li
```

Quit Photoshop, copy `Slippy.8li` into `C:\Program Files\Adobe\Adobe Photoshop 2026\Plug-ins`,
and restart it; File > Automate > Slippy shows the panel (a floating window;
right-click it to pause agents). The Windows build compiles in CI but hasn't
been run in Photoshop yet.

## Connecting an agent

Click **Copy connection** in the panel and give the agent what it copies:
`http://127.0.0.1:7332/mcp/<token>`. The token lives in
`~/Library/Application Support/Slippy/Photoshop/token`, apart from
Illustrator's, so both can run at once. `SLIPPY_PS_PORT` changes the port. Plain
JSON-RPC works too, at `/rpc/<token>`, like Slippy for Illustrator.

## What agents get

Everyday tools: `document_info`, `document_open`, `document_export`, `layer_tree`,
`layer_get`, `layer_set`, `layer_create`, `layer_transform`, `text_create`,
`ps_batchplay`, `ps_get`, `history_undo`, plus `slippy_find` / `slippy_call` for
the rest (~35 commands) and `slippy_batch` for several calls as one History step:

| Group | Methods |
|---|---|
| document | `document.list`, `.info`, `.activate`, `.new`, `.open`, `.save`, `.export` (PNG / JPEG copy), `.close`, `.crop` |
| layer | `layer.tree`, `.get`, `.select`, `.set` (name, visible, opacity, fill opacity, blend mode, locked), `.create`, `.delete`, `.duplicate`, `.transform`, `.move`, `.perspective` |
| selection & paint | `select.rect`, `.ellipse`, `.all`, `.none`; `edit.fill`; `filter.blur` |
| text | `text.create` |
| history | `history.undo`, `.redo` |
| escape hatches | `ps.batchplay` (any action descriptor, in batchPlay's JSON), `ps.get` (any property) |

`layer.perspective` straightens a photographed page, sign or screen: give the
four corners of the thing in the photo and they go to a rectangle (a
perspective transform, fitted to Photoshop's warp mesh).

## The rules

The same as Slippy for Illustrator:

- One call is one step in History, named after what it did ("Slippy: Renamed a layer to “Logo”"); a batch's edits are one step too.
- Opening, saving, exporting and closing run on their own, and never show a dialog.
- Nothing is saved by surprise. `document.close` discards changes unless asked to save.
- Every error says what went wrong in plain words, and the feed shows failures in red.
- Coordinates are pixels from the document's top-left, y down.

## How it works

| File | Role |
|---|---|
| `PsPlugin.cpp` | Entry point (an automation plug-in that stays loaded and starts with Photoshop); queues server requests onto the main thread |
| `PsCommands.cpp` | The command table, batches and History steps (`SuspendHistory`) |
| `PsDescriptor.*` | Action descriptors <-> batchPlay-style JSON, and playing / getting them |
| `PsSuites.*` | Photoshop's suites, string IDs, UTF-8 <-> ZString, errors |
| `PsNarrate.cpp` | Plain-English feed lines and History names |
| `PsPanel.mm`, `PsPanelWin.cpp` | The frog panel (`shared/SlippyPanelView.mm`, `shared/SlippyPanelWin.cpp`) in a floating window; Window > Slippy on macOS |
| `PiPLs.json`, `Slippy.rc` | The plug-in's resource description (a JSON PiPL: in the bundle on macOS, the `JSON_PIPL` resource on Windows) |

Found building it, on Photoshop 27.4:

- Calls run straight from the main thread (GCD's main queue): Photoshop accepts `Play` and `Get` there with or without a document. Posting the plug-in's own event (`PostCommand`) fails with "not available" while the plug-in's menu item is disabled, which it is with no document open, whatever `EnableInfo` says.
- `SuspendHistory` around a run of calls makes them one named History step.
- `set` on a layer applies opacity and blend mode to the *active* layer, whatever layer it names, so `layer.set` selects the layer first.
- ZString suite 2's `MakeFromUnicode` takes a count of UTF-16 units, despite naming it `byteCount`.
- The plug-in only loads from Photoshop's own Plug-ins folder, not `~/Library/Application Support/Adobe/Plug-Ins/CC`.
- Files go into descriptors as bookmarks on macOS and as aliases (from the UTF-16 path) on Windows.

## The earlier UXP version

[`uxp/`](uxp) holds the first version, a UXP panel (`uxp/plugin`) and a Node
MCP server (`uxp/bridge`), kept for reference: `make photoshop-uxp`,
`make install-photoshop-uxp`, `make test-photoshop-uxp`. See
[uxp/docs/design.md](uxp/docs/design.md) and [uxp/NOTICE](uxp/NOTICE).
