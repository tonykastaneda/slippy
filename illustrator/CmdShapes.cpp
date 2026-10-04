// Shapes and path operations: every shape tool (AIShapeConstruction), the
// Pathfinder panel (AIPathfinder), compound paths, path edits and
// measurements (AIPath), outline / expand (AIArtConverter, AIExpand, art
// styles), offset path (the Offset Path live effect, expanded), envelopes
// (AIEnvelope) and repeats (AIRepeat). A few operations exist only as menu
// commands (join, blends, Live Paint...); those select exactly the given art,
// run the command, and return what it made.

#include "Kit.h"
#include "Overlay.h"
#include "IAIAutoBuffer.h"

#include <algorithm>
#include <cmath>

namespace slippy {

namespace {

const double kPi = 3.14159265358979323846;

// New shapes land on top of the current layer; move them where 'layer' / 'parent' say.
json::Value Placed(AIArtHandle art, const json::Value& p)
{
	ai::int16 order;
	AIArtHandle prep;
	Placement(p, order, prep);
	if (prep) Check(sAIArt->ReorderArt(art, order, prep), "ReorderArt");
	return Finish(art, p);
}

AIShapeConstructionSuite* Shapes() { return Need(sAIShapeConstruction, "The shape construction suite"); }

json::Value ShapeRoundedRect(const json::Value& p)
{
	ActiveDocument();
	double x = ReqNum(p, "x"), y = ReqNum(p, "y"), w = ReqNum(p, "width"), h = ReqNum(p, "height");
	double rx = 0, ry = 0;
	const json::Value& r = Required(p, "radius");
	if (r.isNumber()) rx = ry = r.asNumber();
	else { AIRealPoint pr = Point(r, "radius"); rx = pr.h; ry = pr.v; }
	AIArtHandle art = nullptr;
	Check(Shapes()->NewRoundedRect((AIReal) y, (AIReal) x, (AIReal) (y - h), (AIReal) (x + w), (AIReal) rx, (AIReal) ry, false, &art), "NewRoundedRect");
	return Placed(art, p);
}

json::Value ShapePolygon(const json::Value& p)
{
	ActiveDocument();
	AIRealPoint c = Point(Required(p, "center"), "center");
	int sides = (int) p.num("sides", 6);
	if (sides < 3 || sides > 1000) Fail(kErrInvalidParams, "'sides' must be 3 to 1000");
	AIArtHandle art = nullptr;
	Check(Shapes()->NewRegularPolygon((ai::uint16) sides, c.h, c.v, (AIReal) ReqNum(p, "radius"), false, &art), "NewRegularPolygon");
	return Placed(art, p);
}

json::Value ShapeStar(const json::Value& p)
{
	ActiveDocument();
	AIRealPoint c = Point(Required(p, "center"), "center");
	int points = (int) p.num("points", 5);
	if (points < 3 || points > 1000) Fail(kErrInvalidParams, "'points' must be 3 to 1000");
	double outer = ReqNum(p, "radius"), inner = p.num("innerRadius", outer / 2);
	AIArtHandle art = nullptr;
	Check(Shapes()->NewStar((ai::uint16) points, c.h, c.v, (AIReal) outer, (AIReal) inner, false, &art), "NewStar");
	return Placed(art, p);
}

json::Value ShapeSpiral(const json::Value& p)
{
	ActiveDocument();
	AIRealPoint c = Point(Required(p, "center"), "center");
	AIRealPoint start = c;
	start.v += (AIReal) ReqNum(p, "radius");
	AIArtHandle art = nullptr;
	Check(Shapes()->NewSpiral(c, start, (AIReal) p.num("decay", 80), (ai::int16) p.num("segments", 10), p.boolean("clockwise", false), &art), "NewSpiral");
	return Placed(art, p);
}

json::Value ShapePie(const json::Value& p)
{
	ActiveDocument();
	AIRealPoint c = Point(Required(p, "center"), "center");
	AIArtHandle art = nullptr;
	Check(Shapes()->NewEllipticalPie(c, (AIReal) ReqNum(p, "width"), (AIReal) ReqNum(p, "height"), (AIReal) p.num("rotation", 0),
		(AIReal) p.num("startAngle", 0), (AIReal) p.num("endAngle", 270), false, &art), "NewEllipticalPie");
	return Placed(art, p);
}

// ---- pathfinder

using PathfinderOp = decltype(AIPathfinderSuite::DoUniteEffect);

json::Value Pathfind(const json::Value& p, PathfinderOp AIPathfinderSuite::* op, const char* name)
{
	Need(sAIPathfinder, "The pathfinder suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	if (arts.size() < 2 && std::string(name) != "divide" && std::string(name) != "outline") Fail(kErrInvalidParams, std::string(name) + " needs at least 2 objects");
	AIPathfinderData data;
	data.Init();
	data.fSelectedArt = arts.data();
	data.fSelectedArtCount = (ai::int32) arts.size();
	AIFilterMessage message;
	memset(&message, 0, sizeof message);   // "not used": all fields NULL
	// fOutputArt is left empty (30.2), so find the result as what's new among
	// the inputs' parents' children. When the inputs were all of a group,
	// Illustrator replaces the whole group with the result, one level up - so
	// watch every container up to the layer, and skip any that's gone.
	std::vector<AIArtHandle> parents, before;
	for (AIArtHandle a : arts) {
		AIArtHandle parent = nullptr;
		for (sAIArt->GetArtParent(a, &parent); parent; ) {
			if (std::find(parents.begin(), parents.end(), parent) == parents.end()) parents.push_back(parent);
			AIArtHandle up = nullptr;
			if (sAIArt->GetArtParent(parent, &up)) break;
			parent = up;
		}
	}
	auto children = [&] {
		std::vector<AIArtHandle> out;
		for (AIArtHandle parent : parents) {
			if (!sAIArt->ValidArt(parent, true)) continue;   // the group the result replaced
			AIArtHandle c = nullptr;
			for (sAIArt->GetArtFirstChild(parent, &c); c; sAIArt->GetArtSibling(c, &c)) out.push_back(c);
		}
		return out;
	};
	before = children();
	Check((sAIPathfinder->*op)(&data, &message), name);
	json::Value v;
	if (data.fOutputArt && sAIArt->ValidArt(data.fOutputArt, true)) { v["result"] = ArtSummary(data.fOutputArt, 1); return v; }
	json::Value made = json::Value::MakeArray();
	for (AIArtHandle c : children())
		if (std::find(before.begin(), before.end(), c) == before.end()) made.push(ArtSummary(c, 1));
	if (made.size() == 1) v["result"] = made.asArray()[0];
	else if (made.size() > 1) v["result"] = made;
	else v["result"] = json::Value();   // nothing left (e.g. no overlap to intersect)
	return v;
}

// ---- ordering helpers

// Back to front, by comparing paint order.
std::vector<AIArtHandle> BackToFront(std::vector<AIArtHandle> arts)
{
	std::sort(arts.begin(), arts.end(), [](AIArtHandle a, AIArtHandle b) {
		short order = 0;
		sAIArt->GetArtOrder(a, b, &order);
		return order == kFirstBeforeSecond ? false : order == kSecondBeforeFirst;   // "before" = in front
	});
	return arts;
}

// ---- compound paths

json::Value CompoundMake(const json::Value& p)
{
	std::vector<AIArtHandle> arts = BackToFront(ArtList(p, true));
	for (AIArtHandle a : arts)
		if (ArtType(a) != kPathArt && ArtType(a) != kCompoundPathArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't a path");
	AIArtHandle compound = nullptr;
	Check(sAIArt->NewArt(kCompoundPathArt, kPlaceAbove, arts.back(), &compound), "NewArt");
	// Like Object > Compound Path > Make: everything takes the backmost path's paint.
	AIPathStyle style;
	bool haveStyle = sAIPathStyle && !sAIPathStyle->GetPathStyle(arts.front(), &style, nullptr);
	for (AIArtHandle a : arts) {
		if (ArtType(a) == kCompoundPathArt) {   // its paths join, it goes
			AIArtHandle child = nullptr;
			while (!sAIArt->GetArtFirstChild(a, &child) && child) Check(sAIArt->ReorderArt(child, kPlaceInsideOnTop, compound), "ReorderArt");
			sAIArt->DisposeArt(a);
			continue;
		}
		Check(sAIArt->ReorderArt(a, kPlaceInsideOnTop, compound), "ReorderArt");
	}
	if (haveStyle) {
		AIArtHandle child = nullptr;
		for (sAIArt->GetArtFirstChild(compound, &child); child; sAIArt->GetArtSibling(child, &child)) sAIPathStyle->SetPathStyle(child, &style);
	}
	return Finish(compound, p);
}

json::Value CompoundRelease(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle cp : ArtList(p, true)) {
		if (ArtType(cp) != kCompoundPathArt) Fail(kErrInvalidParams, "art " + ArtId(cp) + " isn't a compound path");
		AIArtHandle child = nullptr;
		while (!sAIArt->GetArtFirstChild(cp, &child) && child) {
			Check(sAIArt->ReorderArt(child, kPlaceAbove, cp), "ReorderArt");
			out.push(ArtSummary(child, 0));
		}
		sAIArt->DisposeArt(cp);
	}
	return out;
}

// ---- paths

std::vector<AIArtHandle> Paths(const json::Value& p)
{
	std::vector<AIArtHandle> arts = ArtList(p, true);
	for (AIArtHandle a : arts) if (ArtType(a) != kPathArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't a path");
	return arts;
}

json::Value PathMeasure(const json::Value& p)
{
	Need(sAIPath, "The path suite");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : Paths(p)) {
		json::Value v;
		v["id"] = ArtId(a);
		AIReal length = 0, area = 0;
		ai::int16 count = 0;
		AIBoolean closed = false;
		if (!sAIPath->GetPathLength(a, &length, (AIReal) 0.05)) v["length"] = (double) length;
		if (!sAIPath->GetPathArea(a, &area)) { v["area"] = std::fabs((double) area); v["clockwise"] = area < 0; }
		sAIPath->GetPathSegmentCount(a, &count);
		sAIPath->GetPathClosed(a, &closed);
		v["anchors"] = count;
		v["closed"] = (bool) closed;
		out.push(v);
	}
	return out;
}

// The point (and direction) a share of the way along a path.
json::Value PathPointAt(const json::Value& p)
{
	Need(sAIPath, "The path suite");
	AIArtHandle a = ArtById(IdText(Required(p, "id")));
	if (ArtType(a) != kPathArt) Fail(kErrInvalidParams, "that isn't a path");
	ai::int16 count = 0;
	AIBoolean closed = false;
	sAIPath->GetPathSegmentCount(a, &count);
	sAIPath->GetPathClosed(a, &closed);
	ai::int16 pieces = closed ? count : (ai::int16) (count - 1);
	if (pieces < 1) Fail(kErrInvalidParams, "the path has no length");
	std::vector<AIReal> lengths((size_t) pieces), accumulated((size_t) pieces);
	Check(sAIPath->MeasureSegments(a, 0, pieces, lengths.data(), accumulated.data()), "MeasureSegments");
	double total = accumulated.back() + lengths.back();
	double fraction = p.has("distance") ? ReqNum(p, "distance") / total : p.num("at", 0.5);
	fraction = std::max(0.0, std::min(1.0, fraction));
	ai::int16 seg = 0;
	AIReal t = 0;
	Check(sAIPath->LengthFractionToBezierPos(a, (AIReal) fraction, seg, t, lengths.data(), accumulated.data()), "LengthFractionToBezierPos");
	AIRealBezier b;
	Check(sAIPath->GetPathBezier(a, seg, &b), "GetPathBezier");
	double u = 1 - t;
	double x = u * u * u * b.p0.h + 3 * u * u * t * b.p1.h + 3 * u * t * t * b.p2.h + t * t * t * b.p3.h;
	double y = u * u * u * b.p0.v + 3 * u * u * t * b.p1.v + 3 * u * t * t * b.p2.v + t * t * t * b.p3.v;
	double dx = 3 * u * u * (b.p1.h - b.p0.h) + 6 * u * t * (b.p2.h - b.p1.h) + 3 * t * t * (b.p3.h - b.p2.h);
	double dy = 3 * u * u * (b.p1.v - b.p0.v) + 6 * u * t * (b.p2.v - b.p1.v) + 3 * t * t * (b.p3.v - b.p2.v);
	json::Value v;
	v["point"] = json::Array{x, y};
	v["angle"] = std::atan2(dy, dx) * 180 / kPi;
	v["distance"] = fraction * total;
	v["length"] = total;
	return v;
}

json::Value PathReverse(const json::Value& p)
{
	Need(sAIPath, "The path suite");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : Paths(p)) { Check(sAIPath->ReversePathSegments(a), "ReversePathSegments"); out.push(ArtSummary(a, 0)); }
	return out;
}

json::Value PathSetClosed(const json::Value& p)
{
	Need(sAIPath, "The path suite");
	bool closed = p.boolean("closed", true);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : Paths(p)) { Check(sAIPath->SetPathClosed(a, closed), "SetPathClosed"); out.push(ArtSummary(a, 0)); }
	return out;
}

void* Allocate(size_t size) { void* block = nullptr; sSPBlocks->AllocateBlock(size, "Slippy", &block); return block; }
void Dispose(void* block) { if (block) sSPBlocks->FreeBlock(block); }

json::Value PathSimplify(const json::Value& p)
{
	Need(sAIPathConstruction, "The path construction suite");
	AIPathConstructionMemoryObject memory = {Allocate, Dispose};
	AIReal flatness = (AIReal) p.num("tolerance", 0.5);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : Paths(p)) {
		ai::int16 before = 0, after = 0;
		sAIPath->GetPathSegmentCount(a, &before);
		Check(sAIPathConstruction->ReducePathSegments(a, flatness, &memory), "ReducePathSegments");
		sAIPath->GetPathSegmentCount(a, &after);
		json::Value v = ArtSummary(a, 0);
		v["anchorsBefore"] = before;
		v["anchorsAfter"] = after;
		out.push(v);
	}
	return out;
}

// ---- anchor points by index: 0-based in path order, negative counts from the end

AIArtHandle OnePath(const json::Value& p)
{
	Need(sAIPath, "The path suite");
	AIArtHandle a = ArtById(IdText(Required(p, "id")));
	if (ArtType(a) != kPathArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't a path");
	return a;
}

std::vector<AIPathSegment> ReadSegments(AIArtHandle a)
{
	ai::int16 count = 0;
	sAIPath->GetPathSegmentCount(a, &count);
	std::vector<AIPathSegment> segs((size_t) count);
	if (count) Check(sAIPath->GetPathSegments(a, 0, count, segs.data()), "GetPathSegments");
	return segs;
}

// Point selection is kept by position, so it would land on the wrong points after an
// edit: a selected path comes back wholly selected, an unselected one with none.
void WriteSegments(AIArtHandle a, const std::vector<AIPathSegment>& segs)
{
	if (segs.empty() || segs.size() > 32000) Fail(kErrInvalidParams, "a path needs 1 to 32000 points");
	bool selected = Attr(a, kArtSelected);
	AIErr e = sAIPath->SetPathSegmentCount(a, (ai::int16) segs.size());
	if (e == kUntouchableLayerErr)
		Fail(kErrInvalidParams, "art " + ArtId(a) + " can't be edited right now: the document is isolated on something else (a symbol edit?) - finish that first");
	Check(e, "SetPathSegmentCount");
	Check(sAIPath->SetPathSegments(a, 0, (ai::int16) segs.size(), segs.data()), "SetPathSegments");
	sAIArt->SetArtUserAttr(a, kArtSelected, selected ? kArtSelected : 0);
}

// 'slots' is the point count, or one more where inserting at the end is allowed.
size_t Index(const json::Value& v, size_t slots, size_t count)
{
	if (!v.isNumber() || v.asNumber() != std::floor(v.asNumber())) Fail(kErrInvalidParams, "point indices are whole numbers");
	double i = v.asNumber();
	if (i < 0) i += (double) slots;
	if (i < 0 || i >= (double) slots)
		Fail(kErrInvalidParams, "point " + std::to_string((long long) v.asNumber()) + " is out of range: the path has " + std::to_string(count) + " points");
	return (size_t) i;
}

// 'index' or 'indices', sorted and without repeats.
std::vector<size_t> Indices(const json::Value& p, size_t count)
{
	std::vector<size_t> out;
	if (p.has("indices")) {
		const json::Value& list = p.get("indices");
		if (!list.isArray()) Fail(kErrInvalidParams, "'indices' must be an array");
		for (const json::Value& v : list.asArray()) out.push_back(Index(v, count, count));
	} else out.push_back(Index(Required(p, "index"), count, count));
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

json::Value Edited(AIArtHandle a)
{
	overlay::Touch(a);
	json::Value v = ArtSummary(a, 0);
	json::Value path = PathJson(a);
	v["closed"] = path.get("closed");
	v["segments"] = path.get("segments");
	return v;
}

json::Value PathSetSegments(const json::Value& p)
{
	AIArtHandle a = OnePath(p);
	const json::Value& list = Required(p, "segments");
	if (!list.isArray()) Fail(kErrInvalidParams, "'segments' must be an array");
	std::vector<AIPathSegment> segs;
	for (const json::Value& v : list.asArray()) segs.push_back(SegmentFrom(v));
	WriteSegments(a, segs);
	if (p.has("closed")) Check(sAIPath->SetPathClosed(a, p.boolean("closed", false)), "SetPathClosed");
	return Edited(a);
}

// Moving an anchor ('p' or 'by') carries its handles along, as the Direct Selection tool does;
// 'in' / 'out' then place the handles exactly.
json::Value PathEditPoint(const json::Value& p)
{
	AIArtHandle a = OnePath(p);
	std::vector<AIPathSegment> segs = ReadSegments(a);
	AIPathSegment& s = segs[Index(Required(p, "index"), segs.size(), segs.size())];
	double dx = 0, dy = 0;
	if (p.has("p")) { AIRealPoint to = Point(p.get("p"), "p"); dx = to.h - s.p.h; dy = to.v - s.p.v; }
	if (p.has("by")) { AIRealPoint by = Point(p.get("by"), "by"); dx += by.h; dy += by.v; }
	for (AIRealPoint* pt : {&s.p, &s.in, &s.out}) { pt->h += (AIReal) dx; pt->v += (AIReal) dy; }
	if (p.has("in")) s.in = Point(p.get("in"), "in");
	if (p.has("out")) s.out = Point(p.get("out"), "out");
	if (p.has("smooth")) s.corner = !p.boolean("smooth", false);
	WriteSegments(a, segs);
	return Edited(a);
}

json::Value PathInsertPoint(const json::Value& p)
{
	AIArtHandle a = OnePath(p);
	std::vector<AIPathSegment> segs = ReadSegments(a);
	size_t at = p.has("index") ? Index(p.get("index"), segs.size() + 1, segs.size()) : segs.size();
	segs.insert(segs.begin() + (std::ptrdiff_t) at, SegmentFrom(Required(p, "point")));
	WriteSegments(a, segs);
	return Edited(a);
}

json::Value PathDeletePoints(const json::Value& p)
{
	AIArtHandle a = OnePath(p);
	std::vector<AIPathSegment> segs = ReadSegments(a);
	std::vector<size_t> gone = Indices(p, segs.size());
	if (gone.size() >= segs.size()) Fail(kErrInvalidParams, "that would delete every point; use art.delete to remove the path");
	for (auto i = gone.rbegin(); i != gone.rend(); ++i) segs.erase(segs.begin() + (std::ptrdiff_t) *i);
	WriteSegments(a, segs);
	return Edited(a);
}

// Selects just these anchors, as the Direct Selection tool would; everything else is deselected.
json::Value PathSelectPoints(const json::Value& p)
{
	AIArtHandle a = OnePath(p);
	ai::int16 count = 0;
	sAIPath->GetPathSegmentCount(a, &count);
	std::vector<size_t> chosen = Indices(p, (size_t) count);
	Need(sAIMatchingArt, "The matching art suite")->DeselectAll();
	for (size_t i : chosen) Check(sAIPath->SetPathSegmentSelected(a, (ai::int16) i, kSegmentPointSelected), "SetPathSegmentSelected");
	if (sAIDocument) sAIDocument->RedrawDocument();
	return Edited(a);
}

json::Value PathRemoveSelectedAnchors(const json::Value&)
{
	ActiveDocument();
	if (SelectedArt().empty()) Fail(kErrInvalidParams, "no anchor points are selected; use path.selectPoints first");
	AICommandID id = 0;
	if (!sAICommandManager || sAICommandManager->GetCommandIDFromName("Remove Anchor Points menu", &id) || !id)
		Fail(kErrUnavailable, "Illustrator has no 'Remove Anchor Points' command here");
	Check(sAIMenu->InvokeMenuAction(id), "Remove Anchor Points");
	if (sAIDocument) sAIDocument->SyncDocument();
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : SelectedArt()) out.push(ArtSummary(a, 0));
	return out;
}

// ---- outline, expand, offset

json::Value ArtOutline(const json::Value& p)
{
	Need(sAIArtConverter, "The art converter suite");
	ai::int32 options = kOutlineExpandAppearance | kOutlineEliminateUnpainted | (p.boolean("strokes", true) ? kOutlineAddStrokes : 0);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIArtHandle art = a;
		Check(sAIArtConverter->ArtConvertToOutline(&art, options), "ArtConvertToOutline");
		out.push(ArtSummary(art, 1));
	}
	return out;
}

json::Value ArtToPaths(const json::Value& p)
{
	Need(sAIArtConverter, "The art converter suite");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIArtHandle art = a;
		Check(sAIArtConverter->ArtConvertToPaths(&art), "ArtConvertToPaths");
		out.push(ArtSummary(art, 1));
	}
	return out;
}

json::Value ArtExpandAppearance(const json::Value& p)
{
	Need(sAIArtStyle, "The art style suite");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIArtHandle parent = nullptr, prior = nullptr;
		sAIArt->GetArtParent(a, &parent);
		sAIArt->GetArtPriorSibling(a, &prior);
		Check(sAIArtStyle->FlattenStyle(a), "FlattenStyle");
		// The result takes the art's place in the stack.
		AIArtHandle result = nullptr;
		if (prior) sAIArt->GetArtSibling(prior, &result);
		else if (parent) sAIArt->GetArtFirstChild(parent, &result);
		if (result) out.push(ArtSummary(result, 1));
	}
	return out;
}

json::Value ArtExpand(const json::Value& p)
{
	Need(sAIExpand, "The expand suite");
	ai::int32 flags = kExpandPluginArt | kExpandText | kExpandSymbolInstances | kExpandRepeatArt | kExpandDimensionGroup;
	if (p.boolean("stroke", true)) flags |= kExpandStroke;
	if (p.boolean("fill", true)) flags |= kExpandPattern | (p.str("gradient", "paths") == "mesh" ? kExpandGradientToMesh : kExpandGradientToPaths);
	std::vector<AIArtHandle> arts = ArtList(p, true);
	SelectOnly(arts);
	for (AIArtHandle a : arts) Check(sAIExpand->Expand(a, flags, (ai::int32) p.num("steps", 255)), "Expand");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : SelectedArt()) out.push(ArtSummary(a, 1));
	return out;
}

// Offset Path: the live effect ("Adobe Offset Path": ofst, jntp, mlim), on a copy, expanded.
json::Value PathOffset(const json::Value& p)
{
	Need(sAILiveEffect, "The live effect suite");
	std::string join = p.str("join", "miter");
	int jntp = join == "round" ? 0 : join == "bevel" ? 1 : join == "miter" ? 2 : -1;
	if (jntp < 0) Fail(kErrInvalidParams, "'join' must be miter, round or bevel");
	json::Value settings;
	settings["ofst"]["type"] = "real";
	settings["ofst"]["value"] = ReqNum(p, "distance");
	settings["jntp"] = jntp;
	settings["mlim"]["type"] = "real";
	settings["mlim"]["value"] = p.num("miterLimit", 4);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIArtHandle target = a;
		if (p.boolean("keepOriginal", true)) Check(sAIArt->DuplicateArt(a, kPlaceAbove, a, &target), "DuplicateArt");
		json::Value call;
		call["id"] = ArtId(target);
		call["effect"] = "Adobe Offset Path";
		call["settings"] = settings;
		RunCommand("effect.apply", call);
		json::Value expand;
		expand["id"] = ArtId(target);
		json::Value made = ArtExpandAppearance(expand);
		for (const json::Value& m : made.asArray()) out.push(m);
	}
	return out;
}

// ---- envelopes

AIWarpStyle WarpStyle(const std::string& s)
{
	static const std::pair<const char*, AIWarpStyle> styles[] = {
		{"arc", kWarpStyleArc}, {"arcLower", kWarpStyleArcLower}, {"arcUpper", kWarpStyleArcUpper}, {"arch", kWarpStyleArch},
		{"bulge", kWarpStyleBulge}, {"shellLower", kWarpStyleShellLower}, {"shellUpper", kWarpStyleShellUpper}, {"flag", kWarpStyleFlag},
		{"wave", kWarpStyleWave}, {"fish", kWarpStyleFish}, {"rise", kWarpStyleRise}, {"fisheye", kWarpStyleFisheye},
		{"inflate", kWarpStyleInflate}, {"squeeze", kWarpStyleSqueeze}, {"twist", kWarpStyleTwist}};
	for (auto& st : styles) if (Lower(s) == Lower(st.first)) return st.second;
	Fail(kErrInvalidParams, "'style' must be arc, arcLower, arcUpper, arch, bulge, shellLower, shellUpper, flag, wave, fish, rise, fisheye, inflate, squeeze or twist");
}

json::Value EnvelopeWarp(const json::Value& p)
{
	Need(sAIEnvelope, "The envelope suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIArtHandle envelope = nullptr;
	Check(sAIEnvelope->MakeEnvelope(arts.data(), (ai::int32) arts.size(), nullptr, nullptr, &envelope), "MakeEnvelope");
	Check(sAIEnvelope->WarpEnvelope(envelope, WarpStyle(p.str("style", "arc")), (AIReal) (p.num("bend", 50) / 100),
		(AIReal) (p.num("horizontal", 0) / 100), (AIReal) (p.num("vertical", 0) / 100), p.str("direction", "horizontal") == "vertical"), "WarpEnvelope");
	return ArtSummary(envelope, 1);
}

json::Value EnvelopeFromTop(const json::Value& p)
{
	Need(sAIEnvelope, "The envelope suite");
	std::vector<AIArtHandle> arts = BackToFront(ArtList(p, true));
	if (arts.size() < 2) Fail(kErrInvalidParams, "needs the art and, on top of it, the shape to fit it into");
	AIArtHandle shape = arts.back();
	arts.pop_back();
	AIArtHandle envelope = nullptr;
	Check(sAIEnvelope->MakeEnvelope(arts.data(), (ai::int32) arts.size(), shape, nullptr, &envelope), "MakeEnvelope");
	return ArtSummary(envelope, 1);
}

json::Value EnvelopeRelease(const json::Value& p)
{
	Need(sAIEnvelope, "The envelope suite");
	for (AIArtHandle a : ArtList(p, true)) {
		if (!sAIEnvelope->IsEnvelope(a)) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't an envelope");
		Check(sAIEnvelope->ReleaseEnvelope(a), "ReleaseEnvelope");
	}
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : SelectedArt()) out.push(ArtSummary(a, 0));
	return out;
}

// ---- repeats

AIRealRect BoundsOf(const std::vector<AIArtHandle>& arts)
{
	AIRealRect all = {0, 0, 0, 0};
	bool any = false;
	for (AIArtHandle a : arts) {
		AIRealRect r;
		if (sAIArt->GetArtBounds(a, &r)) continue;
		if (!any) all = r;
		else { all.left = std::min(all.left, r.left); all.right = std::max(all.right, r.right); all.top = std::max(all.top, r.top); all.bottom = std::min(all.bottom, r.bottom); }
		any = true;
	}
	return all;
}

ai::AutoBuffer<AIArtHandle> Buffer(const std::vector<AIArtHandle>& arts)
{
	ai::AutoBuffer<AIArtHandle> buf(arts.size());
	for (size_t i = 0; i < arts.size(); i++) buf[i] = arts[i];
	return buf;
}

json::Value RepeatRadial(const json::Value& p)
{
	Need(sAIRepeat, "The repeat suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIRealRect b = BoundsOf(arts);
	AIRepeat::RadialConfig config;
	config.radius = (AIReal) p.num("radius", std::max(b.right - b.left, b.top - b.bottom));
	// Default center: below the art, so the art sits at the top of the circle.
	config.center = p.has("center") ? Point(p.get("center"), "center") : AIRealPoint{(b.left + b.right) / 2, (b.top + b.bottom) / 2 - config.radius};
	config.numberOfInstances = (ai::uint32) std::max(2.0, p.num("count", 8));
	config.startAngle = (AIReal) (p.num("startAngle", 0) * kPi / 180);
	config.totalAngle = (AIReal) (p.num("totalAngle", 360) * kPi / 180);
	AIArtHandle made = nullptr;
	Check(sAIRepeat->CreateRadialRepeatArt(Buffer(arts), config, made), "CreateRadialRepeatArt");
	return ArtSummary(made, 1);
}

json::Value RepeatGrid(const json::Value& p)
{
	Need(sAIRepeat, "The repeat suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIRepeat::GridConfig config;
	config.bounds = BoundsOf(arts);
	config.horizontalSpacing = (AIReal) p.num("spacingX", 10);
	config.verticalSpacing = (AIReal) p.num("spacingY", 10);
	AIArtHandle made = nullptr;
	Check(sAIRepeat->CreateGridRepeatArt(Buffer(arts), config, made), "CreateGridRepeatArt");
	return ArtSummary(made, 1);
}

json::Value RepeatMirror(const json::Value& p)
{
	Need(sAIRepeat, "The repeat suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIRealRect b = BoundsOf(arts);
	AIRepeat::SymmetryConfig config;
	// Default axis: vertical, just right of the art.
	config.axisCenter = p.has("axisCenter") ? Point(p.get("axisCenter"), "axisCenter") : AIRealPoint{b.right + 10, (b.top + b.bottom) / 2};
	config.axisRotationAngleInRadians = (AIReal) (p.num("axisAngle", 90) * kPi / 180);
	config.axisNormalAngleInRadians = config.axisRotationAngleInRadians + (AIReal) (kPi / 2);
	AIArtHandle made = nullptr;
	Check(sAIRepeat->CreateSymmetryRepeatArt(Buffer(arts), config, made), "CreateSymmetryRepeatArt");
	return ArtSummary(made, 1);
}

// ---- menu-only operations

json::Value RunMenuOn(const json::Value& p, const char* command, size_t minimum)
{
	std::vector<AIArtHandle> arts = ArtList(p, true);
	if (arts.size() < minimum) Fail(kErrInvalidParams, "needs at least " + std::to_string(minimum) + " objects");
	AICommandID id = 0;
	if (!sAICommandManager || sAICommandManager->GetCommandIDFromName(command, &id) || !id)
		Fail(kErrUnavailable, std::string("Illustrator has no '") + command + "' command here");
	SelectOnly(arts);
	Check(sAIMenu->InvokeMenuAction(id), command);
	if (sAIDocument) sAIDocument->SyncDocument();
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : SelectedArt()) out.push(ArtSummary(a, 1));
	return out;
}

} // namespace

void AddShapeCommands(CommandTable& t)
{
	auto place = [](std::initializer_list<std::pair<const char*, const char*>> extra) {
		json::Value v = Params(extra);
		json::Value common = Params({{"fill", kPaint}, {"stroke", kPaint}, {"strokeWidth", "number"}, {"name", "string"}, {"layer", kWhere}, {"parent", kWhere}, {"select", "boolean"}});
		for (auto& kv : common.asObject()) v[kv.first] = kv.second;
		return v;
	};
	t["shape.roundedRect"] = {"Rounded rectangle; (x, y) is its top-left corner.",
		place({{"x", "number"}, {"y", "number"}, {"width", "number"}, {"height", "number"}, {"radius", "number | [rx, ry]"}}), ShapeRoundedRect, true};
	t["shape.polygon"] = {"Regular polygon around a center.", place({{"center", "[x, y]"}, {"radius", "number"}, {"sides", "number (default 6)"}}), ShapePolygon, true};
	t["shape.star"] = {"Star around a center.", place({{"center", "[x, y]"}, {"radius", "number - outer"}, {"innerRadius", "number (default half)"}, {"points", "number (default 5)"}}), ShapeStar, true};
	t["shape.spiral"] = {"Spiral from a center.", place({{"center", "[x, y]"}, {"radius", "number"}, {"decay", "percent (default 80)"}, {"segments", "quarter turns (default 10)"}, {"clockwise", "boolean"}}), ShapeSpiral, true};
	t["shape.pie"] = {"Elliptical pie (wedge).", place({{"center", "[x, y]"}, {"width", "number"}, {"height", "number"}, {"startAngle", "degrees (default 0)"}, {"endAngle", "degrees (default 270)"}, {"rotation", "degrees"}}), ShapePie, true};

	const char* ids = "string[] - art ids (default: the selection)";
	struct { const char* name; PathfinderOp AIPathfinderSuite::* op; const char* what; } ops[] = {
		{"unite", &AIPathfinderSuite::DoUniteEffect, "one shape from all of them"},
		{"intersect", &AIPathfinderSuite::DoIntersectEffect, "only where they all overlap"},
		{"exclude", &AIPathfinderSuite::DoExcludeEffect, "everything but where they overlap"},
		{"minusFront", &AIPathfinderSuite::DoBackMinusFrontEffect, "the back shape with the front ones cut away"},
		{"minusBack", &AIPathfinderSuite::DoFrontMinusBackEffect, "the front shape with the back ones cut away"},
		{"divide", &AIPathfinderSuite::DoDivideEffect, "split into every separate piece"},
		{"trim", &AIPathfinderSuite::DoTrimEffect, "remove hidden parts, keep colors"},
		{"merge", &AIPathfinderSuite::DoMergeEffect, "trim, then join same-colored pieces"},
		{"crop", &AIPathfinderSuite::DoCropEffect, "cut everything to the top shape"},
		{"outline", &AIPathfinderSuite::DoOutlineEffect, "the outlines, as open paths"}};
	for (auto& op : ops) {
		auto fn = op.op;
		const char* name = op.name;
		t[std::string("pathfinder.") + op.name] = {std::string("Pathfinder ") + op.name + ": " + op.what + ". Returns the result.",
			Params({{"ids", ids}, {"id", "string"}}), [fn, name](const json::Value& p) { return Pathfind(p, fn, name); }, true};
	}

	t["compound.make"] = {"Make a compound path (holes where paths overlap), like Object > Compound Path > Make.", Params({{"ids", ids}, {"name", "string"}}), CompoundMake, true};
	t["compound.release"] = {"Release compound paths back into separate paths.", Params({{"ids", ids}, {"id", "string"}}), CompoundRelease, true};
	t["path.measure"] = {"Length, area, anchor count and direction of paths.", Params({{"ids", ids}, {"id", "string"}}), PathMeasure, false};
	t["path.pointAt"] = {"The point and direction a share of the way along a path ('at' 0-1, or 'distance' in points).",
		Params({{"id", "string"}, {"at", "number 0-1 (default 0.5)"}, {"distance", "number - points from the start"}}), PathPointAt, false};
	t["path.reverse"] = {"Reverse path direction.", Params({{"ids", ids}, {"id", "string"}}), PathReverse, true};
	t["path.setClosed"] = {"Close or open paths.", Params({{"ids", ids}, {"id", "string"}, {"closed", "boolean (default true)"}}), PathSetClosed, true};
	t["path.simplify"] = {"Fewer anchor points for the same shape; corners are kept. 'tolerance' in points (default 0.5).",
		Params({{"ids", ids}, {"id", "string"}, {"tolerance", "number"}}), PathSimplify, true};
	t["path.offset"] = {"Offset Path: a new path 'distance' outside (negative: inside) each object.",
		Params({{"ids", ids}, {"id", "string"}, {"distance", "number - points"}, {"join", "miter (default) | round | bevel"}, {"miterLimit", "number"}, {"keepOriginal", "boolean (default true)"}}), PathOffset, true};
	t["path.outlineStroke"] = {"Object > Path > Outline Stroke: strokes become filled shapes.", Params({{"ids", ids}, {"id", "string"}}),
		[](const json::Value& p) { return RunMenuOn(p, "OffsetPath v22", 1); }, true};
	t["path.join"] = {"Object > Path > Join: connect open paths' end points.", Params({{"ids", ids}}), [](const json::Value& p) { return RunMenuOn(p, "join", 1); }, true};
	t["path.addAnchors"] = {"Object > Path > Add Anchor Points: one between each pair.", Params({{"ids", ids}, {"id", "string"}}), [](const json::Value& p) { return RunMenuOn(p, "Add Anchor Points2", 1); }, true};
	t["path.removeAnchors"] = {"Object > Path > Remove Anchor Points: removes the anchors selected now (path.selectPoints, or the user's Direct Selection) "
		"and closes the gap. To delete by index, use path.deletePoints.", Params({}), PathRemoveSelectedAnchors, true};
	const char* point = "[x, y] | {p: [x, y], in: [x, y], out: [x, y], smooth: boolean}";
	t["path.setSegments"] = {"Replace a path's points in place, keeping its id, style and stacking. 'segments' is the list art.get returns, edited.",
		Params({{"id", "string"}, {"segments", "array of points"}, {"closed", "boolean (default: unchanged)"}}), PathSetSegments, true};
	t["path.editPoint"] = {"Change one anchor point. 'p' or 'by' moves it with its handles; 'in' / 'out' set the handles; 'smooth' sets the point type. "
		"'index' is 0-based in path order (negative counts from the end).",
		Params({{"id", "string"}, {"index", "number"}, {"p", "[x, y] - move to"}, {"by", "[dx, dy] - move by"}, {"in", "[x, y]"}, {"out", "[x, y]"}, {"smooth", "boolean"}}), PathEditPoint, true};
	t["path.insertPoint"] = {"Insert an anchor point before 'index' (default: after the last point).",
		Params({{"id", "string"}, {"index", "number"}, {"point", point}}), PathInsertPoint, true};
	t["path.deletePoints"] = {"Delete anchor points by index. The path's shape closes over the gap; at least one point must remain.",
		Params({{"id", "string"}, {"index", "number"}, {"indices", "number[]"}}), PathDeletePoints, true};
	t["path.selectPoints"] = {"Select individual anchor points (Direct Selection); all other art is deselected. art.get marks selected points.",
		Params({{"id", "string"}, {"index", "number"}, {"indices", "number[] ([] deselects all)"}}), PathSelectPoints, false};
	t["art.outline"] = {"Convert art to the single outline of everything visible (fill and stroke), in place.",
		Params({{"ids", ids}, {"id", "string"}, {"strokes", "boolean - include strokes (default true)"}}), ArtOutline, true};
	t["art.toPaths"] = {"Convert live shapes and other plug-in art to plain paths.", Params({{"ids", ids}, {"id", "string"}}), ArtToPaths, true};
	t["art.expand"] = {"Object > Expand: text, symbols, gradients, patterns, strokes to plain art.",
		Params({{"ids", ids}, {"id", "string"}, {"fill", "boolean (default true)"}, {"stroke", "boolean (default true)"}, {"gradient", "paths (default) | mesh"}, {"steps", "number - gradient steps"}}), ArtExpand, true};
	t["art.expandAppearance"] = {"Object > Expand Appearance: effects and multiple fills / strokes become real art.",
		Params({{"ids", ids}, {"id", "string"}}), ArtExpandAppearance, true};
	t["envelope.warp"] = {"Envelope Distort > Make with Warp.",
		Params({{"ids", ids}, {"style", "arc | arcLower | arcUpper | arch | bulge | shellLower | shellUpper | flag | wave | fish | rise | fisheye | inflate | squeeze | twist"},
			{"bend", "percent -100..100 (default 50)"}, {"horizontal", "percent distortion"}, {"vertical", "percent distortion"}, {"direction", "horizontal (default) | vertical"}}), EnvelopeWarp, true};
	t["envelope.fromTop"] = {"Envelope Distort > Make with Top Object: fit the art into the topmost shape.", Params({{"ids", ids}}), EnvelopeFromTop, true};
	t["envelope.release"] = {"Release envelopes: the art and the envelope shape come apart.", Params({{"ids", ids}, {"id", "string"}}), EnvelopeRelease, true};
	t["envelope.expand"] = {"Envelope Distort > Expand: the distorted art becomes plain art.", Params({{"ids", ids}, {"id", "string"}}), [](const json::Value& p) { return RunMenuOn(p, "Expand Envelope", 1); }, true};
	t["repeat.radial"] = {"Object > Repeat > Radial.", Params({{"ids", ids}, {"count", "number (default 8)"}, {"radius", "number"}, {"center", "[x, y]"}, {"startAngle", "degrees"}, {"totalAngle", "degrees (default 360)"}}), RepeatRadial, true};
	t["repeat.grid"] = {"Object > Repeat > Grid.", Params({{"ids", ids}, {"spacingX", "number"}, {"spacingY", "number"}}), RepeatGrid, true};
	t["repeat.mirror"] = {"Object > Repeat > Mirror.", Params({{"ids", ids}, {"axisCenter", "[x, y]"}, {"axisAngle", "degrees (default 90: vertical)"}}), RepeatMirror, true};
	t["blend.make"] = {"Object > Blend > Make, between the objects.", Params({{"ids", ids}}), [](const json::Value& p) { return RunMenuOn(p, "Path Blend Make", 2); }, true};
	t["blend.release"] = {"Object > Blend > Release.", Params({{"ids", ids}, {"id", "string"}}), [](const json::Value& p) { return RunMenuOn(p, "Path Blend Release", 1); }, true};
	t["blend.expand"] = {"Object > Blend > Expand: the steps become real objects.", Params({{"ids", ids}, {"id", "string"}}), [](const json::Value& p) { return RunMenuOn(p, "Path Blend Expand", 1); }, true};
	t["livePaint.make"] = {"Object > Live Paint > Make.", Params({{"ids", ids}}), [](const json::Value& p) { return RunMenuOn(p, "Make Planet X", 1); }, true};
	t["livePaint.expand"] = {"Object > Live Paint > Expand.", Params({{"ids", ids}, {"id", "string"}}), [](const json::Value& p) { return RunMenuOn(p, "Expand Planet X", 1); }, true};
	t["livePaint.release"] = {"Object > Live Paint > Release.", Params({{"ids", ids}, {"id", "string"}}), [](const json::Value& p) { return RunMenuOn(p, "Release Planet X", 1); }, true};
}

} // namespace slippy
