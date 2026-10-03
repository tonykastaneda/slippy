# Slippy for Photoshop

Slippy the frog, now in Photoshop: a panel that lets AI agents (Claude Code,
Codex, Cursor, …) work in your open Photoshop document over MCP, and shows you
what they're doing as they do it.

```
agent ──MCP (HTTP)──▶ bridge (Node, 127.0.0.1:47710) ◀──WebSocket── Slippy panel (UXP, in Photoshop)
```

Photoshop's UXP plug-ins can't listen on a port, so the panel dials out to a
small local bridge, and agents talk to the bridge. Nothing leaves your machine.

## Setup

1. **Bridge**:
   ```sh
   cd bridge && npm install && npm start
   ```
2. **Panel**: run `scripts/install.sh`, or `scripts/package.sh` and double-click
   the `.ccx` it makes. Restart Photoshop, then open **Plugins › Slippy**. The
   panel finds the bridge on its own; the dot turns green when it's connected.
3. **Agent**: click **Copy connection** in the panel, or run:
   ```sh
   claude mcp add --transport http slippy-photoshop http://127.0.0.1:47710/mcp
   ```

## What agents get

The everyday commands are tools: `slippy_status`, `document_info`, `layer_tree`,
`layer_get`, `layer_set`, `layer_create`, `layer_transform`, `text_create`,
`document_export` and `history_undo`. `slippy_find` searches the full catalog,
`slippy_call` runs anything in it, and `slippy_batch` runs several calls as one
undo step. `ps.batchplay` runs any Photoshop action descriptor when nothing else
fits.

## The rules

These came from building Slippy for Illustrator and are written up
in [docs/design.md](docs/design.md):

- One call is one step in History ("Slippy: Renamed a layer to Logo"), and a batch is one step too.
- Saves, exports, opens and closes always run on their own, never inside a batch's step.
- Nothing is saved by surprise. `document.close` discards changes unless asked to save.
- No dialogs. If Photoshop is busy you get `photoshop_busy` straight away rather than a hang.
- Every error has the same shape: `{code, message, hint}`.
- The panel's feed says what happened in plain English.

## Development

- `cd bridge && npm test`: the bridge end to end, with a stand-in panel.
- Open `plugin/index.html` in a browser to preview the panel. Outside
  Photoshop it plays made-up calls so you can watch the frog.
- Settings: `SLIPPY_PS_PORT` (default 47710), `SLIPPY_PS_TIMEOUT_MS` (default 60000).

See [NOTICE](NOTICE) for credits.
