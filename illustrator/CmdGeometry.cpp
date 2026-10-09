// Geometry questions about art: how close one object's real outline comes to a
// path (art.distance), and what is stacked above an object and overlaps it
// (art.above). Both answer in one call what would otherwise take a round trip
// per object, and both treat a clip group as what it shows: its mask.

#include "Kit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace slippy {

namespace {

struct Pt { double x, y; };
struct Poly {
	std::vector<Pt> pts;
	bool closed = false;
	double l = 0, t = 0, r = 0, b = 0;   // bounds: left, top, right, bottom (y up)
};

void Bound(Poly& p)
{
	if (p.pts.empty()) return;
	p.l = p.r = p.pts[0].x; p.t = p.b = p.pts[0].y;
	for (const Pt& q : p.pts) {
		p.l = std::min(p.l, q.x); p.r = std::max(p.r, q.x);
		p.b = std::min(p.b, q.y); p.t = std::max(p.t, q.y);
	}
}

Poly RectPoly(const AIRealRect& r)
{
	Poly p;
	p.pts = {{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
	p.closed = true;
	Bound(p);
	return p;
}

// A path's outline as a polyline: each Bezier segment flattened into short chords.
Poly PathPoly(AIArtHandle art)
{
	Poly p;
	ai::int16 count = 0;
	AIBoolean closed = false;
	sAIPath->GetPathSegmentCount(art, &count);
	sAIPath->GetPathClosed(art, &closed);
	if (!count) return p;
	std::vector<AIPathSegment> s((size_t) count);
	sAIPath->GetPathSegments(art, 0, count, s.data());
	auto curve = [&](const AIPathSegment& a, const AIPathSegment& b) {
		bool straight = a.out.h == a.p.h && a.out.v == a.p.v && b.in.h == b.p.h && b.in.v == b.p.v;
		int steps = straight ? 1 : 12;
		for (int k = 1; k <= steps; k++) {
			double t = (double) k / steps, u = 1 - t;
			double x = u * u * u * a.p.h + 3 * u * u * t * a.out.h + 3 * u * t * t * b.in.h + t * t * t * b.p.h;
			double y = u * u * u * a.p.v + 3 * u * u * t * a.out.v + 3 * u * t * t * b.in.v + t * t * t * b.p.v;
			p.pts.push_back({x, y});
		}
	};
	p.pts.push_back({s[0].p.h, s[0].p.v});
	for (size_t i = 1; i < s.size(); i++) curve(s[i - 1], s[i]);
	if (closed && s.size() > 1) curve(s.back(), s[0]);
	p.closed = closed;
	Bound(p);
	return p;
}

AIArtHandle ClipMaskOf(AIArtHandle group)
{
	AIArtHandle child = nullptr;
	for (sAIArt->GetArtFirstChild(group, &child); child; sAIArt->GetArtSibling(child, &child))
		if (Attr(child, kArtIsClipMask)) return child;
	return nullptr;
}

bool IsClipped(AIArtHandle art)
{
	AIBoolean clipped = false;
	return ArtType(art) == kGroupArt && sAIGroup && !sAIGroup->GetGroupClipped(art, &clipped) && clipped;
}

// Everything visible that makes up an art object's outline. A clip group is its mask
// (what it can show); a group is its children; anything without path geometry (text,
// images, symbols, meshes) is its bounding box.
void Outline(AIArtHandle art, std::vector<Poly>& out, bool viaMask = false)
{
	if (!viaMask && Attr(art, kArtHidden)) return;
	short type = ArtType(art);
	if (type == kPathArt) {
		Poly p = PathPoly(art);
		if (p.pts.size() > 1) out.push_back(std::move(p));
		return;
	}
	if (type == kGroupArt && IsClipped(art)) {
		if (AIArtHandle mask = ClipMaskOf(art)) { Outline(mask, out, true); return; }
	}
	if (type == kGroupArt || type == kCompoundPathArt) {
		AIArtHandle child = nullptr;
		for (sAIArt->GetArtFirstChild(art, &child); child; sAIArt->GetArtSibling(child, &child))
			if (type == kCompoundPathArt || !Attr(child, kArtIsClipMask)) Outline(child, out, viaMask);
		return;
	}
	AIRealRect r;
	if (!sAIArt->GetArtBounds(art, &r)) out.push_back(RectPoly(r));
}

double PointSeg(Pt p, Pt a, Pt b, Pt& on)
{
	double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
	double t = len > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.0, 1.0) : 0;
	on = {a.x + t * dx, a.y + t * dy};
	return std::hypot(p.x - on.x, p.y - on.y);
}

bool Crosses(Pt a, Pt b, Pt c, Pt d)
{
	auto cross = [](Pt o, Pt p, Pt q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
	double d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
	return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

double SegDist(Pt a, Pt b, Pt c, Pt d, Pt& on1, Pt& on2)
{
	if (Crosses(a, b, c, d)) {
		double den = (b.x - a.x) * (d.y - c.y) - (b.y - a.y) * (d.x - c.x);
		double t = den ? ((c.x - a.x) * (d.y - c.y) - (c.y - a.y) * (d.x - c.x)) / den : 0;
		on1 = on2 = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
		return 0;
	}
	Pt q, best1 = a, best2 = c;
	double best = PointSeg(a, c, d, q); best2 = q;
	double v = PointSeg(b, c, d, q);
	if (v < best) { best = v; best1 = b; best2 = q; }
	v = PointSeg(c, a, b, q);
	if (v < best) { best = v; best1 = q; best2 = c; }
	v = PointSeg(d, a, b, q);
	if (v < best) { best = v; best1 = q; best2 = d; }
	on1 = best1; on2 = best2;
	return best;
}

double RectGap(const Poly& p, double l, double t, double r, double b)
{
	double dx = std::max({0.0, l - p.r, p.l - r}), dy = std::max({0.0, b - p.t, p.b - t});
	return std::hypot(dx, dy);
}

bool Inside(Pt p, const Poly& poly)
{
	bool in = false;
	const auto& v = poly.pts;
	for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
		if ((v[i].y > p.y) != (v[j].y > p.y) && p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x) in = !in;
	return in;
}

json::Value PtJson(Pt p)
{
	json::Value v = json::Value::MakeArray();
	v.push(p.x);
	v.push(p.y);
	return v;
}

json::Value DistanceOf(AIArtHandle art, const std::vector<Poly>& target)
{
	std::vector<Poly> pieces;
	Outline(art, pieces);
	json::Value v;
	v["id"] = ArtId(art);
	v["pieces"] = (double) pieces.size();
	if (pieces.empty() || target.empty()) { v["distance"] = json::Value(); return v; }
	double best = std::numeric_limits<double>::max();
	Pt from{0, 0}, to{0, 0};
	for (const Poly& piece : pieces) {
		for (const Poly& tp : target) {
			// skip a piece that can't beat the best so far
			if (RectGap(piece, tp.l, tp.t, tp.r, tp.b) >= best && !(piece.l <= tp.r && piece.r >= tp.l && piece.b <= tp.t && piece.t >= tp.b)) continue;
			size_t n = piece.pts.size() + (piece.closed ? 0 : -1), m = tp.pts.size() + (tp.closed ? 0 : -1);
			for (size_t i = 0; i < n; i++) {
				Pt a = piece.pts[i], b = piece.pts[(i + 1) % piece.pts.size()];
				for (size_t j = 0; j < m; j++) {
					Pt c = tp.pts[j], d = tp.pts[(j + 1) % tp.pts.size()], o1, o2;
					double dist = SegDist(a, b, c, d, o1, o2);
					if (dist < best) { best = dist; from = o1; to = o2; }
				}
			}
		}
	}
	AIRealRect r;
	sAIArt->GetArtBounds(art, &r);
	Pt center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
	bool inside = false;
	for (const Poly& tp : target) if (tp.closed && Inside(center, tp)) inside = !inside;
	v["distance"] = best;
	v["crosses"] = best == 0;
	v["inside"] = inside;
	v["from"] = PtJson(from);
	v["to"] = PtJson(to);
	return v;
}

json::Value ArtDistance(const json::Value& p)
{
	ActiveDocument();
	Need(sAIPath, "The path suite");
	AIArtHandle toArt = ArtById(IdText(Required(p, "to")));
	std::vector<Poly> target;
	Outline(toArt, target, true);
	if (target.empty()) Fail(kErrInvalidParams, "'to' has no outline to measure against");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	if (arts.empty()) Fail(kErrInvalidParams, "give 'id' or 'ids' (or select the art)");
	if (arts.size() == 1 && !p.get("ids").isArray()) return DistanceOf(arts[0], target);
	json::Value list = json::Value::MakeArray();
	for (AIArtHandle a : arts) list.push(DistanceOf(a, target));
	return list;
}

// ---- art.above

struct AboveWalk {
	AIArtHandle target;
	std::set<AIArtHandle> ancestors;
	AIRealRect box;
	bool includeUnpainted;
	json::Value hits = json::Value::MakeArray();
	bool done = false;

	bool Overlaps(const AIRealRect& r) const
	{
		return r.left < box.right && r.right > box.left && r.bottom < box.top && r.top > box.bottom;
	}

	void Visit(AIArtHandle art)
	{
		if (done) return;
		if (art == target) { done = true; return; }
		if (Attr(art, kArtHidden)) return;
		short type = ArtType(art);
		bool clipped = IsClipped(art);
		if (ancestors.count(art) || (type == kGroupArt && !clipped)) {
			AIArtHandle child = nullptr;
			for (sAIArt->GetArtFirstChild(art, &child); child && !done; sAIArt->GetArtSibling(child, &child))
				if (!Attr(child, kArtIsClipMask)) Visit(child);
			return;
		}
		AIRealRect r;
		AIArtHandle mask = clipped ? ClipMaskOf(art) : nullptr;
		if (sAIArt->GetArtBounds(mask ? mask : art, &r) || !Overlaps(r)) return;
		if (type == kPathArt || type == kCompoundPathArt) {
			// a compound path keeps its paint on its child paths
			AIArtHandle painter = art;
			if (type == kCompoundPathArt) sAIArt->GetArtFirstChild(art, &painter);
			json::Value style = painter ? StyleJson(painter) : json::Value();
			auto painted = [&](const char* k) { const json::Value& s = style.get(k); return !(s.isNull() || (s.isString() && s.asString() == "none")); };
			bool fill = painted("fill"), stroke = painted("stroke");
			if (!fill && !stroke && !includeUnpainted) return;
			json::Value h = ArtSummary(art, 0);
			h["fill"] = fill;
			h["stroke"] = stroke;
			hits.push(h);
			return;
		}
		hits.push(ArtSummary(art, 0));
	}
};

json::Value ArtAbove(const json::Value& p)
{
	ActiveDocument();
	AboveWalk w;
	w.target = ArtById(ReqStr(p, "id"));
	w.includeUnpainted = p.boolean("includeUnpainted", false);
	if (sAIArt->GetArtBounds(w.target, &w.box)) Fail(kErrInvalidParams, "the art has no bounds");
	for (AIArtHandle a = w.target; ; ) {
		AIArtHandle parent = nullptr;
		if (sAIArt->GetArtParent(a, &parent) || !parent) break;
		w.ancestors.insert(parent);
		a = parent;
	}
	ai::int32 count = 0;
	sAILayer->CountLayers(&count);
	for (ai::int32 i = 0; i < count && !w.done; i++) {
		AILayerHandle layer = nullptr;
		AIBoolean visible = true;
		if (sAILayer->GetNthLayer(i, &layer)) continue;
		sAILayer->GetLayerVisible(layer, &visible);
		AIArtHandle group = nullptr;
		if (sAIArt->GetFirstArtOfLayer(layer, &group) || !group) continue;
		bool own = w.ancestors.count(group) > 0;
		if (!visible && !own) continue;
		AIArtHandle child = nullptr;
		for (sAIArt->GetArtFirstChild(group, &child); child && !w.done; sAIArt->GetArtSibling(child, &child)) w.Visit(child);
	}
	json::Value v;
	v["id"] = ArtId(w.target);
	v["covered"] = w.hits.size() > 0;
	v["above"] = w.hits;
	return v;
}

} // namespace

void AddGeometryCommands(CommandTable& t)
{
	t["art.distance"] = {"How close art comes to a path: the shortest distance between their real outlines (Bezier curves, "
		"not bounding boxes), whether they cross, and whether the art's centre is inside the path. A clip group counts as its "
		"mask; text, images and symbols as their bounds. With 'ids', one result per object.",
		Params({{"to", "string - the path, compound path or clip group to measure against"}, {"id", "string"},
			{"ids", "string[] - several objects (default: the selection)"}}), ArtDistance, false};
	t["art.above"] = {"What is stacked above an object and overlaps it: the visible objects painted later than it whose "
		"bounds (a clip group's mask bounds) overlap its bounds, top first. Groups are looked into; a clip group, path, "
		"text or image is one entry. Paths with neither fill nor stroke are left out unless includeUnpainted.",
		Params({{"id", "string"}, {"includeUnpainted", "boolean - list paths with no fill and no stroke too (default false)"}}),
		ArtAbove, false};
}

} // namespace slippy
