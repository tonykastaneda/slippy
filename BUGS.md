# Known bugs

Found while using Slippy. Open ones first, newest first; fixed ones below, with the version that fixed them.

_No open bugs right now._

## Fixed

### Fixed after 1.3.3 (unreleased) - no way to read the paint of many objects in one call

**Seen:** 2026-11-09, cleaning up garment templates (GF01 baseball, then the 83 GF03-GF14 basketball files). Each file
needed every path's fill and stroke, to tell the outside cut line (stroked, closed, outermost) from inner lines (thin or
dashed strokes inside it) and old base fills (filled, no stroke). Slippy couldn't give that in one call, so the job fell
back to a pile of `art.get` calls, and then to ExtendScript through `osascript`, which Slippy is meant to replace.

**Why:** `art.tree` built every node with `ArtSummary`: id, type, name, bounds and flags, no paint and not whether a path
is closed. `art.get` added `style` only for the one object asked about, only for a plain path, and always with every
anchor in `segments`; its children came back without style. Eight `art.get` calls on jersey outlines ran to ~160 KB.

**Fix:** `art.tree` takes `style: true` (default off, so other output is unchanged). Every path and compound path in the
tree then carries `style` (fill, stroke, width, dash...), and every path `closed` and `length`. A compound path with no
style of its own reports its first path's paint. `art.get` takes `segments: false` to leave out the anchors. Checked live
on Illustrator 30.2 with GF05 L.ai: one call returns the whole tree, and the 6 pt black cut lines, the 1 pt red marks and
the cream base fills read apart from it alone.

### Fixed in 1.3.3 - `effect.apply` Warp listed in the Appearance panel but drew nothing

**Seen:** 2026-11-03, arching a player name with Effect > Warp > Arch (`Adobe Deform`). The call reported success, the
panel listed the Warp, but the text stayed straight through save / reopen, pre / post stage.

**Why:** `appearance.get` printed a real `0.0` as `0`, and `effect.apply` stored a bare `0` as an integer. Warp's
`DeformHoriz` / `DeformVert` must be reals; as integers the effect can't read them and draws nothing. Measured live: the
hand-made Warp has them as reals, the dead one as integers, every other key the same.

**Fix:** effect settings from `appearance.get` give whole reals as `{"type": "real", "value": 0}`, which `effect.apply`
already stores as a real, so settings round-trip with their types. New `effect.apply from=<art id>` clones the effect off
art that has it (exact dictionary, only that effect, its stage). Checked live on Illustrator 30.2: both ways arch the text.

### Fixed in 1.3.3 - Illustrator 30.2: `text.format align` failed

**Seen:** 2026-11-02. Illustrator 30.2 has the ATE ParaFeatures suite only up to v102 and the SDK headers ask v103, so
every paragraph call was refused.

**Why not just use v102:** its table doesn't match the v103 header. Calling `SetJustification` at the header's slot 49
hit the next function and threw the text's bounds to garbage (left 8592, right -24174).

**Fix:** only for the align call, the wrapper uses v102 and calls `SetJustification` at slot 48, measured live: the frame
centres on its anchor with its width unchanged. Other paragraph settings stay refused on this Illustrator.

### Fixed in 1.3.1 - Illustrator on Windows: `view.screenshot` crashed Illustrator

**Seen:** 2026-10-09, first Windows run (Illustrator 30.8.2, Windows 11).
`view.screenshot` on a fresh scratch document killed Illustrator with an
access violation (0xc0000005 inside `Illustrator.exe`); the client saw
`ECONNRESET`. It crashed the same way with a long path and with a short one.

**Why:** `ViewScreenshot` (`illustrator/CmdView.cpp`) passed `nullptr` as the
view. The other `AIDocumentViewSuite` calls take null to mean the current
view, but `ScreenShot` doesn't say so, and on Windows it dereferences it.

**Fix:** pass the real view from `GetNthDocumentView(0, ...)`. Checked live on
Windows: the PNG is written and Illustrator stays up.

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
