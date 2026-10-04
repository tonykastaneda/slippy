# Known bugs

Found while using Slippy. Open ones first, newest first; fixed ones below, with the version that fixed them.

_No open bugs right now._

## Fixed

### Fixed in 0.1.1 - Illustrator: `pathfinder.*` returned `{"result": null}` when the shapes were the only things in a group

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

**Fix (0.1.1):** `Pathfind` now watches every container from the inputs up to
the layer, skipping any that no longer exists, so the result is found
wherever Illustrator put it. Checked live on the logo (returns the compound
path) and on two shapes in a busy layer (returns just the cut shape).
