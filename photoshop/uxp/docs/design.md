# Slippy for Photoshop — design

Slippy for Photoshop lets coding agents (Claude, Codex, Cursor…) drive Adobe
Photoshop the way [Slippy](https://github.com/tonykastaneda/slippy) drives
Illustrator: a small set of everyday tools, `slippy_find` / `slippy_call` for the
rest, every call one step on Edit > Undo, and a panel where Slippy the frog
shows what's happening.

## Shape

```
agent (MCP over HTTP, 127.0.0.1:47710/mcp)
   │
   ▼
bridge/  (Node)  — MCP server, tool list, batching, timeouts, error envelope
   │  WebSocket 127.0.0.1:47710/plugin  (the plug-in connects out)
   ▼
plugin/  (UXP panel inside Photoshop) — command table, executeAsModal,
          history grouping, the Slippy panel (frog, bars, feed)
```

Why two parts: a UXP plug-in can't listen on a socket, only connect out. The
bridge is the server agents talk to; the plug-in connects to it and runs the
commands. (Every desktop Photoshop MCP project lands on this shape.)

The C++ SDK route (like Slippy's Illustrator plug-in) was ruled out: Photoshop's
C++ SDK is built for filters and file formats, has no clean way to run a
long-lived service on the main thread, and Adobe's new APIs ship in UXP first.

## Rules carried over from Slippy (learned the hard way)

1. **One call, one undo step.** Edits run inside `core.executeAsModal` with
   history suspended, so each call is one history state named after what it did
   ("Slippy: Rename layer"). A batch is one state.
2. **File writes run alone.** In Illustrator, a save inside a batch rolled the
   batch back. Here a batch splits around saves and exports: the edits before
   are one history state, the write runs on its own, the edits after are another.
3. **Never save by surprise.** `document.close` discards unless `save: true`;
   it never leaves Photoshop to answer a "Save changes?" prompt. (Slippy's
   `document.close` once saved because the suppressed prompt defaulted to Save.)
4. **No dialogs.** batchPlay always runs with `dialogOptions: "dontDisplay"`; a
   call that would need a dialog fails with a clear error instead of blocking.
5. **Honest errors, one shape.** Every failure is
   `{ code, message, hint }` — e.g. `photoshop_busy` when a modal dialog is
   open, `no_document`, `not_found`, `invalid_params`, `timeout`. The hint says
   what to try next.
6. **Leave modes cleanly.** Anything that enters a mode (editing a smart object,
   quick mask…) has a matching exit, and the exit verifies it's really out.
   (Slippy's `symbol.finish` once reported success while still in isolation.)
7. **Plain-English feed.** Every call becomes a line in the panel ("Renamed
   layer to Logo"), colored teal for reads, orange for edits, red for errors.
8. **Small surface, full reach.** ~12 everyday tools; everything else through
   `slippy_find` / `slippy_call`, plus a raw `ps.batchplay` escape hatch so an
   agent is never stuck.
9. **Preview without touching files.** Anything rendered for a check goes to a
   copy or a temp file, never into the working document.

Ideas from other projects (no code copied):
- adb-mcp (Mike Chambers, MIT): the bridge + UXP plug-in split, batchPlay
  descriptors for layer operations.
- PS-MCP-IMT (CC BY-NC — ideas only, no code): report `photoshop_busy` instead
  of hanging when Photoshop is modal; a small facade in front of a large catalog.

## Coordinates and ids

Photoshop pixels, origin at the document's top-left, y down. Layer ids are
Photoshop's own `layer.id` — stable for the life of the document (better than
Illustrator's, which change on reopen).

## Panel

Same layout as Slippy's Illustrator panel:
- header: Slippy (green frog head `#92F607` with two eye bumps), "Slippy" +
  version badge, the calling agent's color dot + call / error counts;
- activity bars (calls over the last minute, read / edit / error colored);
- status row ("Listening on 127.0.0.1:47710" / "Bridge not running") + Copy
  connection;
- the feed, newest first.

Slippy's faces: asleep (‿ ‿ with Z's) when idle, happy (^ ^) on the first call,
awake (solid eyes, looking around) while working, ouch (> <, red flash, shake)
on an error. Edits make him hop; every call sends a ripple in its color.
