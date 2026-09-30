# What Slippy can do in Illustrator, and what's left

The goal: anything a person can do in Illustrator, an agent can do through
Slippy, natively through the SDK (no ExtendScript), with ids in and structured
results out. This file is the audit and the build tracker.

The Illustrator 2026 SDK has **136 suites / ~2,500 functions** plus the text
engine (ATE). Slippy used **28** of them when this audit was written.

Legend: ✅ covered and tested · 🔨 built, not yet tested in Illustrator · ◐ partly ·
✗ not yet · — not a user capability
(plug-in plumbing: registering tools, timers, memory, math helpers...).

## How "every interaction" maps to the SDK

- **Anything in a menu** → `menu.run`. Made reliable by `menu.list`, which
  enumerates every command registered in the running Illustrator
  (`AICommandManager::CountCommands` / `GetNthCommandInfo`, including other
  plug-ins'), with its menu path from `AIMenuCommandString.h` (521 built-in
  commands) and whether it's enabled / checked right now (`AIMenu`). No more
  guessing names.
- **Anything recordable** → `action.play`. Made reliable by `action.list`
  (registered events) and `action.run` (play a named action from an action
  set in the Actions panel).
- **Tool gestures** (pen, shape tools, drag-to-move, pathfinder buttons...) →
  the SDK can't synthesize mouse drags, but every gesture's *result* has a
  direct call: `path.create`, `shape.*`, `art.transform`, `pathfinder.*`...
  Plus `tool.select` to switch the active tool for the person.
- **Panels and dialogs** → their underlying operations (swatches, symbols,
  styles, layers, appearance...). Showing a panel is a menu command.
- **What the person sees** → `view.*` (zoom, center, screen mode, guides,
  grid, edges, rulers) and `hit.test` (what's under a point).

## Phase 1 — unblock agents now

| Command | Suite | Status |
|---|---|---|
| `menu.list {search}` - every menu command the SDK names (523), with menu path and on-screen label, plus Illustrator's own label search | AICommandManager | 🔨 |
| `menu.run` also takes the on-screen label; a miss points to `menu.list` | AICommandManager | 🔨 |
| `action.list {search}`, `action.describe {event}` (parameter keys, names, types) | AIActionManager | 🔨 |
| `tool.list`, `tool.current`, `tool.select {name or "pen"}` | AITool | 🔨 |
| `symbol.list`, `.instances`, `.create`, `.place`, `.replace`, `.break`, `.edit`, `.finish {save}`, `.redefine`, `.rename`, `.delete` | AISymbol | 🔨 |
| `isolation.state`, `.enter {id}`, `.exit` | AIIsolationMode | 🔨 |
| `view.get`, `view.set` (zoom, center, screen mode, preview / outline / pixel / overprint, guides, edges, grid, rulers), `view.fit`, `view.screenshot` | AIDocumentView | 🔨 |
| `hit.test {point}` | AIHitTest | 🔨 |
| Playing a named action from the Actions panel (`action.run {set, action}`) | — | ✗ the SDK has no call for it; to research |
| Listing every command at runtime | AICommandManager::CountCommands | ✗ "not implemented" in the SDK; the header catalog stands in |

## Phase 2 — color, paint and appearance

Every fill / stroke (art.set, shape.*, path.create...) now takes named paint:
`{"swatch": n}`, `{"spot": n, "tint"}`, `{"gradient": n, "angle"}` (spans the art
unless origin / length are given), `{"pattern": n, "scale"}` - and reads come
back in the same shape.

| Area | Commands | Suite | Status |
|---|---|---|---|
| Stroke / fill detail | `art.set` dash, dashOffset, cap, join, miterLimit, strokeAlign, fillOverprint, strokeOverprint, evenOdd; read back by `art.get` | AIPathStyle, AIPaintStyle | 🔨 |
| Swatches | `swatch.list`, `.create`, `.set`, `.delete`, `swatch.group.create` | AISwatchList, AISwatchGroup | 🔨 |
| Spot / global colors | `spot.list`, `.create`, `.delete` | AICustomColor | 🔨 |
| Gradients | `gradient.list`, `.create`, `.set`, `.delete` (linear / radial, stops with midpoint + opacity) | AIGradient | 🔨 |
| Patterns | `pattern.list`, `.create` (from art, tile size), `.delete` | AIPattern | 🔨 |
| Graphic styles | `style.list`, `.apply`, `.create`, `.redefine`, `.delete` | AIArtStyle | 🔨 |
| Appearance | `appearance.get` (fills / strokes, effects with settings, transparency), `.add` (extra fill / stroke), `.clear`, `.copy` (eyedropper) | AIArtStyleParser | 🔨 |
| Opacity / blend mode | `appearance.set`, `art.set {opacity, blendMode}` - via the blend dictionary (keys Mode / Opacity / Isolated / Knockout from older SDKs; `appearance.get` shows the raw dictionary to confirm) | AIArtStyleParser | 🔨 |
| Live effects | `effect.list`, `effect.apply {effect, settings}` (settings read off existing art with appearance.get) | AILiveEffect | 🔨 |
| Recolor | `color.used`, `color.replace`, `color.adjust` (invert / grayscale / brightness) | AIPathStyle | 🔨 |
| Pattern / gradient editing in place | enter pattern edit mode | AIPattern::EnterPatternEditMode | ✗ |
| Color guide / harmony | Recolor Artwork dialog | AIColorHarmony (dialog-driven) | ✗ `menu.run` |

## Phase 3 — shapes and path operations

| Area | Commands | Suite | Status |
|---|---|---|---|
| Shapes | `shape.roundedRect`, `.polygon`, `.star`, `.spiral`, `.line`, `.arc`, `.pie` | AIShapeConstruction | ◐ (rect, ellipse) |
| Pathfinder | `pathfinder.unite/intersect/exclude/minusFront/minusBack/divide/trim/merge/crop/outline`, compound shapes | AIPathfinder | ✗ |
| Compound paths | `art.compound`, `art.releaseCompound` | AIArt, AIGroup | ✗ |
| Path edits | reverse, close/open, simplify, smooth, measure (length/area), add/delete anchors, point at length | AIPath, AIPathConstruction, AICurveFitting | ◐ (create only) |
| Convert | outline stroke, expand, expand appearance, convert to paths, flatten transparency | AIArtConverter, AIExpand, AIMaskFlattener | ✗ |
| Blends | `blend.make/release/expand`, steps, spine | AIPathInterpolate | ✗ |
| Envelopes | `envelope.warp/mesh/fromTop/release/expand` | AIEnvelope | ✗ |
| Gradient mesh | `mesh.create/get/setColor` | AIMesh | ✗ |
| Repeats | `repeat.radial/grid/mirror` | AIRepeat | ✗ |
| Live Paint | `livePaint.make/expand` | AIPlanarObject | ✗ |
| Brushes / width | stroke profiles, arrowheads, art brushes | AIBeautifulStrokes | ✗ |
| Perspective | grid show/plane, put art in perspective | AIPerspectiveGrid, AIPerspectiveTransform | ✗ |
| Dimensions | read / expand dimension objects | AIDimensionObject | ✗ |

## Phase 4 — text

| Area | Commands | Suite | Status |
|---|---|---|---|
| Frames | area text, text on a path, threading (link / unlink), orientation | AITextFrame | ◐ (point text) |
| Character | font, size, leading, tracking, kerning, color, case, baseline... on ranges | ATE | ◐ (size) |
| Paragraph | alignment, indents, spacing, hyphenation | ATE | ✗ |
| Styles | `textStyle.char/para.list/create/apply` | ATE, AIATECurrTextFeatures | ✗ |
| Fonts | `font.list`, `font.find` | AIFont | ✗ |
| Outlines | `text.outline` | AITextFrame::CreateOutline | ✗ |
| Legacy text | convert | AILegacyTextConversion | ✗ |

## Phase 5 — document, artboards, layers, images

| Area | Commands | Suite | Status |
|---|---|---|---|
| Artboards | `artboard.list/add/delete/set` (name, bounds, order), `.fitToArt`, select | AIArtboard | ◐ (read) |
| Document setup | units, color mode convert, ruler origin, bleed, raster effect resolution | AIDocument | ◐ |
| Layers | sublayers, move/reorder, color, template, printable, dim images, merge | AILayer, AILayerList | ◐ |
| Selection | select by type / fill / stroke / style / symbol ("Select > Same") | AIMatchingArt, AIArtSet | ◐ |
| Clipboard | cut / copy / paste / paste in place / in front / behind | menu commands, AIClipboard | ✗ (via `menu.run` only) |
| Guides / grid | `guide.create/clear`, grid settings, snapping | AIPath (guides), AIGrid | ✗ |
| Images | embed / relink / rasterize / trace (Image Trace) / crop | AIPlaced, AIRaster, AIRasterize, AIVectorize | ◐ (place) |
| Metadata | art / document key-values, XMP | AIDictionary, AITag, AIXMLElement | ✗ |
| Slices | `slice.*` | AISlicing | ✗ |
| Transform again | `art.transformAgain` | AITransformAgain | ✗ |
| Print | `document.print` | AIDocumentList::Print | ✗ |
| Cloud documents | open / save to cloud, recents | AICloudDocument, AIDocumentList | ✗ |
| Recent files | list / open | AIDocumentList | ✗ |
| Preferences | read / write | AIPreference | ✗ |

## Covered today

`app.info`, `commands.list`, `document.*` (list, info, new, open, activate,
save, export, formats, close, redraw), `layer.*` (list, create, set),
`art.*` (tree, get, selection, select, set, transform, duplicate, arrange,
move, group, ungroup, clip, unclip, place, delete), `shape.rect/ellipse`,
`path.create`, `text.create`, `menu.run`, `action.play`, `plugin.message`,
`history.undo/redo`.

## Not user capabilities (—)

Plug-in plumbing that an agent never needs directly: Annotator(Drawer), Array,
Assertion, Block, CountedObject, Context, CSXSExtension, ControlBar,
CursorSnap, DataFilter, DrawArt, Entry, FilePath, FixedMath, Geometry,
GlobalUnicodeString, HardSoft, LiveEdit, MdMemory, ModalParent, NameSpace,
Notifier, Panel, PlatformMemory, Plugin, PluginGroup (authoring), Random,
RandomBellCurve, RealBezier, Runtime, SFWUtilities, StringFormatUtils,
StringPool, TabletData, Timer, Toolbox, UnicodeString, URL, UUID, and the
format-author halves of FileFormat / FXG / HTMLConversion / SVGFilter /
ForeignObject / ImageOptimization.
