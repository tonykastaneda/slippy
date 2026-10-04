# Known bugs

Found while using Slippy; not fixed yet. Newest first.

## Illustrator: `pathfinder.*` returns `{"result": null}` when the shapes were the only things in a group

**Seen:** 2026-10-04, cutting the frog out of the nav logo's green square
(`SVG/navlogo.svg`). `pathfinder.minusFront {ids: ["32", "34"]}` did the cut
correctly (a compound path, id 75, the square with a frog-shaped hole), but
the call returned `{"result": null}`, as if nothing were left.

**Why:** `Pathfind` (`illustrator/CmdShapes.cpp`) finds the result by
comparing the children of the inputs' parent before and after the operation,
because Illustrator 30.2 leaves `fOutputArt` empty. Here the two paths were
the only children of a group (id 30). Pathfinder replaced that whole group
with its result, one level up, in the layer. The group the code was watching
no longer exists, so no new child turns up and the result reads as empty.

**Fix idea:** also watch the parents' parents (and the layer), or record the
parent group's own parent and position before the operation, and look there
when the group is gone. Then return the new art from wherever it landed.

**Workaround:** the operation itself works. Read the result with `art.tree`
afterwards.
