// Named paint: swatches (and swatch groups), spot / global colors, gradients
// and patterns - listing, making, changing, deleting - plus the hooks that let
// every command's fill / stroke take them by name:
//   {"swatch": "Brand Red"}  {"spot": "PANTONE 185 C", "tint": 50}
//   {"gradient": "Sunset", "angle": 90}  {"pattern": "Dots", "scale": 50}

#include "Kit.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace slippy {

namespace {

const double kDegrees = 180.0 / 3.14159265358979323846;

AISwatchListRef Swatches()
{
	Need(sAISwatchList, "The swatch list suite");
	ActiveDocument();
	AISwatchListRef list = nullptr;
	Check(sAISwatchList->GetSwatchList(nullptr, &list), "GetSwatchList");
	return list;
}

std::string SwatchName(AISwatchRef s)
{
	ai::UnicodeString n;
	return sAISwatchList->GetSwatchName(s, n) ? "" : S(n);
}

AISwatchRef SwatchByName(const std::string& name)
{
	AISwatchRef s = sAISwatchList->GetSwatchByName(Swatches(), U(name));
	if (!s) Fail(kErrNotFound, "no swatch named '" + name + "' - see swatch.list");
	return s;
}

std::string CustomColorName(AICustomColorHandle h)
{
	ai::UnicodeString n;
	return sAICustomColor->GetCustomColorName(h, n) ? "" : S(n);
}

AICustomColorHandle SpotByName(const std::string& name)
{
	Need(sAICustomColor, "The custom color suite");
	ActiveDocument();
	AICustomColorHandle h = nullptr;
	if (sAICustomColor->GetCustomColorByName(U(name), &h) || !h) Fail(kErrNotFound, "no spot / global color named '" + name + "' - see spot.list");
	return h;
}

std::string GradientName(AIGradientHandle g)
{
	ai::UnicodeString n;
	return sAIGradient->GetGradientName(g, n) ? "" : S(n);
}

AIGradientHandle GradientByName(const std::string& name)
{
	Need(sAIGradient, "The gradient suite");
	ActiveDocument();
	AIGradientHandle g = nullptr;
	if (sAIGradient->GetGradientByName(U(name), &g) || !g) Fail(kErrNotFound, "no gradient named '" + name + "' - see gradient.list");
	return g;
}

std::string PatternName(AIPatternHandle p)
{
	ai::UnicodeString n;
	return sAIPattern->GetPatternName(p, n) ? "" : S(n);
}

AIPatternHandle PatternByName(const std::string& name)
{
	Need(sAIPattern, "The pattern suite");
	ActiveDocument();
	AIPatternHandle p = nullptr;
	if (sAIPattern->GetPatternByName(U(name), &p) || !p) Fail(kErrNotFound, "no pattern named '" + name + "' - see pattern.list");
	return p;
}

// Adds a swatch for new named paint, as the panels do (so people see it).
void AddSwatch(const AIColor& color, const std::string& name)
{
	if (!sAISwatchList) return;
	AISwatchRef s = sAISwatchList->InsertNthSwatch(Swatches(), -1);
	if (!s) return;
	AIColor c = color;
	sAISwatchList->SetAIColor(s, &c);
	sAISwatchList->SetSwatchName(s, U(name));
}

json::Value GradientJson(AIGradientHandle g)
{
	json::Value v;
	v["name"] = GradientName(g);
	ai::int16 type = 0;
	sAIGradient->GetGradientType(g, &type);
	v["type"] = type == kRadialGradient ? "radial" : "linear";
	json::Value stops = json::Value::MakeArray();
	ai::int16 count = 0;
	sAIGradient->GetGradientStopCount(g, &count);
	for (ai::int16 i = 0; i < count; i++) {
		AIGradientStop stop;
		if (sAIGradient->GetNthGradientStop(g, i, &stop)) continue;
		json::Value s;
		s["position"] = (double) stop.rampPoint;
		s["color"] = ColorJson(stop.color);
		s["midpoint"] = (double) stop.midPoint;
		if (stop.opacity < 1) s["opacity"] = stop.opacity * 100.0;
		stops.push(s);
	}
	v["stops"] = stops;
	return v;
}

// stops: [{color, position 0-100, midpoint 13-87, opacity 0-100}] (positions spread evenly when left out).
void SetStops(AIGradientHandle g, const json::Value& stops)
{
	if (!stops.isArray() || stops.size() < 2) Fail(kErrInvalidParams, "'stops' must be an array of at least 2 {color, position?, midpoint?, opacity?}");
	ai::int16 have = 0;
	sAIGradient->GetGradientStopCount(g, &have);
	size_t n = stops.size();
	for (size_t i = 0; i < n; i++) {
		const json::Value& s = stops.asArray()[i];
		AIGradientStop stop;
		stop.Init();
		stop.color = ParseColor(s.isObject() && s.has("color") ? s.get("color") : s, "stops[].color");
		if (stop.color.kind == kGradient || stop.color.kind == kPattern || stop.color.kind == kNoneColor)
			Fail(kErrInvalidParams, "gradient stops take solid colors (or spot colors)");
		stop.rampPoint = (AIReal) (s.isObject() && s.get("position").isNumber() ? s.get("position").asNumber() : 100.0 * i / (n - 1));
		stop.midPoint = (AIReal) std::max(13.0, std::min(87.0, s.isObject() ? s.num("midpoint", 50) : 50.0));
		stop.opacity = (AIReal) std::max(0.0, std::min(1.0, (s.isObject() ? s.num("opacity", 100) : 100.0) / 100.0));
		if ((ai::int16) i < have) Check(sAIGradient->SetNthGradientStop(g, (ai::int16) i, &stop), "SetNthGradientStop");
		else Check(sAIGradient->InsertGradientStop(g, (ai::int16) i, &stop), "InsertGradientStop");
	}
	for (ai::int16 i = have - 1; i >= (ai::int16) n; i--) {
		AIGradientStop gone;
		sAIGradient->DeleteGradientStop(g, i, &gone);
	}
}

// ---- swatches

json::Value SwatchList(const json::Value& p)
{
	AISwatchListRef list = Swatches();
	Need(sAISwatchGroup, "The swatch group suite");
	std::string only = p.str("group", "");
	json::Value groups = json::Value::MakeArray();
	ai::int32 count = sAISwatchGroup->CountSwatchGroups(list);
	for (ai::int32 i = 0; i < count; i++) {
		AISwatchGroupRef group = sAISwatchGroup->GetNthSwatchGroup(list, i);
		if (!group) continue;
		ai::UnicodeString gname;
		sAISwatchGroup->GetSwatchGroupName(group, gname);
		if (!only.empty() && S(gname) != only) continue;
		json::Value g;
		g["group"] = i == 0 && gname.empty() ? "(general)" : S(gname);
		json::Value items = json::Value::MakeArray();
		ai::int32 n = sAISwatchGroup->CountSwatches(group);
		for (ai::int32 k = 0; k < n; k++) {
			AISwatchRef s = sAISwatchGroup->GetNthSwatch(group, k);
			if (!s) continue;
			AIColor c;
			if (sAISwatchList->GetAIColor(s, &c)) continue;
			json::Value item;
			item["name"] = SwatchName(s);
			item["color"] = ColorJson(c);
			items.push(item);
		}
		g["swatches"] = items;
		groups.push(g);
	}
	return groups;
}

json::Value SwatchCreate(const json::Value& p)
{
	AISwatchListRef list = Swatches();
	std::string name = ReqStr(p, "name");
	AIColor color = ParseColor(Required(p, "color"), "color");
	AISwatchRef s = nullptr;
	if (p.get("group").isString()) {
		Need(sAISwatchGroup, "The swatch group suite");
		AISwatchGroupRef group = sAISwatchGroup->GetSwatchGroupByName(list, U(p.get("group").asString()));
		if (!group) Fail(kErrNotFound, "no swatch group named '" + p.get("group").asString() + "'");
		s = sAISwatchGroup->InsertNthSwatch(group, &color, -1);
	}
	else {
		s = sAISwatchList->InsertNthSwatch(list, -1);
		if (s) Check(sAISwatchList->SetAIColor(s, &color), "SetAIColor");
	}
	if (!s) Fail(kErrIllustrator, "Illustrator didn't make the swatch");
	Check(sAISwatchList->SetSwatchName(s, U(name)), "SetSwatchName");
	json::Value v;
	v["name"] = SwatchName(s);
	v["color"] = ColorJson(color);
	return v;
}

json::Value SwatchSet(const json::Value& p)
{
	AISwatchRef s = SwatchByName(ReqStr(p, "name"));
	if (p.has("color")) {
		AIColor c = ParseColor(p.get("color"), "color");
		Check(sAISwatchList->SetAIColor(s, &c), "SetAIColor");
	}
	if (p.get("rename").isString()) Check(sAISwatchList->SetSwatchName(s, U(p.get("rename").asString())), "SetSwatchName");
	AIColor c;
	sAISwatchList->GetAIColor(s, &c);
	json::Value v;
	v["name"] = SwatchName(s);
	v["color"] = ColorJson(c);
	return v;
}

json::Value SwatchDelete(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	AISwatchRef s = SwatchByName(name);
	Check(sAISwatchList->RemoveSwatch(Swatches(), s, p.boolean("deleteColor", false)), "RemoveSwatch");
	json::Value v;
	v["deleted"] = name;
	return v;
}

json::Value SwatchGroupCreate(const json::Value& p)
{
	AISwatchListRef list = Swatches();
	Need(sAISwatchGroup, "The swatch group suite");
	AISwatchGroupRef group = nullptr;
	Check(sAISwatchGroup->NewSwatchGroup(list, kAISGKindGeneric, -1, &group), "NewSwatchGroup");
	Check(sAISwatchGroup->SetSwatchGroupName(group, U(ReqStr(p, "name"))), "SetSwatchGroupName");
	// Colors to add, each a paint or {name, color}.
	const json::Value& colors = p.get("colors");
	if (colors.isArray()) {
		for (const json::Value& item : colors.asArray()) {
			AIColor c = ParseColor(item.isObject() && item.has("color") ? item.get("color") : item, "colors[]");
			AISwatchRef s = sAISwatchGroup->InsertNthSwatch(group, &c, -1);
			if (s && item.isObject() && item.get("name").isString()) sAISwatchList->SetSwatchName(s, U(item.get("name").asString()));
		}
	}
	json::Value v;
	v["group"] = ReqStr(p, "name");
	v["swatches"] = (double) sAISwatchGroup->CountSwatches(group);
	return v;
}

// ---- spot / global colors

json::Value SpotList(const json::Value&)
{
	Need(sAICustomColor, "The custom color suite");
	ActiveDocument();
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAICustomColor->CountCustomColors(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AICustomColorHandle h = nullptr;
		AICustomColor cc;
		if (sAICustomColor->GetNthCustomColor(i, &h) || !h || sAICustomColor->GetCustomColor(h, &cc)) continue;
		json::Value s;
		s["name"] = CustomColorName(h);
		s["spot"] = (cc.flag & kCustomSpotColor) != 0;
		if (cc.flag & kCustomRegistrationColor) s["registration"] = true;
		AIColor base;
		base.Init();
		if (cc.kind == kCustomFourColor) { base.kind = kFourColor; base.c.f = cc.c.f; }
		else if (cc.kind == kCustomThreeColor) { base.kind = kThreeColor; base.c.rgb = cc.c.rgb; }
		s["color"] = base.kind == kNoneColor ? json::Value("lab") : ColorJson(base);
		list.push(s);
	}
	return list;
}

json::Value SpotCreate(const json::Value& p)
{
	Need(sAICustomColor, "The custom color suite");
	ActiveDocument();
	std::string name = ReqStr(p, "name");
	AIColor base = ParseColor(Required(p, "color"), "color");
	AICustomColor cc;
	cc.Init();
	if (base.kind == kFourColor) { cc.kind = kCustomFourColor; cc.c.f = base.c.f; }
	else if (base.kind == kThreeColor) { cc.kind = kCustomThreeColor; cc.c.rgb = base.c.rgb; }
	else Fail(kErrInvalidParams, "'color' must be a solid rgb or cmyk color");
	cc.flag = p.boolean("spot", true) ? kCustomSpotColor : 0;   // false: a global process color
	AICustomColorHandle h = nullptr;
	Check(sAICustomColor->NewCustomColor(&cc, U(name), &h), "NewCustomColor");
	AIColor color;
	color.Init();
	color.kind = kCustomColor;
	color.c.c.color = h;
	color.c.c.tint = 0;
	if (p.boolean("swatch", true)) AddSwatch(color, name);
	json::Value v;
	v["name"] = CustomColorName(h);
	v["spot"] = (cc.flag & kCustomSpotColor) != 0;
	return v;
}

json::Value SpotDelete(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	Check(sAICustomColor->DeleteCustomColor(SpotByName(name)), "DeleteCustomColor");
	json::Value v;
	v["deleted"] = name;
	return v;
}

// ---- gradients

json::Value GradientList(const json::Value&)
{
	Need(sAIGradient, "The gradient suite");
	ActiveDocument();
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAIGradient->CountGradients(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AIGradientHandle g = nullptr;
		if (!sAIGradient->GetNthGradient(i, &g) && g) list.push(GradientJson(g));
	}
	return list;
}

void SetGradientType(AIGradientHandle g, const json::Value& p)
{
	if (!p.has("type")) return;
	std::string t = ReqStr(p, "type");
	if (t != "linear" && t != "radial") Fail(kErrInvalidParams, "'type' must be linear or radial");
	Check(sAIGradient->SetGradientType(g, t == "radial" ? kRadialGradient : kLinearGradient), "SetGradientType");
}

json::Value GradientCreate(const json::Value& p)
{
	Need(sAIGradient, "The gradient suite");
	ActiveDocument();
	std::string name = ReqStr(p, "name");
	AIGradientHandle g = nullptr;
	Check(sAIGradient->NewGradient(&g), "NewGradient");
	Check(sAIGradient->SetGradientName(g, U(name)), "SetGradientName");
	SetGradientType(g, p);
	SetStops(g, Required(p, "stops"));
	if (p.boolean("swatch", true)) {
		AIColor color;
		color.Init();
		color.kind = kGradient;
		color.c.b.Init();
		color.c.b.gradient = g;
		color.c.b.matrix.Init();
		AddSwatch(color, name);
	}
	return GradientJson(g);
}

json::Value GradientSet(const json::Value& p)
{
	AIGradientHandle g = GradientByName(ReqStr(p, "name"));
	SetGradientType(g, p);
	if (p.has("stops")) SetStops(g, p.get("stops"));
	if (p.get("rename").isString()) Check(sAIGradient->SetGradientName(g, U(p.get("rename").asString())), "SetGradientName");
	return GradientJson(g);
}

json::Value GradientDelete(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	Check(sAIGradient->DeleteGradient(GradientByName(name)), "DeleteGradient");
	json::Value v;
	v["deleted"] = name;
	return v;
}

// ---- patterns

json::Value PatternList(const json::Value&)
{
	Need(sAIPattern, "The pattern suite");
	ActiveDocument();
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAIPattern->CountPatterns(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AIPatternHandle pat = nullptr;
		if (sAIPattern->GetNthPattern(i, &pat) || !pat) continue;
		json::Value v;
		v["name"] = PatternName(pat);
		AIRealRect tile;
		if (!sAIPattern->GetPatternTileBounds(pat, &tile)) v["tile"] = RectJson(tile);
		list.push(v);
	}
	return list;
}

// A pattern tile from art (default: the selection): its bounds, or 'tile' [w, h]
// around its center for spacing.
json::Value PatternCreate(const json::Value& p)
{
	Need(sAIPattern, "The pattern suite");
	std::string name = ReqStr(p, "name");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIRealRect bounds;
	bool any = false;
	for (AIArtHandle a : arts) {
		AIRealRect r;
		if (sAIArt->GetArtBounds(a, &r)) continue;
		if (!any) bounds = r;
		else { bounds.left = std::min(bounds.left, r.left); bounds.right = std::max(bounds.right, r.right);
			bounds.top = std::max(bounds.top, r.top); bounds.bottom = std::min(bounds.bottom, r.bottom); }
		any = true;
	}
	if (!any) Fail(kErrInvalidParams, "that art has no bounds");
	if (p.has("tile")) {
		AIRealPoint size = Point(p.get("tile"), "tile");
		AIReal cx = (bounds.left + bounds.right) / 2, cy = (bounds.top + bounds.bottom) / 2;
		bounds.left = cx - size.h / 2; bounds.right = cx + size.h / 2;
		bounds.top = cy + size.v / 2; bounds.bottom = cy - size.v / 2;
	}
	// The group Illustrator wants: copies of the art over an unpainted tile rectangle.
	AIArtHandle group = nullptr, tile = nullptr;
	Check(sAIArt->NewArt(kGroupArt, kPlaceAboveAll, nullptr, &group), "NewArt");
	Check(sAIArt->NewArt(kPathArt, kPlaceInsideOnTop, group, &tile), "NewArt");
	AIPathSegment segs[4];
	AIRealPoint corners[4] = {{bounds.left, bounds.top}, {bounds.right, bounds.top}, {bounds.right, bounds.bottom}, {bounds.left, bounds.bottom}};
	for (int i = 0; i < 4; i++) { segs[i].p = segs[i].in = segs[i].out = corners[i]; segs[i].corner = true; }
	sAIPath->SetPathSegmentCount(tile, 4);
	sAIPath->SetPathSegments(tile, 0, 4, segs);
	sAIPath->SetPathClosed(tile, true);
	AIPathStyle none;
	sAIPathStyle->GetPathStyle(tile, &none, nullptr);
	none.fillPaint = none.strokePaint = false;
	sAIPathStyle->SetPathStyle(tile, &none);
	for (auto it = arts.rbegin(); it != arts.rend(); ++it) {
		AIArtHandle copy = nullptr;
		sAIArt->DuplicateArt(*it, kPlaceInsideOnTop, group, &copy);
	}
	AIPatternHandle pat = nullptr;
	AIErr e = sAIPattern->NewPattern(&pat);
	if (!e) e = sAIPattern->SetPatternArt(pat, group);   // copies the group
	sAIArt->DisposeArt(group);
	Check(e, "SetPatternArt");
	Check(sAIPattern->SetPatternName(pat, U(name)), "SetPatternName");
	if (p.boolean("swatch", true)) {
		AIColor color;
		color.Init();
		color.kind = kPattern;
		color.c.p.Init();
		color.c.p.pattern = pat;
		color.c.p.scale.h = color.c.p.scale.v = 100;
		AddSwatch(color, name);
	}
	json::Value v;
	v["name"] = PatternName(pat);
	return v;
}

json::Value PatternDelete(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	Check(sAIPattern->DeletePattern(PatternByName(name)), "DeletePattern");
	json::Value v;
	v["deleted"] = name;
	return v;
}

// ---- recolor

struct ColorVisit {
	std::map<std::string, json::Value> seen;   // unique colors, by their JSON
	std::string mode;                          // "", invert, grayscale, brightness
	double amount = 0;
};

void VisitColor(AIColor* color, void* data, AIErr* result, AIBoolean* altered)
{
	ColorVisit& v = *(ColorVisit*) data;
	*result = kNoErr;
	*altered = false;
	if (v.mode.empty()) {
		json::Value j = ColorJson(*color);
		v.seen[j.dump()] = j;
		return;
	}
	auto clamp = [](double x) { return (AIReal) std::max(0.0, std::min(1.0, x)); };
	if (color->kind == kThreeColor) {
		AIThreeColorStyle& c = color->c.rgb;
		if (v.mode == "invert") { c.red = 1 - c.red; c.green = 1 - c.green; c.blue = 1 - c.blue; }
		else if (v.mode == "grayscale") { AIReal y = clamp(0.299 * c.red + 0.587 * c.green + 0.114 * c.blue); c.red = c.green = c.blue = y; }
		else { c.red = clamp(c.red + v.amount); c.green = clamp(c.green + v.amount); c.blue = clamp(c.blue + v.amount); }
	}
	else if (color->kind == kFourColor) {
		AIFourColorStyle& c = color->c.f;
		if (v.mode == "invert") { c.cyan = 1 - c.cyan; c.magenta = 1 - c.magenta; c.yellow = 1 - c.yellow; }
		else if (v.mode == "grayscale") { AIReal k = clamp(c.black + (0.3 * c.cyan + 0.59 * c.magenta + 0.11 * c.yellow)); c.cyan = c.magenta = c.yellow = 0; c.black = k; }
		else c.black = clamp(c.black - v.amount);
	}
	else if (color->kind == kGrayColor) {
		AIReal& g = color->c.g.gray;   // ink amount
		if (v.mode == "invert") g = 1 - g;
		else if (v.mode == "brightness") g = clamp(g - v.amount);
	}
	else return;   // spot colors, gradients and patterns stay as they are
	*altered = true;
}

std::vector<AIArtHandle> TargetsOrDocument(const json::Value& p)
{
	if (p.has("id") || p.has("ids")) return ArtList(p);
	if (p.boolean("selection", false)) return ArtList(p, true);
	return {nullptr};   // the whole document
}

json::Value ColorUsed(const json::Value& p)
{
	Need(sAIPathStyle, "The path style suite");
	ActiveDocument();
	ColorVisit visit;
	for (AIArtHandle a : TargetsOrDocument(p)) sAIPathStyle->AdjustObjectAIColors(a, VisitColor, &visit, kVisitColorsNullFlags, nullptr);
	json::Value list = json::Value::MakeArray();
	for (auto& kv : visit.seen) list.push(kv.second);
	return list;
}

// Process colors match within a hair (0.5%), so a color read back from
// appearance.get / art.get - rounded on the way out - still finds itself.
struct ColorSwap {
	AIColor from, to;
};

bool Near(AIReal a, AIReal b) { return std::fabs(a - b) <= 0.005; }

bool SameColor(const AIColor& a, const AIColor& b)
{
	if (a.kind != b.kind) return false;
	if (a.kind == kFourColor) return Near(a.c.f.cyan, b.c.f.cyan) && Near(a.c.f.magenta, b.c.f.magenta) && Near(a.c.f.yellow, b.c.f.yellow) && Near(a.c.f.black, b.c.f.black);
	if (a.kind == kThreeColor) return Near(a.c.rgb.red, b.c.rgb.red) && Near(a.c.rgb.green, b.c.rgb.green) && Near(a.c.rgb.blue, b.c.rgb.blue);
	if (a.kind == kGrayColor) return Near(a.c.g.gray, b.c.g.gray);
	return false;
}

void SwapColor(AIColor* color, void* data, AIErr* result, AIBoolean* altered)
{
	ColorSwap& s = *(ColorSwap*) data;
	*result = kNoErr;
	*altered = SameColor(*color, s.from);
	if (*altered) *color = s.to;
}

json::Value ColorReplace(const json::Value& p)
{
	Need(sAIPathStyle, "The path style suite");
	ActiveDocument();
	AIColor from = ParseColor(Required(p, "from"), "from"), to = ParseColor(Required(p, "to"), "to");
	bool process = from.kind == kFourColor || from.kind == kThreeColor || from.kind == kGrayColor;
	ColorSwap swap{from, to};
	bool any = false;
	for (AIArtHandle a : TargetsOrDocument(p)) {
		AIBoolean made = false;
		if (process) Check(sAIPathStyle->AdjustObjectAIColors(a, SwapColor, &swap, kVisitColorsNullFlags, &made), "AdjustObjectAIColors");
		else Check(sAIPathStyle->ReplaceObjectAIColor(a, &from, &to, false, &made), "ReplaceObjectAIColor");
		any = any || made;
	}
	json::Value v;
	v["replaced"] = any;
	return v;
}

json::Value ColorAdjust(const json::Value& p)
{
	Need(sAIPathStyle, "The path style suite");
	ActiveDocument();
	ColorVisit visit;
	visit.mode = ReqStr(p, "mode");
	if (visit.mode != "invert" && visit.mode != "grayscale" && visit.mode != "brightness") Fail(kErrInvalidParams, "'mode' must be invert, grayscale or brightness");
	visit.amount = p.num("amount", 0) / 100.0;
	bool any = false;
	for (AIArtHandle a : TargetsOrDocument(p)) {
		AIBoolean made = false;
		Check(sAIPathStyle->AdjustObjectAIColors(a, VisitColor, &visit, kVisitColorsNullFlags, &made), "AdjustObjectAIColors");
		any = any || made;
	}
	json::Value v;
	v["changed"] = any;
	return v;
}

} // namespace

// ---- named paint for every command's fill / stroke (Kit.h)

bool NamedPaintFromJson(const json::Value& v, AIColor& c)
{
	c.Init();
	if (v.get("swatch").isString()) {
		Check(sAISwatchList->GetAIColor(SwatchByName(v.get("swatch").asString()), &c), "GetAIColor");
		return true;
	}
	if (v.get("spot").isString()) {
		c.kind = kCustomColor;
		c.c.c.color = SpotByName(v.get("spot").asString());
		c.c.c.tint = (AIReal) (1 - std::max(0.0, std::min(100.0, v.num("tint", 100))) / 100.0);   // Illustrator stores the ink left out
		return true;
	}
	if (v.get("gradient").isString()) {
		c.kind = kGradient;
		c.c.b.Init();
		c.c.b.gradient = GradientByName(v.get("gradient").asString());
		c.c.b.gradientAngle = (AIReal) v.num("angle", 0);
		c.c.b.matrix.Init();
		if (v.has("origin")) c.c.b.gradientOrigin = Point(v.get("origin"), "origin");
		if (v.has("length")) c.c.b.gradientLength = (AIReal) v.get("length").asNumber();
		if (v.has("matrix")) {   // as appearance.get reports it: copies a gradient exactly
			const json::Value& m = v.get("matrix");
			if (!m.isArray() || m.size() != 6) Fail(kErrInvalidParams, "'matrix' must be [a, b, c, d, tx, ty]");
			AIReal* f[] = {&c.c.b.matrix.a, &c.c.b.matrix.b, &c.c.b.matrix.c, &c.c.b.matrix.d, &c.c.b.matrix.tx, &c.c.b.matrix.ty};
			for (int i = 0; i < 6; i++) *f[i] = (AIReal) m.asArray()[i].asNumber();
		}
		return true;
	}
	if (v.get("pattern").isString()) {
		c.kind = kPattern;
		c.c.p.Init();
		c.c.p.pattern = PatternByName(v.get("pattern").asString());
		c.c.p.scale.h = c.c.p.scale.v = (AIReal) v.num("scale", 100);   // percent
		c.c.p.rotate = (AIReal) v.num("rotate", 0);
		c.c.p.transform.Init();
		return true;
	}
	return false;
}

json::Value NamedPaintJson(const AIColor& c)
{
	json::Value v;
	if (c.kind == kCustomColor && sAICustomColor) {
		v["spot"] = CustomColorName(c.c.c.color);
		v["tint"] = (1 - c.c.c.tint) * 100.0;
	}
	else if (c.kind == kGradient && sAIGradient) {
		v["gradient"] = GradientName(c.c.b.gradient);
		ai::int16 type = 0;
		sAIGradient->GetGradientType(c.c.b.gradient, &type);
		v["type"] = type == kRadialGradient ? "radial" : "linear";
		const AIGradientStyle& g = c.c.b;
		v["angle"] = (double) g.gradientAngle;
		v["origin"] = PointJson(g.gradientOrigin);
		v["length"] = (double) g.gradientLength;
		// Illustrator often keeps the real placement in the matrix (origin
		// [0,0], length 1 is common): pass it back to copy the gradient, and
		// 'from' / 'to' say where the ramp actually runs on the page.
		AIRealMatrix identity;
		identity.Init();
		if (!(g.matrix == identity)) {
			const AIRealMatrix& m = g.matrix;
			v["matrix"] = json::Array{(double) m.a, (double) m.b, (double) m.c, (double) m.d, (double) m.tx, (double) m.ty};
			double a = g.gradientAngle / kDegrees;
			double x0 = g.gradientOrigin.h, y0 = g.gradientOrigin.v;
			double x1 = x0 + std::cos(a) * g.gradientLength, y1 = y0 + std::sin(a) * g.gradientLength;
			auto at = [&](double x, double y) { return json::Array{m.a * x + m.c * y + m.tx, m.b * x + m.d * y + m.ty}; };
			v["from"] = at(x0, y0);
			v["to"] = at(x1, y1);
		}
	}
	else if (c.kind == kPattern && sAIPattern) {
		v["pattern"] = PatternName(c.c.p.pattern);
		v["scale"] = (double) c.c.p.scale.h;
		if (c.c.p.rotate != 0) v["rotate"] = (double) c.c.p.rotate;
	}
	else return json::Value(c.kind == kGradient ? "gradient" : c.kind == kPattern ? "pattern" : "spot");
	return v;
}

// A gradient without 'origin' / 'length' spans the art, like dropping a
// gradient swatch on it: linear across the bounds at 'angle', radial from the center.
void FitPaintToArt(AIArtHandle art, AIColor& c, const json::Value& spec)
{
	if (c.kind != kGradient || !spec.isObject() || (spec.has("origin") && spec.has("length")) || spec.has("matrix")) return;
	AIRealRect b;
	if (sAIArt->GetArtBounds(art, &b)) return;
	double w = b.right - b.left, h = b.top - b.bottom, cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
	ai::int16 type = 0;
	sAIGradient->GetGradientType(c.c.b.gradient, &type);
	if (type == kRadialGradient) {
		if (!spec.has("origin")) { c.c.b.gradientOrigin.h = (AIReal) cx; c.c.b.gradientOrigin.v = (AIReal) cy; }
		if (!spec.has("length")) c.c.b.gradientLength = (AIReal) (std::max(w, h) / 2);
		return;
	}
	double a = c.c.b.gradientAngle / kDegrees, dx = std::cos(a), dy = std::sin(a);
	double length = std::fabs(w * dx) + std::fabs(h * dy);
	if (!spec.has("length")) c.c.b.gradientLength = (AIReal) length;
	if (!spec.has("origin")) {
		c.c.b.gradientOrigin.h = (AIReal) (cx - dx * c.c.b.gradientLength / 2);
		c.c.b.gradientOrigin.v = (AIReal) (cy - dy * c.c.b.gradientLength / 2);
	}
}

// Stroke alignment lives with the selection in the SDK: select the art, set it, put the selection back.
void SetStrokeAlign(AIArtHandle art, const std::string& align)
{
	Need(sAIPaintStyle, "The paint style suite");
	ai::uint32 value = align == "center" ? kAIStrokeAlignmentCenter : align == "inside" ? kAIStrokeAlignmentInside
		: align == "outside" ? kAIStrokeAlignmentOutside : 99;
	if (value == 99) Fail(kErrInvalidParams, "'strokeAlign' must be center, inside or outside");
	std::vector<AIArtHandle> before;
	AIArtHandle** matches = nullptr;
	ai::int32 n = 0;
	if (!sAIMatchingArt->GetSelectedArt(&matches, &n) && matches) {
		for (ai::int32 i = 0; i < n; i++) before.push_back((*matches)[i]);
		sSPBlocks->FreeBlock(matches);
	}
	sAIMatchingArt->DeselectAll();
	sAIArt->SetArtUserAttr(art, kArtSelected, kArtSelected);
	AIErr e = sAIPaintStyle->SetStrokeAlignmentOnSelection(value);
	sAIMatchingArt->DeselectAll();
	for (AIArtHandle a : before) if (sAIArt->ValidArt(a, true)) sAIArt->SetArtUserAttr(a, kArtSelected, kArtSelected);
	Check(e, "SetStrokeAlignment");
}

void AddPaintCommands(CommandTable& t)
{
	t["swatch.list"] = {"The Swatches panel: groups and their swatches (name, color). Use a swatch anywhere a color goes: {\"swatch\": name}.",
		Params({{"group", "string - only this group (optional)"}}), SwatchList, false};
	t["swatch.create"] = {"New swatch from any paint (a color, spot, gradient or pattern).",
		Params({{"name", "string"}, {"color", kPaint}, {"group", "string - add to this swatch group (optional)"}}), SwatchCreate, true};
	t["swatch.set"] = {"Change a swatch's color and/or rename it (art using a global swatch updates).",
		Params({{"name", "string"}, {"color", kPaint}, {"rename", "string"}}), SwatchSet, true};
	t["swatch.delete"] = {"Delete a swatch.", Params({{"name", "string"}, {"deleteColor", "boolean - also delete its spot / global color"}}), SwatchDelete, true};
	t["swatch.group.create"] = {"New swatch group, optionally with colors.",
		Params({{"name", "string"}, {"colors", "array - paints or {name, color}"}}), SwatchGroupCreate, true};
	t["spot.list"] = {"Spot and global colors: name, spot or global, base color.", Params({}), SpotList, false};
	t["spot.create"] = {"New spot color (spot=true, default) or global process color (spot=false), with a swatch. Use it: {\"spot\": name, \"tint\": 0-100}.",
		Params({{"name", "string"}, {"color", "rgb / cmyk paint"}, {"spot", "boolean (default true)"}, {"swatch", "boolean (default true)"}}), SpotCreate, true};
	t["spot.delete"] = {"Delete a spot / global color.", Params({{"name", "string"}}), SpotDelete, true};
	t["gradient.list"] = {"Gradients: name, linear / radial, stops (position 0-100, color, midpoint, opacity).", Params({}), GradientList, false};
	t["gradient.create"] = {"New gradient (and swatch). Apply it: fill {\"gradient\": name, \"angle\": degrees} - it spans the art unless origin / length are given.",
		Params({{"name", "string"}, {"type", "linear (default) | radial"}, {"stops", "array - [{color, position?, midpoint?, opacity?}] or plain colors"},
			{"swatch", "boolean (default true)"}}), GradientCreate, true};
	t["gradient.set"] = {"Change a gradient's type, stops or name; art using it updates.",
		Params({{"name", "string"}, {"type", "linear | radial"}, {"stops", "array"}, {"rename", "string"}}), GradientSet, true};
	t["gradient.delete"] = {"Delete a gradient.", Params({{"name", "string"}}), GradientDelete, true};
	t["pattern.list"] = {"Fill patterns: name, tile bounds.", Params({}), PatternList, false};
	t["pattern.create"] = {"New pattern (and swatch) from art (default: the selection); 'tile' [w, h] sets the repeat size. Apply it: fill {\"pattern\": name, \"scale\": percent}.",
		Params({{"name", "string"}, {"ids", kIds}, {"id", "string"}, {"tile", "[w, h] - optional"}, {"swatch", "boolean (default true)"}}), PatternCreate, true};
	t["pattern.delete"] = {"Delete a pattern.", Params({{"name", "string"}}), PatternDelete, true};
	const char* scope = "ids / id, or selection=true; default: the whole document";
	t["color.used"] = {"Every color art uses (default: the whole document), each once.", Params({{"ids", "string[]"}, {"id", "string"}, {"selection", scope}}), ColorUsed, false};
	t["color.replace"] = {"Replace one color with another everywhere it's used (default: the whole document), including in gradients and patterns.",
		Params({{"from", kPaint}, {"to", kPaint}, {"ids", "string[]"}, {"id", "string"}, {"selection", scope}}), ColorReplace, true};
	t["color.adjust"] = {"Recolor: invert, grayscale, or brightness by 'amount' (-100..100); spot colors, gradients' swatches and patterns are left alone.",
		Params({{"mode", "invert | grayscale | brightness"}, {"amount", "number - brightness, percent"}, {"ids", "string[]"}, {"id", "string"}, {"selection", scope}}), ColorAdjust, true};
}

} // namespace slippy
