#include "IllustratorSDK.h"
#include "CrashLog.h"
#include "Commands.h"
#include "Kit.h"
#include "Narrate.h"
#include "Overlay.h"
#include "SlippySuites.h"
#include "SlippyID.h"
#include "IAIFilePath.hpp"
#include "IText.h"
#include "Platform.h"
#include "Raster.h"

#ifdef SendMessage
#undef SendMessage   // windows.h's; this file means SPInterfaceSuite::SendMessage
#endif

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace slippy {

// Shared helpers (Kit.h) first; this file's own commands follow in an
// anonymous namespace.

// ------------------------------------------------------------------ errors

[[noreturn]] void Fail(int code, const std::string& message) { throw CommandError{code, message}; }

std::string ErrText(AIErr e)
{
	// Most AIErr values are four-character codes ('PARM', '!sel').
	char c[5] = {(char) ((e >> 24) & 0xFF), (char) ((e >> 16) & 0xFF), (char) ((e >> 8) & 0xFF), (char) (e & 0xFF), 0};
	for (int i = 0; i < 4; i++) if (c[i] < 32 || c[i] > 126) return std::to_string((long) e);
	return std::string("'") + c + "'";
}

void Check(AIErr e, const char* what)
{
	if (e) throw CommandError{kErrIllustrator, std::string(what) + " failed (" + ErrText(e) + ")", e};
}

// ------------------------------------------------------------------ strings

ai::UnicodeString U(const std::string& s) { return ai::UnicodeString::FromUTF8(s); }
std::string S(const ai::UnicodeString& u) { return u.as_UTF8(); }

std::string Lower(std::string s)
{
	for (char& c : s) c = (char) tolower((unsigned char) c);
	return s;
}

// ------------------------------------------------------------------ params

const json::Value& Required(const json::Value& p, const char* key)
{
	const json::Value& v = p.get(key);
	if (v.isNull()) Fail(kErrInvalidParams, std::string("missing '") + key + "'");
	return v;
}

std::string ReqStr(const json::Value& p, const char* key)
{
	const json::Value& v = Required(p, key);
	if (!v.isString()) Fail(kErrInvalidParams, std::string("'") + key + "' must be a string");
	return v.asString();
}

double ReqNum(const json::Value& p, const char* key)
{
	const json::Value& v = Required(p, key);
	if (!v.isNumber()) Fail(kErrInvalidParams, std::string("'") + key + "' must be a number");
	return v.asNumber();
}

AIRealPoint Point(const json::Value& v, const char* what)
{
	if (!v.isArray() || v.size() != 2 || !v.asArray()[0].isNumber() || !v.asArray()[1].isNumber())
		Fail(kErrInvalidParams, std::string("'") + what + "' must be [x, y]");
	AIRealPoint p;
	p.h = (AIReal) v.asArray()[0].asNumber();
	p.v = (AIReal) v.asArray()[1].asNumber();
	return p;
}

json::Value PointJson(const AIRealPoint& p) { return json::Array{(double) p.h, (double) p.v}; }

json::Value RectJson(const AIRealRect& r)
{
	json::Value v;
	v["left"] = (double) r.left;
	v["top"] = (double) r.top;
	v["right"] = (double) r.right;
	v["bottom"] = (double) r.bottom;
	v["width"] = (double) (r.right - r.left);
	v["height"] = (double) std::fabs(r.top - r.bottom);
	return v;
}

// ------------------------------------------------------------------ document

AIDocumentHandle ActiveDocument()
{
	AIDocumentHandle doc = nullptr;
	ai::int32 count = 0;
	sAIDocumentList->Count(&count);
	if (count == 0 || sAIDocument->GetDocument(&doc) || !doc) Fail(kErrUnavailable, "no document is open");
	return doc;
}

std::vector<std::pair<AIDocumentHandle, int>> OpenDocuments()
{
	std::vector<std::pair<AIDocumentHandle, int>> docs;
	ai::int32 count = 0;
	sAIDocumentList->Count(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AIDocumentHandle doc = nullptr;
		if (sAIDocumentList->GetNthDocument(&doc, i) || !doc) continue;
		auto seen = std::find_if(docs.begin(), docs.end(), [&](const std::pair<AIDocumentHandle, int>& d) { return d.first == doc; });
		if (seen != docs.end()) seen->second++;
		else docs.push_back({doc, 1});
	}
	return docs;
}

std::string DocName(AIDocumentHandle doc)
{
	ai::UnicodeString name;
	if (!sAIDocument->GetDocumentFileNameFromHandle(doc, name)) return S(name);
	return "";
}

std::string DocPath(AIDocumentHandle doc)
{
	ai::FilePath path;
	if (!sAIDocument->GetDocumentFileSpecificationFromHandle(doc, path)) return S(path.GetFullPath());
	return "";
}

// ------------------------------------------------------------------ art ids
// Art ids are Illustrator's own UUIDs: stable for the life of the object,
// scoped to its document, and safe to pass back (a stale id doesn't resolve,
// it never touches freed memory).

std::string ArtId(AIArtHandle art)
{
	if (!sAIUUID) return "";
	ai::uuid id;
	ai::UnicodeString s;
	if (sAIUUID->GetArtUUID(art, id) || sAIUUID->UUIDToString(id, s)) return "";
	return S(s);
}

AIArtHandle ArtById(const std::string& idText)
{
	Need(sAIUUID, "The UUID suite");
	ActiveDocument();
	ai::uuid id;
	AIArtHandle art = nullptr;
	if (sAIUUID->StringToUUID(U(idText), id) || sAIUUID->GetArtHandle(id, art) || !art || !sAIArt->ValidArt(art, true))
		Fail(kErrNotFound, "no art with id " + idText + " in the active document");
	overlay::Touch(art);
	return art;
}

// The selected objects an edit should act on: fully selected, topmost (a
// selected group stands for its contents), never a layer's own group -
// Illustrator reports those as partially selected, and moving one moves the
// whole layer.
std::vector<AIArtHandle> SelectedArt()
{
	std::vector<AIArtHandle> all, out;
	Need(sAIMatchingArt, "The matching art suite");
	AIArtHandle** matches = nullptr;
	ai::int32 n = 0;
	if (!sAIMatchingArt->GetSelectedArt(&matches, &n) && matches) {
		for (ai::int32 i = 0; i < n; i++) all.push_back((*matches)[i]);
		sSPBlocks->FreeBlock(matches);
	}
	std::set<AIArtHandle> chosen;
	for (AIArtHandle a : all) {
		ai::int32 attr = 0;
		sAIArt->GetArtUserAttr(a, kArtFullySelected, &attr);
		AIBoolean layerGroup = false;
		sAIArt->IsArtLayerGroup(a, &layerGroup);
		if ((attr & kArtFullySelected) && !layerGroup) chosen.insert(a);
	}
	for (AIArtHandle a : all) {
		if (!chosen.count(a)) continue;
		bool nested = false;
		AIArtHandle up = nullptr;
		for (sAIArt->GetArtParent(a, &up); up && !nested; sAIArt->GetArtParent(up, &up)) nested = chosen.count(up) > 0;
		if (!nested) out.push_back(a);
	}
	return out;
}

// Selects exactly these, as the SDK's "from selection" calls want.
// Selecting a group or compound path selects everything in it, as clicking
// it with the Selection tool does. Illustrator would settle the contents only
// after the event ends, so a menu command run right after (Copy...) saw the
// container half-selected and copied nothing that would paste.
void SelectDeep(AIArtHandle art)
{
	Check(sAIArt->SetArtUserAttr(art, kArtSelected, kArtSelected), "select");
	AIArtHandle child = nullptr;
	sAIArt->GetArtFirstChild(art, &child);
	for (; child; sAIArt->GetArtSibling(child, &child)) SelectDeep(child);
}

void SelectOnly(const std::vector<AIArtHandle>& arts)
{
	Need(sAIMatchingArt, "The matching art suite")->DeselectAll();
	for (AIArtHandle a : arts) SelectDeep(a);
}

// Ids are strings ("476"); agents often send them as numbers, so take both.
bool IsId(const json::Value& v) { return v.isString() || (v.isNumber() && v.asNumber() == std::floor(v.asNumber())); }

std::string IdText(const json::Value& v)
{
	if (v.isString()) return v.asString();
	if (IsId(v)) return std::to_string((long long) v.asNumber());
	Fail(kErrInvalidParams, "art ids are strings like \"476\"");
}

std::vector<AIArtHandle> ArtList(const json::Value& p, bool selectionIfMissing)
{
	std::vector<AIArtHandle> out;
	const json::Value& ids = p.get("ids");
	const json::Value& id = p.get("id");
	if (IsId(id)) out.push_back(ArtById(IdText(id)));
	else if (ids.isArray()) {
		for (const json::Value& v : ids.asArray()) out.push_back(ArtById(IdText(v)));
	}
	else if (selectionIfMissing) {
		out = SelectedArt();
		for (AIArtHandle a : out) overlay::Touch(a);
		if (out.empty()) Fail(kErrInvalidParams, "pass 'id' or 'ids', or select something first");
	}
	else Fail(kErrInvalidParams, "pass 'id' or 'ids'");
	return out;
}

const char* TypeName(short type)
{
	switch (type) {
	case kGroupArt: return "group";
	case kPathArt: return "path";
	case kCompoundPathArt: return "compoundPath";
	case kPlacedArt: return "placed";
	case kMysteryPathArt: return "mysteryPath";
	case kRasterArt: return "raster";
	case kPluginArt: return "plugin";
	case kMeshArt: return "mesh";
	case kTextFrameArt: return "text";
	case kSymbolArt: return "symbol";
	case kForeignArt: return "foreign";
	case kLegacyTextArt: return "legacyText";
	case kChartArt: return "chart";
	default: return "unknown";
	}
}

short ArtType(AIArtHandle art)
{
	short type = kUnknownArt;
	sAIArt->GetArtType(art, &type);
	return type;
}

bool Attr(AIArtHandle art, ai::int32 which)
{
	ai::int32 attr = 0;
	sAIArt->GetArtUserAttr(art, which, &attr);
	return (attr & which) != 0;
}

std::string LayerTitle(AILayerHandle layer)
{
	ai::UnicodeString t;
	sAILayer->GetLayerTitle(layer, t);
	return S(t);
}

json::Value ArtSummary(AIArtHandle art, int depth)
{
	json::Value v;
	short type = ArtType(art);
	v["id"] = ArtId(art);
	v["type"] = TypeName(type);
	ai::UnicodeString name;
	ASBoolean isDefault = true;
	if (!sAIArt->GetArtName(art, name, &isDefault) && !isDefault) v["name"] = S(name);
	AIRealRect bounds;
	if (!sAIArt->GetArtBounds(art, &bounds)) v["bounds"] = RectJson(bounds);
	if (Attr(art, kArtSelected)) v["selected"] = true;
	if (Attr(art, kArtHidden)) v["hidden"] = true;
	if (Attr(art, kArtLocked)) v["locked"] = true;
	if (Attr(art, kArtIsClipMask)) v["clipMask"] = true;
	AIBoolean clipped = false;
	if (type == kGroupArt && sAIGroup && !sAIGroup->GetGroupClipped(art, &clipped) && clipped) v["clipped"] = true;
	if (type == kPlacedArt && sAIPlaced) {
		ai::FilePath file;
		if (!sAIPlaced->GetPlacedFileSpecification(art, file)) v["file"] = S(file.GetFullPath());
	}
	if (type == kGroupArt || type == kCompoundPathArt) {
		AIArtHandle child = nullptr;
		sAIArt->GetArtFirstChild(art, &child);
		int count = 0;
		json::Value children = json::Value::MakeArray();
		for (; child; sAIArt->GetArtSibling(child, &child)) {
			count++;
			if (depth > 0) children.push(ArtSummary(child, depth - 1));
		}
		v["childCount"] = count;
		if (depth > 0) v["children"] = children;
	}
	return v;
}

// ------------------------------------------------------------------ layers

AILayerHandle LayerByParam(const json::Value& p)
{
	const json::Value& layer = p.get("layer");
	if (layer.isNull()) return nullptr;
	AILayerHandle h = nullptr;
	if (layer.isNumber()) {
		if (sAILayer->GetNthLayer(layer.asInt(), &h) || !h) Fail(kErrNotFound, "no layer at index " + std::to_string(layer.asInt()));
	}
	else if (layer.isString()) {
		if (sAILayer->GetLayerByTitle(&h, U(layer.asString())) || !h) Fail(kErrNotFound, "no layer named '" + layer.asString() + "'");
	}
	else Fail(kErrInvalidParams, "'layer' must be a layer name or index");
	return h;
}

// Where new art goes: inside 'parent' (a group id), on top of 'layer', or
// above everything in the current layer.
void Placement(const json::Value& p, ai::int16& order, AIArtHandle& prep)
{
	ActiveDocument();   // new art needs a document: say so plainly
	order = kPlaceAboveAll;
	prep = nullptr;
	if (IsId(p.get("parent"))) {
		prep = ArtById(IdText(p.get("parent")));
		if (ArtType(prep) != kGroupArt) Fail(kErrInvalidParams, "'parent' must be a group");
		order = kPlaceInsideOnTop;
	}
	else if (AILayerHandle layer = LayerByParam(p)) {
		Check(sAIArt->GetFirstArtOfLayer(layer, &prep), "GetFirstArtOfLayer");
		order = kPlaceInsideOnTop;
	}
}

// ------------------------------------------------------------------ color

json::Value ColorJson(const AIColor& c)
{
	char hex[8];
	auto byte = [](AIReal x) { return (int) std::lround(std::max(0.0, std::min(1.0, (double) x)) * 255); };
	switch (c.kind) {
	case kThreeColor: {
		snprintf(hex, sizeof hex, "#%02X%02X%02X", byte(c.c.rgb.red), byte(c.c.rgb.green), byte(c.c.rgb.blue));
		return json::Value(hex);
	}
	case kFourColor: {
		json::Value v;
		v["cmyk"] = json::Array{c.c.f.cyan * 100.0, c.c.f.magenta * 100.0, c.c.f.yellow * 100.0, c.c.f.black * 100.0};
		return v;
	}
	case kGrayColor: {
		json::Value v;
		v["gray"] = c.c.g.gray * 100.0;
		return v;
	}
	case kNoneColor: return json::Value("none");
	case kPattern: case kGradient: case kCustomColor: return NamedPaintJson(c);   // CmdPaint.cpp
	default: return json::Value("other");
	}
}

// "#RRGGBB", "none", {"rgb":[0-255 x3]}, {"cmyk":[0-100 x4]}, {"gray":0-100},
// or by name: {"swatch"}, {"spot", "tint"}, {"gradient", ...}, {"pattern", ...} (CmdPaint.cpp).
AIColor ParseColor(const json::Value& v, const char* what)
{
	AIColor c;
	c.Init();
	auto bad = [&]() { Fail(kErrInvalidParams, std::string("'") + what + "' must be " + kPaint); };
	if (v.isString()) {
		const std::string& s = v.asString();
		if (s == "none") { c.kind = kNoneColor; return c; }
		if (s.size() != 7 || s[0] != '#') bad();
		unsigned rgb = 0;
		if (sscanf(s.c_str() + 1, "%6x", &rgb) != 1) bad();
		c.kind = kThreeColor;
		c.c.rgb.red = ((rgb >> 16) & 0xFF) / 255.0;
		c.c.rgb.green = ((rgb >> 8) & 0xFF) / 255.0;
		c.c.rgb.blue = (rgb & 0xFF) / 255.0;
		return c;
	}
	if (!v.isObject()) bad();
	if (NamedPaintFromJson(v, c)) return c;
	auto channels = [&](const json::Value& a, size_t n, double scale, AIReal* out[]) {
		if (!a.isArray() || a.size() != n) bad();
		for (size_t i = 0; i < n; i++) {
			if (!a.asArray()[i].isNumber()) bad();
			*out[i] = (AIReal) std::max(0.0, std::min(1.0, a.asArray()[i].asNumber() / scale));
		}
	};
	if (v.has("rgb")) {
		c.kind = kThreeColor;
		AIReal* out[] = {&c.c.rgb.red, &c.c.rgb.green, &c.c.rgb.blue};
		channels(v.get("rgb"), 3, 255.0, out);
	}
	else if (v.has("cmyk")) {
		c.kind = kFourColor;
		AIReal* out[] = {&c.c.f.cyan, &c.c.f.magenta, &c.c.f.yellow, &c.c.f.black};
		channels(v.get("cmyk"), 4, 100.0, out);
	}
	else if (v.has("gray")) {
		if (!v.get("gray").isNumber()) bad();
		c.kind = kGrayColor;
		c.c.g.gray = (AIReal) std::max(0.0, std::min(1.0, v.get("gray").asNumber() / 100.0));
	}
	else bad();
	return c;
}

const char* CapName(AILineCap c) { return c == kAIRoundCap ? "round" : c == kAIProjectingCap ? "projecting" : "butt"; }
const char* JoinName(AILineJoin j) { return j == kAIRoundJoin ? "round" : j == kAIBevelJoin ? "bevel" : "miter"; }

json::Value StyleJson(AIArtHandle art)
{
	json::Value v;
	if (!sAIPathStyle) return v;
	AIPathStyle style;
	AIBoolean advanced = false;
	if (sAIPathStyle->GetPathStyle(art, &style, &advanced)) return v;
	v["fill"] = style.fillPaint ? ColorJson(style.fill.color) : json::Value("none");
	v["stroke"] = style.strokePaint ? ColorJson(style.stroke.color) : json::Value("none");
	if (style.strokePaint) {
		v["strokeWidth"] = (double) style.stroke.width;
		v["cap"] = CapName(style.stroke.cap);
		v["join"] = JoinName(style.stroke.join);
		if (style.stroke.join == kAIMiterJoin) v["miterLimit"] = (double) style.stroke.miterLimit;
		if (style.stroke.dash.length > 0) {
			json::Value dash = json::Value::MakeArray();
			for (int i = 0; i < style.stroke.dash.length && i < kMaxDashComponents; i++) dash.push((double) style.stroke.dash.array[i]);
			v["dash"] = dash;
			if (style.stroke.dash.offset != 0) v["dashOffset"] = (double) style.stroke.dash.offset;
		}
		if (style.stroke.overprint) v["strokeOverprint"] = true;
	}
	if (style.fillPaint && style.fill.overprint) v["fillOverprint"] = true;
	if (style.evenodd) v["evenOdd"] = true;
	if (advanced) v["advancedFill"] = true;
	return v;
}

namespace {
const char* const kPaintKeys[] = {"fill", "stroke", "strokeWidth", "dash", "dashOffset", "cap", "join", "miterLimit",
	"fillOverprint", "strokeOverprint", "evenOdd", "strokeAlign"};

bool HasPaint(const json::Value& p)
{
	for (const char* k : kPaintKeys) if (p.has(k)) return true;
	return false;
}

void ApplyStroke(AIStrokeStyle& stroke, const json::Value& p)
{
	if (p.has("strokeWidth")) stroke.width = (AIReal) ReqNum(p, "strokeWidth");
	if (p.has("cap")) {
		std::string c = ReqStr(p, "cap");
		stroke.cap = c == "round" ? kAIRoundCap : c == "projecting" ? kAIProjectingCap : c == "butt" ? kAIButtCap : (Fail(kErrInvalidParams, "'cap' must be butt, round or projecting"), kAIButtCap);
	}
	if (p.has("join")) {
		std::string j = ReqStr(p, "join");
		stroke.join = j == "round" ? kAIRoundJoin : j == "bevel" ? kAIBevelJoin : j == "miter" ? kAIMiterJoin : (Fail(kErrInvalidParams, "'join' must be miter, round or bevel"), kAIMiterJoin);
	}
	if (p.has("miterLimit")) stroke.miterLimit = (AIReal) std::max(1.0, ReqNum(p, "miterLimit"));
	if (p.has("dash")) {
		const json::Value& d = p.get("dash");
		if (!d.isArray() || d.size() > kMaxDashComponents) Fail(kErrInvalidParams, "'dash' must be an array of up to 6 lengths ([] for solid)");
		stroke.dash.length = (ai::int16) d.size();
		for (size_t i = 0; i < d.size(); i++) stroke.dash.array[i] = (AIFloat) d.asArray()[i].asNumber();
	}
	if (p.has("dashOffset")) stroke.dash.offset = (AIFloat) ReqNum(p, "dashOffset");
	if (p.has("strokeOverprint")) stroke.overprint = p.boolean("strokeOverprint", false);
}
} // namespace

// Paint - whichever of fill, stroke, strokeWidth, dash, dashOffset, cap, join,
// miterLimit, fillOverprint, strokeOverprint, evenOdd, strokeAlign are present.
void ApplyStyle(AIArtHandle art, const json::Value& p)
{
	if (!HasPaint(p)) return;
	Need(sAIPathStyle, "The path style suite");
	short type = ArtType(art);
	if (type == kGroupArt || type == kCompoundPathArt) {
		// Style the paths inside, like the Swatches panel does.
		AIArtHandle child = nullptr;
		sAIArt->GetArtFirstChild(art, &child);
		for (; child; sAIArt->GetArtSibling(child, &child)) ApplyStyle(child, p);
		return;
	}
	if (type != kPathArt) return;
	AIPathStyle style;
	AIBoolean advanced = false;
	Check(sAIPathStyle->GetPathStyle(art, &style, &advanced), "GetPathStyle");
	if (p.has("fill")) {
		AIColor c = ParseColor(p.get("fill"), "fill");
		FitPaintToArt(art, c, p.get("fill"));
		style.fillPaint = c.kind != kNoneColor;
		if (style.fillPaint) style.fill.color = c;
	}
	if (p.has("stroke")) {
		AIColor c = ParseColor(p.get("stroke"), "stroke");
		FitPaintToArt(art, c, p.get("stroke"));
		style.strokePaint = c.kind != kNoneColor;
		if (style.strokePaint) {
			style.stroke.color = c;
			if (style.stroke.width <= 0) style.stroke.width = 1;
		}
	}
	ApplyStroke(style.stroke, p);
	if (p.has("fillOverprint")) style.fill.overprint = p.boolean("fillOverprint", false);
	if (p.has("evenOdd")) style.evenodd = p.boolean("evenOdd", false);
	Check(sAIPathStyle->SetPathStyle(art, &style), "SetPathStyle");
	if (p.has("strokeAlign")) SetStrokeAlign(art, ReqStr(p, "strokeAlign"));   // CmdPaint.cpp
}

// Name and style for freshly created art, and its id back.
json::Value Finish(AIArtHandle art, const json::Value& p)
{
	if (p.get("name").isString()) sAIArt->SetArtName(art, U(p.get("name").asString()));
	ApplyStyle(art, p);
	if (p.boolean("select", false)) sAIArt->SetArtUserAttr(art, kArtSelected, kArtSelected);
	overlay::Touch(art);
	return ArtSummary(art, 0);
}

// ------------------------------------------------------------------ paths

AIArtHandle NewPath(const json::Value& p, const std::vector<AIPathSegment>& segs, bool closed)
{
	Need(sAIPath, "The path suite");
	if (segs.empty() || segs.size() > 32000) Fail(kErrInvalidParams, "a path needs 1 to 32000 points");
	ai::int16 order;
	AIArtHandle prep;
	Placement(p, order, prep);
	AIArtHandle art = nullptr;
	Check(sAIArt->NewArt(kPathArt, order, prep, &art), "NewArt");
	Check(sAIPath->SetPathSegmentCount(art, (ai::int16) segs.size()), "SetPathSegmentCount");
	Check(sAIPath->SetPathSegments(art, 0, (ai::int16) segs.size(), segs.data()), "SetPathSegments");
	Check(sAIPath->SetPathClosed(art, closed), "SetPathClosed");
	return art;
}

AIPathSegment Corner(double x, double y)
{
	AIPathSegment s;
	s.p.h = s.in.h = s.out.h = (AIReal) x;
	s.p.v = s.in.v = s.out.v = (AIReal) y;
	s.corner = true;
	return s;
}

// [x, y] (a corner), or {"p", "in", "out", "smooth"} as PathJson writes it.
AIPathSegment SegmentFrom(const json::Value& v)
{
	if (v.isArray()) { AIRealPoint pt = Point(v, "point"); return Corner(pt.h, pt.v); }
	if (!v.isObject()) Fail(kErrInvalidParams, "each point is [x, y] or {\"p\":[x,y], \"in\":[x,y], \"out\":[x,y]}");
	AIRealPoint pt = Point(v.get("p"), "p");
	AIPathSegment s = Corner(pt.h, pt.v);
	if (v.has("in")) s.in = Point(v.get("in"), "in");
	if (v.has("out")) s.out = Point(v.get("out"), "out");
	s.corner = !v.boolean("smooth", false);
	return s;
}

json::Value PathJson(AIArtHandle art)
{
	json::Value v;
	if (!sAIPath) return v;
	ai::int16 count = 0;
	AIBoolean closed = false;
	sAIPath->GetPathSegmentCount(art, &count);
	sAIPath->GetPathClosed(art, &closed);
	std::vector<AIPathSegment> segs((size_t) count);
	if (count) sAIPath->GetPathSegments(art, 0, count, segs.data());
	json::Value list = json::Value::MakeArray();
	for (ai::int16 i = 0; i < count; i++) {
		const AIPathSegment& s = segs[(size_t) i];
		json::Value seg;
		seg["p"] = PointJson(s.p);
		if (s.in.h != s.p.h || s.in.v != s.p.v) seg["in"] = PointJson(s.in);
		if (s.out.h != s.p.h || s.out.v != s.p.v) seg["out"] = PointJson(s.out);
		if (!s.corner) seg["smooth"] = true;
		ai::int16 selected = kSegmentNotSelected;
		if (!sAIPath->GetPathSegmentSelected(art, i, &selected) && selected == kSegmentPointSelected) seg["selected"] = true;
		list.push(seg);
	}
	v["closed"] = (bool) closed;
	v["segments"] = list;
	return v;
}

// ------------------------------------------------------------------ text

std::string TextOf(AIArtHandle art)
{
	TextRangeRef ref = nullptr;
	if (!sAITextFrame || sAITextFrame->GetATETextRange(art, &ref) || !ref) return "";
	ATE::ITextRange range(ref);
	ATETextDOM::Int32 size = range.GetSize();
	std::vector<ASUnicode> buf((size_t) size + 1, 0);
	range.GetContents((ATETextDOM::Unicode*) buf.data(), size + 1);
	std::string text = S(ai::UnicodeString(buf.data(), (ai::UnicodeString::size_type) size));
	for (char& c : text) if (c == '\r') c = '\n';   // Illustrator's paragraph breaks
	return text;
}

void SetText(AIArtHandle art, const std::string& text, const json::Value& p)
{
	TextRangeRef ref = nullptr;
	Check(sAITextFrame->GetATETextRange(art, &ref), "GetATETextRange");
	ATE::ITextRange range(ref);
	range.Remove();
	// Illustrator uses \r for paragraph breaks.
	std::string t = text;
	for (char& c : t) if (c == '\n') c = '\r';
	std::basic_string<ASUnicode> u = U(t).as_ASUnicode();
	range.InsertAfter((const ATETextDOM::Unicode*) u.c_str(), (ATETextDOM::Int32) u.size());
	if (p.get("size").isNumber()) {
		TextRangeRef all = nullptr;
		Check(sAITextFrame->GetATETextRange(art, &all), "GetATETextRange");
		ATE::ITextRange whole(all);
		ATE::ICharFeatures features;
		features.SetFontSize((ATETextDOM::Real) p.get("size").asNumber());
		whole.SetLocalCharFeatures(features);
	}
}

json::Value Params(std::initializer_list<std::pair<const char*, const char*>> list)
{
	json::Value v = json::Value::MakeObject();
	for (auto& kv : list) v[kv.first] = kv.second;
	return v;
}

// ------------------------------------------------------------------ commands

namespace {

std::map<std::string, Command>& Table();

json::Value AppInfo(const json::Value&)
{
	json::Value v;
	v["plugin"] = "Slippy";
	v["version"] = kSlippyVersion;
	if (sAIRuntime) {
		ai::UnicodeString name;
		if (!sAIRuntime->GetAppNameUS(name)) v["app"] = S(name);
		v["appVersion"] = std::to_string(sAIRuntime->GetAppMajorVersion()) + "." + std::to_string(sAIRuntime->GetAppMinorVersion()) +
			"." + std::to_string(sAIRuntime->GetAppRevisionVersion());
	}
	ai::int32 count = (ai::int32) OpenDocuments().size();
	v["documentCount"] = count;
	json::Value missing = json::Value::MakeArray();
	if (!sAIPath) missing.push("path");
	if (!sAIPathStyle) missing.push("pathStyle");
	if (!sAITextFrame) missing.push("textFrame");
	if (!sAIUUID) missing.push("uuid");
	if (!sAIActionManager) missing.push("actionManager");
	if (!sAICommandManager) missing.push("commandManager");
	if (!sAITransformArt) missing.push("transformArt");
	if (!sAIUndo) missing.push("undo");
	if (!sAIFileFormat) missing.push("fileFormat");
	for (const std::string& s : SlippyMissingSuites()) missing.push(s);
	v["missingSuites"] = missing;
	json::Value newer = json::Value::MakeArray();
	for (const std::string& s : SlippyNewerSuites()) newer.push(s);
	if (newer.size()) v["newerSuites"] = newer;
	v["timerSuiteVersion"] = SlippyTimerVersion();
	if (sAIUndo && count) {
		ai::int32 past = 0, future = 0;
		if (!sAIUndo->CountTransactions(&past, &future)) { v["undoSteps"] = past; v["redoSteps"] = future; }
	}
	return v;
}

json::Value DocumentList(const json::Value&)
{
	auto docs = OpenDocuments();
	AIDocumentHandle active = nullptr;
	if (!docs.empty()) sAIDocument->GetDocument(&active);
	json::Value list = json::Value::MakeArray();
	for (size_t i = 0; i < docs.size(); i++) {
		json::Value d;
		d["index"] = (int) i;
		d["name"] = DocName(docs[i].first);
		d["path"] = DocPath(docs[i].first);
		d["active"] = docs[i].first == active;
		if (docs[i].second > 1) d["windows"] = docs[i].second;
		list.push(d);
	}
	return list;
}

json::Value ArtboardsJson()
{
	json::Value list = json::Value::MakeArray();
	if (!sAIArtboard) return list;
	ai::ArtboardList abl;
	if (sAIArtboard->GetArtboardList(abl)) return list;
	ai::ArtboardID count = 0, active = 0;
	sAIArtboard->GetCount(abl, count);
	sAIArtboard->GetActive(abl, active);
	for (ai::ArtboardID i = 0; i < count; i++) {
		ai::ArtboardProperties props;
		if (sAIArtboard->Init(props)) continue;
		if (!sAIArtboard->GetArtboardProperties(abl, i, props)) {
			json::Value a;
			a["index"] = (int) i;
			ai::UnicodeString name;
			sAIArtboard->GetName(props, name);
			a["name"] = S(name);
			AIRealRect r;
			if (!sAIArtboard->GetPosition(props, r)) a["bounds"] = RectJson(r);
			a["active"] = i == active;
			list.push(a);
		}
		sAIArtboard->Dispose(props);
	}
	sAIArtboard->ReleaseArtboardList(abl);
	return list;
}

json::Value DocumentInfo(const json::Value&)
{
	AIDocumentHandle doc = ActiveDocument();
	json::Value v;
	v["name"] = DocName(doc);
	v["path"] = DocPath(doc);
	AIBoolean modified = false;
	sAIDocument->GetDocumentModified(&modified);
	v["modified"] = (bool) modified;
	ai::int16 model = 0;
	if (!sAIDocument->GetDocumentColorModel(&model))
		v["colorModel"] = model == kDocRGBColor ? "rgb" : model == kDocCMYKColor ? "cmyk" : model == kDocGrayColor ? "gray" : "other";
	v["units"] = "points";
	v["coordinates"] = "Illustrator artwork coordinates: points, y grows upward. Art bounds, artboard bounds and every position parameter use the same space.";
	v["artboards"] = ArtboardsJson();
	ai::int32 layers = 0;
	sAILayer->CountLayers(&layers);
	v["layerCount"] = layers;
	return v;
}

json::Value DocumentNew(const json::Value& p)
{
	// Start from a real preset so every field is set (a half-filled struct
	// gives garbage artboards).
	AINewDocumentPreset settings;
	ai::UnicodeString preset;
	bool filled = false;
	std::vector<std::string> names = p.has("preset") ? std::vector<std::string>{ReqStr(p, "preset")}
		: std::vector<std::string>{"Print", "Art & Illustration", "Web", "Mobile", "Film & Video"};
	for (const std::string& name : names) {
		preset = U(name);
		if (!sAIDocumentList->GetPresetSettings(preset, &settings)) { filled = true; break; }
	}
	if (!filled) Fail(kErrInvalidParams, p.has("preset") ? "no new-document preset named '" + p.str("preset") + "'" : "couldn't read any new-document preset");
	if (p.has("width")) settings.docWidth = (AIReal) ReqNum(p, "width");
	if (p.has("height")) settings.docHeight = (AIReal) ReqNum(p, "height");
	if (p.has("title")) settings.docTitle = U(ReqStr(p, "title"));
	if (p.has("artboards")) settings.docNumArtboards = (ai::int32) ReqNum(p, "artboards");
	std::string mode = p.str("colorMode", "");
	if (mode == "rgb") settings.docColorMode = kAIRGBColorModel;
	else if (mode == "cmyk") settings.docColorMode = kAICMYKColorModel;
	else if (!mode.empty()) Fail(kErrInvalidParams, "'colorMode' must be \"rgb\" or \"cmyk\"");
	AIDocumentHandle doc = nullptr;
	Check(sAIDocumentList->New(preset, &settings, kDialogOff, &doc), "New document");
	return DocumentInfo(p);
}

json::Value DocumentOpen(const json::Value& p)
{
	ai::FilePath path(U(ReqStr(p, "path")));
	AIDocumentHandle doc = nullptr;
	Check(sAIDocumentList->Open(path, kAIUnknownColorModel, kDialogOff, false, &doc), "Open document");
	return DocumentInfo(p);
}

AIDocumentHandle DocumentByIndex(const json::Value& p)
{
	if (!p.has("index")) return ActiveDocument();
	int index = (int) ReqNum(p, "index");
	auto docs = OpenDocuments();   // numbered as document.list numbers them
	if (index < 0 || index >= (int) docs.size()) Fail(kErrNotFound, "no document at index " + std::to_string(index));
	return docs[index].first;
}

json::Value DocumentActivate(const json::Value& p)
{
	Check(sAIDocumentList->Activate(DocumentByIndex(p), true), "Activate document");
	return DocumentInfo(p);
}

// Illustrator's file formats, by the names WriteDocument takes.
json::Value DocumentFormats(const json::Value&)
{
	Need(sAIFileFormat, "The file format suite");
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAIFileFormat->CountFileFormats(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AIFileFormatHandle f = nullptr;
		const char* name = nullptr;
		ai::int32 options = 0;
		if (sAIFileFormat->GetNthFileFormat(i, &f) || sAIFileFormat->GetFileFormatName(f, &name) || !name) continue;
		sAIFileFormat->GetFileFormatOptions(f, &options);
		if (!(options & (kFileFormatWrite | kFileFormatExport))) continue;
		json::Value v;
		v["name"] = name;
		ai::UnicodeString ext;
		if (!sAIFileFormat->GetFileFormatExtension(f, ext)) v["extensions"] = S(ext);
		v["save"] = (options & kFileFormatWrite) != 0;
		v["export"] = (options & kFileFormatExport) != 0;
		list.push(v);
	}
	return list;
}

// The native .ai writer: its name varies by version, so find it by extension
// ("ai,ait" - the one listed first is what it writes).
std::string NativeFormat()
{
	json::Value formats = DocumentFormats(json::Value());
	for (const json::Value& f : formats.asArray()) {
		std::string ext = f.str("extensions");
		if (f.boolean("save") && ext.substr(0, ext.find_first_of(",;")) == "ai") return f.get("name").asString();
	}
	Fail(kErrUnavailable, "couldn't find Illustrator's native save format - see document.formats");
}

// Save or export to a file: never shows a dialog (a failed write otherwise
// pops Illustrator's "unknown error" alert and blocks everything behind it).
void WriteTo(const std::string& pathText, const std::string& format)
{
	bool save = false, found = false;
	json::Value formats = DocumentFormats(json::Value());   // keep it alive while iterating
	for (const json::Value& f : formats.asArray())
		if (f.get("name").asString() == format) { found = true; save = f.boolean("save"); }
	if (!found) Fail(kErrInvalidParams, "no file format named '" + format + "' - see document.formats");
	ai::FilePath path(U(pathText));
	ai::int32 options = (save ? kFileFormatWrite : kFileFormatExport) | kFileFormatSuppressUI;
	Check(sAIDocument->WriteDocumentWithOptions(path, format.c_str(), options, nullptr, false, nullptr), "WriteDocument");
}

// Native .ai: the AI writer asks for its options (version, PDF compatibility)
// even with kFileFormatSuppressUI, so play Save As the way a recorded action
// does, dialog off. Unlike a copy, the document now lives at this path.
// PDF compatibility is asked for: left out, the file has only the "saved
// without PDF content" placeholder for anything but Illustrator, and the
// document keeps that setting, so every later export renders the placeholder.
void SaveAsNative(const std::string& path, const std::string& format)
{
	Need(sAIActionManager, "The action manager suite");
	AIActionParamValueRef params = nullptr;
	Check(sAIActionManager->AINewActionParamValue(&params), "AINewActionParamValue");
	AIErr e = sAIActionManager->AIActionSetStringUS(params, 'name', U(path));
	if (!e) e = sAIActionManager->AIActionSetString(params, 'frmt', format.c_str());
	if (!e) e = sAIActionManager->AIActionSetBoolean(params, 'pdf ', true);
	if (!e) e = sAIActionManager->AIActionSetBoolean(params, 'cmpr', true);
	if (!e) e = sAIActionManager->PlayActionEvent("adobe_saveDocumentAs", kDialogOff, params);
	sAIActionManager->AIDeleteActionParamValue(params);
	Check(e, "Save As");
}

// ------------------------------------------------------------------ export

// Short names agents use, and Illustrator's own name for each writer.
const std::map<std::string, std::string>& Writers()
{
	static const std::map<std::string, std::string> writers = {
		{"pdf", "PDF File Format"}, {"svg", "svg file format"}, {"svgz", "svg compressed file format"},
		{"tiff", "TIFF"}, {"psd", "Photoshop PSD Export"}, {"webp", "WebP"}, {"eps", "Adobe Illustrator EPSF"},
		{"bmp", "BMP"}, {"tga", "Targa"}, {"dxf", "DXF Export"}, {"dwg", "DWG Export"},
		{"emf", "Enhanced Metafile"}, {"wmf", "Windows Metafile"}, {"ai", "Adobe Illustrator Any Format Writer"},
	};
	return writers;
}

// "png", "JPG", ".jpeg", or the path's extension when there's no 'format'.
std::string FormatKind(const json::Value& p, const std::string& path)
{
	std::string f = Lower(p.str("format", ""));
	if (f.empty()) {
		size_t dot = path.find_last_of('.'), slash = path.find_last_of('/');
		if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) f = Lower(path.substr(dot + 1));
	}
	if (!f.empty() && f[0] == '.') f = f.substr(1);
	if (f == "jpeg" || f == "jpe") f = "jpg";
	if (f == "tif") f = "tiff";
	return f;
}

bool ValidRect(const AIRealRect& r) { return r.right > r.left && r.top != r.bottom && r.right - r.left < 32000; }

void Grow(AIRealRect& all, const AIRealRect& r, bool& any)
{
	if (!ValidRect(r)) return;
	if (!any) { all = r; any = true; return; }
	all.left = std::min(all.left, r.left); all.right = std::max(all.right, r.right);
	all.top = std::max(all.top, r.top); all.bottom = std::min(all.bottom, r.bottom);
}

AIRealRect ArtboardRect(const json::Value& p)
{
	Need(sAIArtboard, "The artboard suite");
	ai::ArtboardList list;
	Check(sAIArtboard->GetArtboardList(list), "GetArtboardList");
	ai::ArtboardID count = 0, index = 0;
	sAIArtboard->GetCount(list, count);
	sAIArtboard->GetActive(list, index);
	if (p.get("artboard").isNumber()) index = (ai::ArtboardID) p.get("artboard").asInt();
	AIRealRect rect = {0, 0, 0, 0};
	ai::ArtboardProperties props;
	AIErr e = index < 0 || index >= count ? kBadParameterErr : sAIArtboard->Init(props);
	if (!e) e = sAIArtboard->GetArtboardProperties(list, index, props);
	if (!e) e = sAIArtboard->GetPosition(props, rect);
	sAIArtboard->Dispose(props);
	sAIArtboard->ReleaseArtboardList(list);
	if (e) Fail(kErrNotFound, "no artboard " + std::to_string(index) + " (there are " + std::to_string(count) + ", from 0)");
	return rect;
}

// Never cancels. Required: Illustrator calls it without checking for null.
AIAPI AIBoolean KeepRasterizing(ai::int32, ai::int32) { return true; }

// Illustrator's own rasterizer draws one layer's art inside 'crop' to a PNG,
// on a clear background. (Exports went through a PDF copy, but once a
// document has .ai save settings - saved, or opened from an .ai or PDF - the
// PDF writer writes only the "saved without PDF content" page.)
void RasterizeTo(const std::string& file, AIArtHandle art, const AIRealRect& crop, double dpi, size_t wide, size_t high)
{
	Need(sAIImageOpt, "The image optimization suite");
	Need(sAIDataFilter, "The data filter suite");
	AIImageOptPNGParams2 params;
	params.versionOneSuiteParams.interlaced = false;
	params.versionOneSuiteParams.numberOfColors = 0;
	params.versionOneSuiteParams.transparentIndex = 0;
	params.versionOneSuiteParams.resolution = (AIFloat) dpi;
	params.versionOneSuiteParams.outAlpha = true;
	params.versionOneSuiteParams.outWidth = (ai::int32) wide;
	params.versionOneSuiteParams.outHeight = (ai::int32) high;
	params.antialias = 2;   // art and text both smoothed
	params.cropBox = crop;
	params.backgroundIsTransparent = true;
	AIDataFilter* filter = nullptr;
	Check(sAIDataFilter->NewFileDataFilter(ai::FilePath(U(file)), "write", 'prw ', 'PNGf', &filter), "NewFileDataFilter");
	AIErr e = sAIDataFilter->LinkDataFilter(nullptr, filter);
	if (!e) e = sAIImageOpt->MakePNG24(art, filter, params, KeepRasterizing);
	AIDataFilter* prev = nullptr;
	AIErr closed = sAIDataFilter->UnlinkDataFilter(filter, &prev);
	Check(e, "MakePNG24");
	Check(closed, "writing the render");
}

// PNG / JPEG at any resolution, cropped to the artboard, all the art, or one
// object: Illustrator rasterizes, the system writes the file (Raster.h).
json::Value ExportRaster(const json::Value& p, const std::string& path, const std::string& kind)
{
	ActiveDocument();
	double dpi = p.has("dpi") ? ReqNum(p, "dpi") : 72.0 * p.num("scale", 1);
	if (dpi < 1 || dpi > 2400) Fail(kErrInvalidParams, "'dpi' must be 1 to 2400 ('scale' 1 = 72 dpi, 2 = 144)");
	bool object = IsId(p.get("id")) || p.has("ids");
	std::string area = object ? "object" : p.str("area", "artboard");
	if (area != "artboard" && area != "art" && area != "object") Fail(kErrInvalidParams, "'area' must be \"artboard\" or \"art\"");

	// The artboard and the part of it to render, in artwork points.
	json::Value which = p;
	ai::ArtboardID index = 0;
	{
		ai::ArtboardList list;
		Check(Need(sAIArtboard, "The artboard suite")->GetArtboardList(list), "GetArtboardList");
		sAIArtboard->GetActive(list, index);
		sAIArtboard->ReleaseArtboardList(list);
		if (p.get("artboard").isNumber()) index = (ai::ArtboardID) p.get("artboard").asInt();
		which["artboard"] = (double) index;
	}
	AIRealRect board = ArtboardRect(which), crop = board;
	bool any = area == "artboard";
	if (area == "object") {
		std::vector<AIArtHandle> arts = ArtList(p);
		for (AIArtHandle a : arts) { AIRealRect r; if (!sAIArt->GetArtBounds(a, &r)) Grow(crop, r, any); }
	}
	else if (area == "art") {
		ai::int32 layers = 0;
		sAILayer->CountLayers(&layers);
		for (ai::int32 i = 0; i < layers; i++) {
			AILayerHandle layer = nullptr;
			AIArtHandle group = nullptr;
			AIRealRect r;
			if (!sAILayer->GetNthLayer(i, &layer) && !sAIArt->GetFirstArtOfLayer(layer, &group) && group && !sAIArt->GetArtBounds(group, &r))
				Grow(crop, r, any);
		}
	}
	if (!any) Fail(kErrInvalidParams, "there's nothing to export there");
	// Exports stay inside the artboard.
	crop.left = std::max(crop.left, board.left); crop.right = std::min(crop.right, board.right);
	crop.top = std::min(crop.top, board.top); crop.bottom = std::max(crop.bottom, board.bottom);
	if (crop.right <= crop.left || crop.top <= crop.bottom) Fail(kErrInvalidParams, "that's outside artboard " + std::to_string(index));

	double k = dpi / 72.0;
	RasterJob job;
	job.pixelsWide = (size_t) std::max(1.0, std::ceil((crop.right - crop.left) * k));
	job.pixelsHigh = (size_t) std::max(1.0, std::ceil((crop.top - crop.bottom) * k));
	if ((double) job.pixelsWide * job.pixelsHigh > 400e6) Fail(kErrInvalidParams, "that's over 400 megapixels - lower 'scale' or 'dpi'");
	job.dpi = dpi;
	job.png = kind == "png";
	job.transparent = p.boolean("transparent", true);
	job.quality = (int) p.num("quality", 90);
	job.out = path;
	// Each layer renders on its own (the rasterizer takes one art tree), bottom
	// first; Raster.cpp stacks them. Hidden and template layers stay out, as
	// in Illustrator's own export.
	auto cleanup = [&job] { for (const std::string& f : job.images) platform::RemoveFile(f); };
	try {
		ai::int32 layers = 0;
		sAILayer->CountLayers(&layers);
		for (ai::int32 i = layers - 1; i >= 0; i--) {
			AILayerHandle layer = nullptr;
			AIArtHandle group = nullptr;
			AIBoolean visible = false, hidden = false;
			if (sAILayer->GetNthLayer(i, &layer) || !layer) continue;
			sAILayer->GetLayerVisible(layer, &visible);
			sAILayer->GetLayerIsTemplate(layer, &hidden);
			if (!visible || hidden || sAIArt->GetFirstArtOfLayer(layer, &group) || !group) continue;
			job.images.push_back(path + ".slippy" + std::to_string(i) + ".png");
			RasterizeTo(job.images.back(), group, crop, dpi, job.pixelsWide, job.pixelsHigh);
		}
	}
	catch (...) { cleanup(); throw; }
	std::string error = EncodeImage(job);
	cleanup();
	if (!error.empty()) Fail(kErrIllustrator, error);
	size_t w = job.pixelsWide, h = job.pixelsHigh;
	json::Value v;
	v["path"] = path;
	v["format"] = kind;
	v["width"] = (double) w;
	v["height"] = (double) h;
	v["dpi"] = dpi;
	v["area"] = area;
	v["artboard"] = (double) index;
	return v;
}

json::Value DocumentExport(const json::Value& p)
{
	std::string path = ReqStr(p, "path");
	if (!platform::IsAbsolutePath(path)) Fail(kErrInvalidParams, "'path' must be absolute");
	std::string kind = FormatKind(p, path);
	if (kind == "png" || kind == "jpg") return ExportRaster(p, path, kind);
	auto w = Writers().find(kind);
	std::string format = w != Writers().end() ? w->second : p.str("format", "");
	if (format.empty()) Fail(kErrInvalidParams, "say which format: png, jpg, pdf, svg, tiff, psd, webp... (or give the path an extension)");
	ActiveDocument();
	WriteTo(path, format);
	json::Value v;
	v["path"] = path;
	v["format"] = kind.empty() ? format : kind;
	return v;
}

json::Value DocumentSave(const json::Value& p)
{
	AIDocumentHandle doc = ActiveDocument();
	if (p.has("path")) {
		std::string format = p.has("format") ? p.str("format") : NativeFormat();
		auto w = Writers().find(Lower(format));
		if (w != Writers().end() && Lower(format) != "ai") format = w->second;
		if (Lower(format) == "png" || Lower(format) == "jpg" || Lower(format) == "jpeg")
			return DocumentExport(p);
		if (format == NativeFormat()) SaveAsNative(ReqStr(p, "path"), format);
		else WriteTo(ReqStr(p, "path"), format);
	}
	else {
		if (DocPath(doc).empty()) Fail(kErrInvalidParams, "this document was never saved - pass 'path'");
		Check(sAIDocumentList->Save(doc), "Save document");
	}
	json::Value v;
	v["saved"] = true;
	v["path"] = p.has("path") ? p.str("path") : DocPath(doc);
	return v;
}

json::Value DocumentClose(const json::Value& p)
{
	AIDocumentHandle doc = DocumentByIndex(p);
	std::string name = DocName(doc);
	if (p.boolean("save", false)) Check(sAIDocumentList->Save(doc), "Save document");
	else {
		// Close asks "Save changes?" of a modified document, and with dialogs
		// off (as while agents run) that answers Save. Marked unmodified, it
		// just closes, discarding the changes as asked.
		Check(sAIDocumentList->Activate(doc, false), "Activate");
		Check(Need(sAIDocument, "The document suite")->SetDocumentModified(false), "SetDocumentModified");
	}
	// Close takes one window at a time; the document is gone with its last.
	auto open = [&] {
		for (auto& d : OpenDocuments()) if (d.first == doc) return d.second;
		return 0;
	};
	for (int windows = open(); windows > 0; windows--) {
		Check(sAIDocumentList->Close(doc), "Close document");
		if (!open()) break;
	}
	json::Value v;
	v["closed"] = name;
	return v;
}

json::Value LayerList(const json::Value&)
{
	ActiveDocument();
	ai::int32 count = 0;
	sAILayer->CountLayers(&count);
	AILayerHandle current = nullptr;
	sAILayer->GetCurrentLayer(&current);
	json::Value list = json::Value::MakeArray();
	for (ai::int32 i = 0; i < count; i++) {
		AILayerHandle layer = nullptr;
		if (sAILayer->GetNthLayer(i, &layer)) continue;
		json::Value l;
		l["index"] = i;
		l["name"] = LayerTitle(layer);
		AIBoolean visible = true, editable = true;
		sAILayer->GetLayerVisible(layer, &visible);
		sAILayer->GetLayerEditable(layer, &editable);
		l["visible"] = (bool) visible;
		l["locked"] = !editable;
		l["current"] = layer == current;
		AIArtHandle group = nullptr;
		if (!sAIArt->GetFirstArtOfLayer(layer, &group) && group) l["id"] = ArtId(group);
		list.push(l);
	}
	return list;
}

json::Value LayerCreate(const json::Value& p)
{
	ActiveDocument();
	AILayerHandle layer = nullptr;
	// Inside 'parent' (a sub-layer), right 'above' / 'below' another layer, or on top.
	auto named = [](const json::Value& v, const char* what) {
		json::Value q;
		q["layer"] = v;
		AILayerHandle h = LayerByParam(q);
		if (!h) Fail(kErrInvalidParams, std::string("'") + what + "' must be a layer name or index");
		return h;
	};
	if (p.has("parent")) Check(sAILayer->InsertLayer(named(p.get("parent"), "parent"), kPlaceInsideOnTop, &layer), "InsertLayer");
	else if (p.has("above")) Check(sAILayer->InsertLayer(named(p.get("above"), "above"), kPlaceAbove, &layer), "InsertLayer");
	else if (p.has("below")) Check(sAILayer->InsertLayer(named(p.get("below"), "below"), kPlaceBelow, &layer), "InsertLayer");
	else Check(sAILayer->InsertLayer(nullptr, kPlaceAboveAll, &layer), "InsertLayer");
	if (p.has("name")) Check(sAILayer->SetLayerTitle(layer, U(ReqStr(p, "name"))), "SetLayerTitle");
	if (p.boolean("current", true)) sAILayer->SetCurrentLayer(layer);
	json::Value v;
	v["name"] = LayerTitle(layer);
	return v;
}

json::Value LayerSet(const json::Value& p)
{
	ActiveDocument();
	AILayerHandle layer = LayerByParam(p);
	if (!layer) Fail(kErrInvalidParams, "pass 'layer' (name or index)");
	if (p.has("name")) Check(sAILayer->SetLayerTitle(layer, U(ReqStr(p, "name"))), "SetLayerTitle");
	if (p.has("visible")) Check(sAILayer->SetLayerVisible(layer, p.boolean("visible")), "SetLayerVisible");
	if (p.has("locked")) Check(sAILayer->SetLayerEditable(layer, !p.boolean("locked")), "SetLayerEditable");
	if (p.boolean("current", false)) Check(sAILayer->SetCurrentLayer(layer), "SetCurrentLayer");
	if (p.has("template")) Check(sAILayer->SetLayerIsTemplate(layer, p.boolean("template")), "SetLayerIsTemplate");
	if (p.has("printable")) Check(sAILayer->SetLayerPrinted(layer, p.boolean("printable")), "SetLayerPrinted");
	if (p.has("preview")) Check(sAILayer->SetLayerPreview(layer, p.boolean("preview")), "SetLayerPreview");
	if (p.has("dimImages")) Check(sAILayer->SetLayerDimPlacedImages(layer, p.boolean("dimImages")), "SetLayerDimPlacedImages");
	if (p.has("color")) {
		AIColor c = ParseColor(p.get("color"), "color");
		if (c.kind != kThreeColor) Fail(kErrInvalidParams, "'color' must be \"#RRGGBB\" or {\"rgb\"}");
		AIRGBColor rgb;
		rgb.red = (ai::uint16) (c.c.rgb.red * 65535);
		rgb.green = (ai::uint16) (c.c.rgb.green * 65535);
		rgb.blue = (ai::uint16) (c.c.rgb.blue * 65535);
		Check(sAILayer->SetLayerColor(layer, rgb), "SetLayerColor");
	}
	if (p.boolean("delete", false)) { Check(sAILayer->DeleteLayer(layer), "DeleteLayer"); return json::Value("deleted"); }
	json::Value v;
	v["name"] = LayerTitle(layer);
	return v;
}

json::Value ArtTree(const json::Value& p)
{
	ActiveDocument();
	int depth = (int) p.num("depth", 3);
	if (IsId(p.get("id"))) return ArtSummary(ArtById(IdText(p.get("id"))), depth);
	json::Value out = json::Value::MakeArray();
	ai::int32 count = 0;
	sAILayer->CountLayers(&count);
	AILayerHandle only = LayerByParam(p);
	for (ai::int32 i = 0; i < count; i++) {
		AILayerHandle layer = nullptr;
		if (sAILayer->GetNthLayer(i, &layer) || (only && layer != only)) continue;
		AIArtHandle group = nullptr;
		if (sAIArt->GetFirstArtOfLayer(layer, &group) || !group) continue;
		json::Value l = ArtSummary(group, depth);
		l["type"] = "layer";
		l["name"] = LayerTitle(layer);
		l["index"] = i;
		out.push(l);
	}
	return out;
}

json::Value ArtGet(const json::Value& p)
{
	AIArtHandle art = ArtById(IdText(Required(p, "id")));
	json::Value v = ArtSummary(art, (int) p.num("depth", 1));
	short type = ArtType(art);
	AILayerHandle layer = nullptr;
	if (!sAIArt->GetLayerOfArt(art, &layer) && layer) v["layer"] = LayerTitle(layer);
	AIArtHandle parent = nullptr;
	if (!sAIArt->GetArtParent(art, &parent) && parent) v["parent"] = ArtId(parent);
	if (type == kPathArt) {
		v["style"] = StyleJson(art);
		json::Value path = PathJson(art);
		v["closed"] = path.get("closed");
		v["segments"] = path.get("segments");
	}
	if (type == kTextFrameArt) v["contents"] = TextOf(art);
	return v;
}

json::Value ArtSelection(const json::Value& p)
{
	ActiveDocument();
	json::Value list = json::Value::MakeArray();
	for (AIArtHandle a : SelectedArt()) list.push(ArtSummary(a, (int) p.num("depth", 0)));
	return list;
}

json::Value ArtSelect(const json::Value& p)
{
	ActiveDocument();
	Need(sAIMatchingArt, "The matching art suite");
	std::vector<AIArtHandle> arts = p.has("id") || p.has("ids") ? ArtList(p) : std::vector<AIArtHandle>{};
	if (!p.boolean("add", false)) sAIMatchingArt->DeselectAll();
	for (AIArtHandle a : arts) SelectDeep(a);
	return ArtSelection(json::Value());
}

json::Value ArtDelete(const json::Value& p)
{
	std::vector<AIArtHandle> arts = ArtList(p);
	int n = 0;
	for (AIArtHandle a : arts) if (sAIArt->ValidArt(a, true)) { Check(sAIArt->DisposeArt(a), "DisposeArt"); n++; }
	json::Value v;
	v["deleted"] = n;
	return v;
}

json::Value ArtSet(const json::Value& p)
{
	std::vector<AIArtHandle> arts = ArtList(p, true);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : arts) {
		if (p.get("name").isString()) Check(sAIArt->SetArtName(a, U(p.get("name").asString())), "SetArtName");
		if (p.has("hidden")) Check(sAIArt->SetArtUserAttr(a, kArtHidden, p.boolean("hidden") ? kArtHidden : 0), "hide");
		if (p.has("locked")) Check(sAIArt->SetArtUserAttr(a, kArtLocked, p.boolean("locked") ? kArtLocked : 0), "lock");
		ApplyStyle(a, p);
		SetBlend(a, p);
		if (p.get("contents").isString()) {
			if (ArtType(a) != kTextFrameArt) Fail(kErrInvalidParams, "'contents' only applies to text");
			SetText(a, p.get("contents").asString(), p);
		}
		out.push(ArtSummary(a, 0));
	}
	return out;
}

json::Value ArtDuplicate(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIArtHandle copy = nullptr;
		Check(sAIArt->DuplicateArt(a, kPlaceAbove, a, &copy), "DuplicateArt");
		overlay::Touch(copy);
		out.push(ArtSummary(copy, 0));
	}
	return out;
}

json::Value ArtArrange(const json::Value& p)
{
	std::string to = ReqStr(p, "to");
	ai::int16 order;
	if (to == "front") order = kPlaceInsideOnTop;
	else if (to == "back") order = kPlaceInsideOnBottom;
	else Fail(kErrInvalidParams, "'to' must be \"front\" or \"back\"");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIArtHandle parent = nullptr;
		Check(sAIArt->GetArtParent(a, &parent), "GetArtParent");
		Check(sAIArt->ReorderArt(a, order, parent), "ReorderArt");
		out.push(ArtSummary(a, 0));
	}
	return out;
}

// Transforming a group with TransformArt moves it but records no undo step
// (30.2), and a compound path doesn't move at all, so both are transformed
// through their contents.
void TransformDeep(AIArtHandle art, AIRealMatrix& m, AIReal lineScale, ai::int32 flags)
{
	short type = ArtType(art);
	if (type == kGroupArt || type == kCompoundPathArt) {
		AIArtHandle child = nullptr;
		sAIArt->GetArtFirstChild(art, &child);
		for (; child; sAIArt->GetArtSibling(child, &child)) TransformDeep(child, m, lineScale, flags);
		return;
	}
	Check(sAITransformArt->TransformArt(art, &m, lineScale, flags), "TransformArt");
}

json::Value ArtTransform(const json::Value& p)
{
	Need(sAITransformArt, "The transform art suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	double sx = 1, sy = 1, angle = p.num("rotate", 0) * M_PI / 180.0, dx = 0, dy = 0;
	const json::Value& scale = p.get("scale");
	if (scale.isNumber()) sx = sy = scale.asNumber();
	else if (!scale.isNull()) { AIRealPoint s = Point(scale, "scale"); sx = s.h; sy = s.v; }
	if (p.has("translate")) { AIRealPoint t = Point(p.get("translate"), "translate"); dx = t.h; dy = t.v; }

	// Origin: the combined bounds' center unless given.
	AIRealPoint origin = {0, 0};
	if (p.get("origin").isArray()) origin = Point(p.get("origin"), "origin");
	else {
		AIRealRect all = {0, 0, 0, 0};
		bool first = true;
		for (AIArtHandle a : arts) {
			AIRealRect r;
			if (sAIArt->GetArtBounds(a, &r)) continue;
			if (first) { all = r; first = false; continue; }
			all.left = std::min(all.left, r.left); all.right = std::max(all.right, r.right);
			all.top = std::max(all.top, r.top); all.bottom = std::min(all.bottom, r.bottom);
		}
		origin.h = (all.left + all.right) / 2;
		origin.v = (all.top + all.bottom) / 2;
	}

	// M = T(origin + translate) * R * S * T(-origin)
	double c = std::cos(angle), s = std::sin(angle);
	AIRealMatrix m;
	m.a = (AIReal) (c * sx);  m.b = (AIReal) (s * sx);
	m.c = (AIReal) (-s * sy); m.d = (AIReal) (c * sy);
	m.tx = (AIReal) (origin.h + dx - (m.a * origin.h + m.c * origin.v));
	m.ty = (AIReal) (origin.v + dy - (m.b * origin.h + m.d * origin.v));

	bool scaleStrokes = p.boolean("scaleStrokes", true);
	ai::int32 flags = kTransformObjects | kTransformFillGradients | kTransformFillPatterns | kTransformStrokeGradients | kTransformStrokePatterns;
	if (scaleStrokes) flags |= kScaleLines;
	AIReal lineScale = (AIReal) std::sqrt(std::fabs(sx * sy));
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : arts) {
		TransformDeep(a, m, lineScale, flags);
		out.push(ArtSummary(a, 0));
	}
	return out;
}

// Keeps the stacking order: the art is sorted top first ("before" in the
// paint order), the group goes where the topmost was, and each piece is moved
// in to the group's bottom in turn. (Invoking the Group menu command from a
// timer message doesn't take effect, found testing in 30.2.)
// Front-most first, duplicates dropped.
std::vector<AIArtHandle> FrontToBack(std::vector<AIArtHandle> arts)
{
	std::sort(arts.begin(), arts.end(), [](AIArtHandle a, AIArtHandle b) {
		short order = kUnknownOrder;
		return a != b && !sAIArt->GetArtOrder(a, b, &order) && order == kFirstBeforeSecond;
	});
	arts.erase(std::unique(arts.begin(), arts.end()), arts.end());
	return arts;
}

// Moves art next to or into 'prep', keeping its stacking order among itself.
void MoveAll(const std::vector<AIArtHandle>& arts, ai::int16 order, AIArtHandle prep)
{
	for (AIArtHandle a : arts) {
		for (AIArtHandle up = prep; up; sAIArt->GetArtParent(up, &up))
			if (up == a) Fail(kErrInvalidParams, "can't move art " + ArtId(a) + " into or next to itself");
	}
	std::vector<AIArtHandle> list = FrontToBack(arts);
	// Each move lands right against prep, so go in the order that leaves the
	// front-most on top.
	if (order == kPlaceBelow || order == kPlaceInsideOnTop) std::reverse(list.begin(), list.end());
	for (AIArtHandle a : list) Check(sAIArt->ReorderArt(a, order, prep), "ReorderArt");
}

// Where art goes: 'into' a group (top, or 'position' "bottom"), or right
// 'above' / 'below' another object. False when none is given.
bool Destination(const json::Value& p, ai::int16& order, AIArtHandle& prep)
{
	int given = IsId(p.get("into")) + IsId(p.get("above")) + IsId(p.get("below"));
	if (given > 1) Fail(kErrInvalidParams, "give one of 'into', 'above' or 'below'");
	if (IsId(p.get("into"))) {
		prep = ArtById(IdText(p.get("into")));
		if (ArtType(prep) != kGroupArt) Fail(kErrInvalidParams, "'into' must be a group (a clipping group is fine)");
		std::string position = p.str("position", "top");
		if (position != "top" && position != "bottom") Fail(kErrInvalidParams, "'position' must be \"top\" or \"bottom\"");
		order = position == "top" ? kPlaceInsideOnTop : kPlaceInsideOnBottom;
	}
	else if (IsId(p.get("above"))) { prep = ArtById(IdText(p.get("above"))); order = kPlaceAbove; }
	else if (IsId(p.get("below"))) { prep = ArtById(IdText(p.get("below"))); order = kPlaceBelow; }
	else return false;
	return true;
}

json::Value ArtGroup(const json::Value& p)
{
	std::vector<AIArtHandle> arts = FrontToBack(ArtList(p, true));
	AIArtHandle group = nullptr;
	Check(sAIArt->NewArt(kGroupArt, kPlaceAbove, arts.front(), &group), "NewArt group");
	MoveAll(arts, kPlaceInsideOnBottom, group);
	return Finish(group, p);
}

json::Value ArtMove(const json::Value& p)
{
	std::vector<AIArtHandle> arts = ArtList(p, true);
	ai::int16 order;
	AIArtHandle prep = nullptr;
	if (!Destination(p, order, prep)) Fail(kErrInvalidParams, "say where: 'into' (a group id), 'above' or 'below' (an art id)");
	MoveAll(arts, order, prep);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : FrontToBack(arts)) out.push(ArtSummary(a, 0));
	return out;
}

bool IsLayerGroup(AIArtHandle art)
{
	AIBoolean layerGroup = false;
	sAIArt->IsArtLayerGroup(art, &layerGroup);
	return layerGroup;
}

std::vector<AIArtHandle> Children(AIArtHandle group)
{
	std::vector<AIArtHandle> out;
	AIArtHandle child = nullptr;
	for (sAIArt->GetArtFirstChild(group, &child); child; sAIArt->GetArtSibling(child, &child)) out.push_back(child);
	return out;
}

// Turns a clipping group back into a plain one. The mask stays, unpainted,
// as Object > Clipping Mask > Release leaves it.
void Unclip(AIArtHandle group)
{
	Need(sAIGroup, "The group suite");
	for (AIArtHandle c : Children(group))   // the masks first, while the group still clips
		if (Attr(c, kArtIsClipMask)) Check(sAIArt->SetArtUserAttr(c, kArtIsClipMask, 0), "clear clip mask");
	Check(sAIGroup->SetGroupClipped(group, false), "SetGroupClipped");
}

json::Value ArtClip(const json::Value& p)
{
	Need(sAIGroup, "The group suite");
	std::vector<AIArtHandle> arts = FrontToBack(ArtList(p, true));
	AIArtHandle mask = IsId(p.get("mask")) ? ArtById(IdText(p.get("mask"))) : arts.front();
	if (std::find(arts.begin(), arts.end(), mask) == arts.end()) arts.insert(arts.begin(), mask);
	if (arts.size() < 2) Fail(kErrInvalidParams, "a clipping mask needs the mask and at least one object to clip");
	short type = ArtType(mask);
	if (type != kPathArt && type != kCompoundPathArt && type != kTextFrameArt)
		Fail(kErrInvalidParams, "the mask (art " + ArtId(mask) + ") must be a path, compound path or text, not " + TypeName(type));
	if (type == kPathArt) {
		ai::int16 segments = 0;
		sAIPath->GetPathSegmentCount(mask, &segments);
		if (segments < 2) Fail(kErrInvalidParams, "the mask path needs at least two anchor points");
	}
	AIArtHandle group = nullptr;
	Check(sAIArt->NewArt(kGroupArt, kPlaceAbove, arts.front(), &group), "NewArt group");
	MoveAll(arts, kPlaceInsideOnBottom, group);
	Check(sAIArt->ReorderArt(mask, kPlaceInsideOnTop, group), "ReorderArt mask");
	// Like Object > Clipping Mask > Make: the mask loses its paint.
	json::Value none;
	none["fill"] = "none";
	none["stroke"] = "none";
	ApplyStyle(mask, none);
	// The group first: Illustrator only takes a mask already inside a clipping group.
	Check(sAIGroup->SetGroupClipped(group, true), "SetGroupClipped");
	Check(sAIArt->SetArtUserAttr(mask, kArtIsClipMask, kArtIsClipMask), "set clip mask");
	return Finish(group, p);
}

json::Value ArtUnclip(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	Need(sAIGroup, "The group suite");
	for (AIArtHandle g : ArtList(p, true)) {
		AIBoolean clipped = false;
		if (ArtType(g) == kGroupArt) sAIGroup->GetGroupClipped(g, &clipped);
		if (!clipped) Fail(kErrInvalidParams, "art " + ArtId(g) + " isn't a clipping group");
		Unclip(g);
		out.push(ArtSummary(g, 1));
	}
	return out;
}

json::Value ArtUngroup(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle g : ArtList(p, true)) {
		if (ArtType(g) != kGroupArt) Fail(kErrInvalidParams, "art " + ArtId(g) + " isn't a group");
		if (IsLayerGroup(g)) Fail(kErrInvalidParams, "art " + ArtId(g) + " is a layer's own group - use layer.set");
		AIBoolean clipped = false;
		if (sAIGroup && !sAIGroup->GetGroupClipped(g, &clipped) && clipped) Unclip(g);
		std::vector<AIArtHandle> kids = Children(g);
		if (!kids.empty()) MoveAll(kids, kPlaceAbove, g);
		Check(sAIArt->DisposeArt(g), "DisposeArt group");
		for (AIArtHandle k : FrontToBack(kids)) {
			overlay::Touch(k);
			out.push(ArtSummary(k, 0));
		}
	}
	return out;
}

// Scales and centers art on a target's bounds: "fill" covers it (the
// clipping group crops the rest), "fit" fits inside, "stretch" matches both.
void FitTo(AIArtHandle art, AIArtHandle target, const std::string& how)
{
	AIRealRect a, t;
	Check(sAIArt->GetArtBounds(art, &a), "GetArtBounds");
	Check(sAIArt->GetArtBounds(target, &t), "GetArtBounds target");
	double aw = std::fabs(a.right - a.left), ah = std::fabs(a.top - a.bottom);
	double tw = std::fabs(t.right - t.left), th = std::fabs(t.top - t.bottom);
	if (aw <= 0 || ah <= 0) Fail(kErrInvalidParams, "the art has no size to scale");
	double sx = tw / aw, sy = th / ah;
	if (how == "fill") sx = sy = std::max(sx, sy);
	else if (how == "fit") sx = sy = std::min(sx, sy);
	else if (how != "stretch") Fail(kErrInvalidParams, "'fit' must be \"fill\", \"fit\" or \"stretch\"");
	double ax = (a.left + a.right) / 2, ay = (a.top + a.bottom) / 2, tx = (t.left + t.right) / 2, ty = (t.top + t.bottom) / 2;
	AIRealMatrix m;
	m.a = (AIReal) sx; m.b = 0; m.c = 0; m.d = (AIReal) sy;
	m.tx = (AIReal) (tx - sx * ax);
	m.ty = (AIReal) (ty - sy * ay);
	ai::int32 flags = kTransformObjects | kTransformFillGradients | kTransformFillPatterns | kTransformStrokeGradients | kTransformStrokePatterns | kScaleLines;
	TransformDeep(art, m, (AIReal) std::sqrt(sx * sy), flags);
}

json::Value ArtPlace(const json::Value& p)
{
	Need(sAIPlaced, "The placed art suite");
	ActiveDocument();
	std::string path = ReqStr(p, "path");
	if (!platform::Readable(path)) Fail(kErrNotFound, "can't read " + path);
	// Where it goes and what it fits are checked before placing anything.
	ai::int16 order = kPlaceAboveAll;
	AIArtHandle prep = nullptr;
	if (!Destination(p, order, prep)) Placement(p, order, prep);
	AIArtHandle target = IsId(p.get("fitTo")) ? ArtById(IdText(p.get("fitTo"))) : nullptr;
	std::string how = p.str("fit", "fill");

	ai::FilePath file(U(path));
	AIPlaceRequestData request;
	request.m_lPlaceMode = kVanillaPlace;
	request.m_pFilePath = &file;
	request.m_filemethod = p.boolean("link", true) ? 1 : 0;
	request.m_disableTemplate = true;
	request.m_doShowParamDialog = false;
	Check(sAIPlaced->ExecPlaceRequest(request), "Place");
	AIArtHandle art = request.m_hNewArt;
	if (!art) Fail(kErrIllustrator, "Illustrator didn't place " + path);
	if (prep) Check(sAIArt->ReorderArt(art, order, prep), "ReorderArt");
	if (target) FitTo(art, target, how);
	return Finish(art, p);
}

json::Value ShapeRect(const json::Value& p)
{
	double x = ReqNum(p, "x"), y = ReqNum(p, "y"), w = ReqNum(p, "width"), h = ReqNum(p, "height");
	// (x, y) is the top-left corner; y grows upward, so the rect runs down to y - h.
	std::vector<AIPathSegment> segs = {Corner(x, y), Corner(x + w, y), Corner(x + w, y - h), Corner(x, y - h)};
	return Finish(NewPath(p, segs, true), p);
}

json::Value ShapeEllipse(const json::Value& p)
{
	double x = ReqNum(p, "x"), y = ReqNum(p, "y"), w = ReqNum(p, "width"), h = ReqNum(p, "height");
	double cx = x + w / 2, cy = y - h / 2, rx = w / 2, ry = h / 2, k = 0.5522847498;
	auto seg = [&](double px, double py, double ix, double iy, double ox, double oy) {
		AIPathSegment s;
		s.p.h = (AIReal) px; s.p.v = (AIReal) py;
		s.in.h = (AIReal) ix; s.in.v = (AIReal) iy;
		s.out.h = (AIReal) ox; s.out.v = (AIReal) oy;
		s.corner = false;
		return s;
	};
	// Top, right, bottom, left - clockwise.
	std::vector<AIPathSegment> segs = {
		seg(cx, cy + ry, cx - k * rx, cy + ry, cx + k * rx, cy + ry),
		seg(cx + rx, cy, cx + rx, cy + k * ry, cx + rx, cy - k * ry),
		seg(cx, cy - ry, cx + k * rx, cy - ry, cx - k * rx, cy - ry),
		seg(cx - rx, cy, cx - rx, cy - k * ry, cx - rx, cy + k * ry),
	};
	return Finish(NewPath(p, segs, true), p);
}

json::Value PathCreate(const json::Value& p)
{
	const json::Value& pts = Required(p, "points");
	if (!pts.isArray()) Fail(kErrInvalidParams, "'points' must be an array");
	std::vector<AIPathSegment> segs;
	for (const json::Value& v : pts.asArray()) segs.push_back(SegmentFrom(v));
	return Finish(NewPath(p, segs, p.boolean("closed", false)), p);
}

json::Value TextCreate(const json::Value& p)
{
	Need(sAITextFrame, "The text frame suite");
	AIRealPoint anchor = Point(Required(p, "position"), "position");
	ai::int16 order;
	AIArtHandle prep;
	Placement(p, order, prep);
	AIArtHandle art = nullptr;
	Check(sAITextFrame->NewPointText(order, prep, kHorizontalTextOrientation, anchor, &art), "NewPointText");
	SetText(art, ReqStr(p, "contents"), p);
	json::Value v = Finish(art, p);
	v["contents"] = TextOf(art);
	return v;
}

// Enough of Illustrator's state to tell whether a menu command did anything.
struct DocState {
	int documents = 0;
	AIDocumentHandle active = nullptr;
	ai::int32 undoSteps = 0, redoSteps = 0;
	AIBoolean modified = false;
	std::vector<AIArtHandle> selection;
	bool operator==(const DocState& o) const
	{
		return documents == o.documents && active == o.active && undoSteps == o.undoSteps && redoSteps == o.redoSteps &&
			modified == o.modified && selection == o.selection;
	}
};

DocState CurrentState()
{
	DocState s;
	ai::int32 n = (ai::int32) OpenDocuments().size();
	s.documents = n;
	if (n == 0 || sAIDocument->GetDocument(&s.active) || !s.active) return s;
	sAIDocument->GetDocumentModified(&s.modified);
	if (sAIUndo) sAIUndo->CountTransactions(&s.undoSteps, &s.redoSteps);
	if (sAIMatchingArt) {
		AIArtHandle** matches = nullptr;
		ai::int32 count = 0;
		if (!sAIMatchingArt->GetSelectedArt(&matches, &count) && matches) {
			for (ai::int32 i = 0; i < count; i++) s.selection.push_back((*matches)[i]);
			sSPBlocks->FreeBlock(matches);
		}
	}
	return s;
}

json::Value MenuRun(const json::Value& p)
{
	Need(sAICommandManager, "The command manager suite");
	std::string name = ReqStr(p, "command");
	AICommandID id = 0;
	// Its name ("group"), or the label people see ("Group").
	if ((sAICommandManager->GetCommandIDFromName(name.c_str(), &id) || !id) &&
		(sAICommandManager->GetCommandIDFromLocalizedName(U(name), &id) || !id))
		Fail(kErrNotFound, "no menu command named '" + name + "' - find it with menu.list {search: \"" + name + "\"}");
	// Illustrator runs a command that doesn't apply as a no-op (its alert is
	// suppressed), so compare before and after to say whether anything happened.
	if (sAIDocument) sAIDocument->SyncDocument();
	DocState before = CurrentState();
	Check(sAIMenu->InvokeMenuAction(id), "InvokeMenuAction");
	if (sAIDocument) sAIDocument->SyncDocument();
	json::Value v;
	v["ran"] = name;
	v["changed"] = !(CurrentState() == before);
	if (!v.get("changed").asBool())
		v["note"] = "Illustrator ran it but nothing changed - it probably doesn't apply to the current selection. "
			"For structure, prefer art.move, art.group, art.ungroup, art.clip, art.unclip and art.place.";
	return v;
}

json::Value PluginMessage(const json::Value& p)
{
	Need(sSPInterface, "The plug-in interface suite");
	std::string name = ReqStr(p, "plugin"), selector = ReqStr(p, "selector");
	SPPluginRef plugin = nullptr;
	if (sSPPlugins->GetNamedPlugin(name.c_str(), &plugin) || !plugin) Fail(kErrNotFound, "no plug-in named '" + name + "' is loaded");
	// The same message app.sendScriptMessage(plugin, selector, param) sends.
	AIScriptMessage msg;
	msg.inParam = U(p.str("param"));
	Check(sSPInterface->SetupMessageData(plugin, &msg.d), "SetupMessageData");
	SPErr result = kNoErr;
	SPErr e = sSPInterface->SendMessage(plugin, kCallerAIScriptMessage, selector.c_str(), &msg, &result);
	sSPInterface->EmptyMessageData(plugin, &msg.d);
	Check(e ? e : result, "SendMessage");
	json::Value v;
	v["plugin"] = name;
	v["selector"] = selector;
	v["reply"] = S(msg.outParam);
	return v;
}

ActionParamKeyID KeyId(const std::string& key)
{
	if (key.size() == 4) return ((ActionParamKeyID) (unsigned char) key[0] << 24) | ((ActionParamKeyID) (unsigned char) key[1] << 16) |
		((ActionParamKeyID) (unsigned char) key[2] << 8) | (ActionParamKeyID) (unsigned char) key[3];
	char* end = nullptr;
	unsigned long n = strtoul(key.c_str(), &end, 10);
	if (key.empty() || *end) Fail(kErrInvalidParams, "action parameter keys are 4-character codes (\"name\") or numbers");
	return (ActionParamKeyID) n;
}

json::Value ActionPlay(const json::Value& p)
{
	Need(sAIActionManager, "The action manager suite");
	std::string event = ReqStr(p, "event");
	std::string dialog = p.str("dialog", "off");
	ActionDialogStatus status = dialog == "on" ? kDialogOn : dialog == "none" ? kDialogNone : kDialogOff;
	AIActionParamValueRef params = nullptr;
	const json::Value& in = p.get("params");
	if (in.isObject()) {
		Check(sAIActionManager->AINewActionParamValue(&params), "AINewActionParamValue");
		try {
			for (const auto& kv : in.asObject()) {
				ActionParamKeyID key = KeyId(kv.first);
				const json::Value& v = kv.second;
				std::string type;
				json::Value value = v;
				if (v.isObject()) { type = v.str("type"); value = v.get("value"); }
				if (type.empty()) type = value.isBool() ? "boolean" : value.isString() ? "string"
					: value.isNumber() && value.asNumber() == std::floor(value.asNumber()) ? "integer" : "real";
				if (type == "boolean") Check(sAIActionManager->AIActionSetBoolean(params, key, value.asBool()), "set boolean");
				else if (type == "string") Check(sAIActionManager->AIActionSetStringUS(params, key, U(value.asString())), "set string");
				else if (type == "integer") Check(sAIActionManager->AIActionSetInteger(params, key, (ai::int32) value.asNumber()), "set integer");
				else if (type == "real") Check(sAIActionManager->AIActionSetReal(params, key, (AIReal) value.asNumber()), "set real");
				else if (type == "enum") Check(sAIActionManager->AIActionSetEnumerated(params, key, v.str("name").c_str(), (ai::int32) value.asNumber()), "set enum");
				else Fail(kErrInvalidParams, "unknown action parameter type '" + type + "'");
			}
		}
		catch (...) {
			sAIActionManager->AIDeleteActionParamValue(params);
			throw;
		}
	}
	AIErr e = sAIActionManager->PlayActionEvent(event.c_str(), status, params);
	if (params) sAIActionManager->AIDeleteActionParamValue(params);
	Check(e, ("PlayActionEvent " + event).c_str());
	json::Value v;
	v["played"] = event;
	return v;
}

json::Value HistoryUndo(const json::Value& p)
{
	Need(sAIUndo, "The undo suite");
	Check(sAIUndo->MultiUndoTransaction(ActiveDocument(), (ai::int32) p.num("steps", 1)), "Undo");
	return json::Value("undone");
}

json::Value HistoryRedo(const json::Value& p)
{
	Need(sAIUndo, "The undo suite");
	Check(sAIUndo->MultiRedoTransaction(ActiveDocument(), (ai::int32) p.num("steps", 1)), "Redo");
	return json::Value("redone");
}

json::Value Redraw(const json::Value&)
{
	ActiveDocument();
	Check(sAIDocument->RedrawDocument(), "RedrawDocument");
	return json::Value("redrawn");
}

json::Value AppLog(const json::Value& p);

std::map<std::string, Command>& Table()
{
	static std::map<std::string, Command> table = [] {
	CommandTable t = {
		{"app.info", {"Illustrator + Slippy versions, open document count, missing suites.", Params({}), AppInfo, false}},
		{"commands.list", {"Every command with its parameters.", Params({}), [](const json::Value&) { return Describe(); }, false}},
		{"document.list", {"Open documents (index, name, path, active), each once - 'windows' when it has more than one (Window > New Window); 'index' is what document.activate / close take.", Params({}), DocumentList, false}},
		{"document.info", {"The active document: name, path, color model, artboards (with bounds), layer count.", Params({}), DocumentInfo, false}},
		{"document.new", {"New document without a dialog.", Params({{"preset", "string - new-document preset name (optional)"}, {"width", "number - points"},
			{"height", "number - points"}, {"colorMode", "\"rgb\" | \"cmyk\""}, {"title", "string"}, {"artboards", "number"}}), DocumentNew, true}},
		{"document.open", {"Open a file without a dialog.", Params({{"path", "string - absolute path"}}), DocumentOpen, true}},
		{"document.activate", {"Bring a document to the front.", Params({{"index", "number - from document.list"}}), DocumentActivate, false}},
		{"document.save", {"Save; with 'path': native .ai is a Save As (the document moves there), any other 'format' writes a copy.",
			Params({{"path", "string - optional absolute path"}, {"format", "string - a name from document.formats"}}), DocumentSave, false}},
		{"document.export", {"Export a copy: png / jpg rendered at any resolution (artboard, all art, or one object), or pdf, svg, tiff, psd, webp... Format from 'format' or the path's extension.",
			Params({{"path", "string - absolute path"}, {"format", "string - png, jpg, pdf, svg, tiff, psd, webp, eps... (default: the extension)"},
				{"scale", "number - png/jpg size, 1 = 72 dpi (default 1)"}, {"dpi", "number - png/jpg, instead of scale"},
				{"area", "\"artboard\" (default) | \"art\" - png/jpg"}, {"artboard", "number - which artboard (default: active)"},
				{"id", "string - png/jpg: crop to this object (ids: several)"}, {"ids", "string[]"}, {"transparent", "boolean - png (default true)"},
				{"quality", "number - jpg 1-100 (default 90)"}}), DocumentExport, false}},
		{"document.formats", {"File formats Illustrator can save or export, by the names document.save takes.", Params({}), DocumentFormats, false}},
		{"document.close", {"Close a document (default: the active one). Unsaved changes may prompt unless save=true.",
			Params({{"index", "number"}, {"save", "boolean"}}), DocumentClose, true}},
		{"document.redraw", {"Force a redraw.", Params({}), Redraw, false}},
		{"layer.list", {"Layers top to bottom (index, name, visible, locked, current, id of its art group).", Params({}), LayerList, false}},
		{"layer.create", {"New layer: on top, inside 'parent' (a sub-layer), or right above / below another layer.",
			Params({{"name", "string"}, {"parent", "string | number - make it a sub-layer of this layer"}, {"above", "string | number - layer"},
				{"below", "string | number - layer"}, {"current", "boolean - make it current (default true)"}}), LayerCreate, true}},
		{"layer.set", {"Change a layer (name, visibility, lock, current, template, printable, preview, dim images, color) or delete it.",
			Params({{"layer", "string | number - name or index"}, {"name", "string - rename"}, {"visible", "boolean"},
			{"locked", "boolean"}, {"current", "boolean"}, {"template", "boolean"}, {"printable", "boolean"}, {"preview", "boolean - false = outline view"},
			{"dimImages", "boolean"}, {"color", "\"#RRGGBB\" - the layer's selection color"}, {"delete", "boolean"}}), LayerSet, true}},
		{"art.tree", {"The art tree: per layer, or below one art id.", Params({{"depth", "number - levels of children (default 3)"},
			{"layer", "string | number - only this layer"}, {"id", "string - start at this art"}}), ArtTree, false}},
		{"art.get", {"One art object in detail: bounds, style, path segments, text contents, layer, parent.",
			Params({{"id", "string"}, {"depth", "number - levels of children (default 1)"}}), ArtGet, false}},
		{"art.selection", {"The selected art.", Params({{"depth", "number - levels of children (default 0)"}}), ArtSelection, false}},
		{"art.select", {"Select art by id (replaces the selection unless add=true; no ids = deselect all).",
			Params({{"ids", "string[]"}, {"id", "string"}, {"add", "boolean"}}), ArtSelect, false}},
		{"art.set", {"Change art (default: the selection): name, hidden, locked, paint (fill, stroke and stroke detail), opacity, blendMode, text contents/size.",
			Params({{"ids", "string[]"}, {"id", "string"}, {"name", "string"}, {"hidden", "boolean"}, {"locked", "boolean"}, {"fill", kPaint},
				{"stroke", kPaint}, {"strokeWidth", "number"}, {"dash", "number[] - dash and gap lengths ([] = solid)"}, {"dashOffset", "number"},
				{"cap", "butt | round | projecting"}, {"join", "miter | round | bevel"}, {"miterLimit", "number"}, {"strokeAlign", "center | inside | outside"},
				{"fillOverprint", "boolean"}, {"strokeOverprint", "boolean"}, {"evenOdd", "boolean"},
				{"opacity", "number 0-100"}, {"blendMode", "normal | multiply | screen | overlay | ... (see appearance.set)"},
				{"contents", "string - text only"}, {"size", "number - font size, with contents"}}), ArtSet, true}},
		{"art.transform", {"Move / scale / rotate art (default: the selection) about its center or 'origin'.",
			Params({{"ids", "string[]"}, {"id", "string"}, {"translate", "[dx, dy]"}, {"scale", "number | [sx, sy]"}, {"rotate", "number - degrees, counterclockwise"},
				{"origin", "[x, y]"}, {"scaleStrokes", "boolean (default true)"}}), ArtTransform, true}},
		{"art.duplicate", {"Duplicate art (default: the selection) in place.", Params({{"ids", "string[]"}, {"id", "string"}}), ArtDuplicate, true}},
		{"art.arrange", {"Bring to front / send to back within its parent.", Params({{"ids", "string[]"}, {"id", "string"}, {"to", "\"front\" | \"back\""}}), ArtArrange, true}},
		{"art.group", {"Group art (default: the selection).", Params({{"ids", "string[]"}, {"id", "string"}, {"name", "string"}}), ArtGroup, true}},
		{"art.move", {"Move art (default: the selection) into a group - including a clipping group - or right above / below another object. Keeps its stacking order.",
			Params({{"ids", "string[]"}, {"id", "string"}, {"into", "string - group id"}, {"position", "\"top\" | \"bottom\" - inside 'into' (default top)"},
				{"above", "string - art id"}, {"below", "string - art id"}}), ArtMove, true}},
		{"art.ungroup", {"Ungroup (default: the selection); a clipping group is released first. Returns the freed objects.",
			Params({{"ids", "string[]"}, {"id", "string"}}), ArtUngroup, true}},
		{"art.clip", {"Make a clipping mask: 'mask' (default: the front-most object) clips the rest. Returns the clipping group.",
			Params({{"ids", "string[]"}, {"id", "string"}, {"mask", "string - art id of a path, compound path or text"}, {"name", "string"}}), ArtClip, true}},
		{"art.unclip", {"Release a clipping group's mask; the group and the (unpainted) mask path stay.",
			Params({{"ids", "string[]"}, {"id", "string"}}), ArtUnclip, true}},
		{"art.place", {"Place a file (image, PDF, .ai...) without a dialog, linked by default. Put it 'into' a group or 'above'/'below' art, and scale it to 'fitTo' an object.",
			Params({{"path", "string - absolute path"}, {"link", "boolean (default true; false embeds)"}, {"into", "string - group id"},
				{"position", "\"top\" | \"bottom\""}, {"above", "string - art id"}, {"below", "string - art id"}, {"layer", kWhere},
				{"fitTo", "string - art id: scale and center on its bounds"}, {"fit", "\"fill\" (cover, default) | \"fit\" (inside) | \"stretch\""},
				{"name", "string"}, {"select", "boolean"}}), ArtPlace, true}},
		{"art.delete", {"Delete art.", Params({{"ids", "string[]"}, {"id", "string"}}), ArtDelete, true}},
		{"shape.rect", {"Rectangle; (x, y) is its top-left corner.", Params({{"x", "number"}, {"y", "number"}, {"width", "number"}, {"height", "number"},
			{"fill", kPaint}, {"stroke", kPaint}, {"strokeWidth", "number"}, {"name", "string"}, {"layer", kWhere}, {"parent", kWhere}, {"select", "boolean"}}), ShapeRect, true}},
		{"shape.ellipse", {"Ellipse in the box whose top-left is (x, y).", Params({{"x", "number"}, {"y", "number"}, {"width", "number"}, {"height", "number"},
			{"fill", kPaint}, {"stroke", kPaint}, {"strokeWidth", "number"}, {"name", "string"}, {"layer", kWhere}, {"parent", kWhere}, {"select", "boolean"}}), ShapeEllipse, true}},
		{"path.create", {"Path from points: [x,y] corners or {p, in, out, smooth} Bezier anchors.",
			Params({{"points", "array"}, {"closed", "boolean"}, {"fill", kPaint}, {"stroke", kPaint}, {"strokeWidth", "number"}, {"name", "string"},
				{"layer", kWhere}, {"parent", kWhere}, {"select", "boolean"}}), PathCreate, true}},
		{"text.create", {"Point text; 'position' is the first baseline's start.", Params({{"position", "[x, y]"}, {"contents", "string (\\n = new paragraph)"},
			{"size", "number - font size"}, {"name", "string"}, {"layer", kWhere}, {"parent", kWhere}, {"select", "boolean"}}), TextCreate, true}},
		{"menu.run", {"Run any menu command by its name from menu.list (\"outline\", \"selectall\"), as app.executeMenuCommand does, or by its on-screen label. "
			"Acts on the selection; 'changed' says whether anything happened. Prefer the art.* commands for grouping, masks and moving art.",
			Params({{"command", "string"}}), MenuRun, true}},
		{"action.play", {"Play an action event (e.g. \"adobe_paste\") with typed parameters, no dialog by default.",
			Params({{"event", "string"}, {"params", "object - 4-char key -> value | {\"type\":\"integer|real|string|boolean|enum\",\"value\":..,\"name\":..}"},
				{"dialog", "\"off\" | \"on\" | \"none\""}}), ActionPlay, true}},
		{"plugin.message", {"Send another plug-in a script message, as app.sendScriptMessage(plugin, selector, param) does, and return its text reply. "
			"The plug-in decides what the selector does; work it defers finishes after this returns.",
			Params({{"plugin", "string - the plug-in's name, e.g. \"RAGE\""}, {"selector", "string"}, {"param", "string (optional)"}}), PluginMessage, true}},
		{"app.log", {"Slippy's recent calls, newest last: time, method, params, and the error for any that failed (errors=true: failures only).",
			Params({{"lines", "number (default 30)"}, {"errors", "boolean"}}), AppLog, false}},
		{"history.undo", {"Undo steps in the active document.", Params({{"steps", "number (default 1)"}}), HistoryUndo, false}},
		{"history.redo", {"Redo steps in the active document.", Params({{"steps", "number (default 1)"}}), HistoryRedo, false}},
	};
	AddCatalogCommands(t);
	AddSymbolCommands(t);
	AddViewCommands(t);
	AddPaintCommands(t);
	AddAppearanceCommands(t);
	AddShapeCommands(t);
	AddTextCommands(t);
	AddDocumentCommands(t);
	return t;
	}();
	return table;
}

json::Value RunOneUntimed(const json::Value& call)
{
	json::Value response;
	response["jsonrpc"] = "2.0";
	response["id"] = call.get("id");
	auto error = [&](int code, const std::string& message, AIErr aiErr) {
		response["error"]["code"] = code;
		response["error"]["message"] = message;
		if (aiErr) response["error"]["data"]["aiError"] = ErrText(aiErr);
		return response;
	};
	if (!call.isObject() || !call.get("method").isString()) return error(kErrInvalidRequest, "each call needs a 'method' string", kNoErr);
	std::string method = call.get("method").asString();
	auto& table = Table();
	auto it = table.find(method);
	if (it == table.end()) return error(kErrMethodNotFound, "unknown method '" + method + "' - see commands.list", kNoErr);
	const json::Value& params = call.get("params");
	if (!params.isNull() && !params.isObject()) return error(kErrInvalidParams, "'params' must be an object", kNoErr);
	// What crash.log names if this call takes Illustrator down.
	crashlog::SetCurrentCall(method, params.isNull() ? "{}" : params.dump());
	struct Done { ~Done() { crashlog::ClearCurrentCall(); } } done;
	try {
		if (it->second.changesDocument && sAIUndo)
			sAIUndo->SetUndoTextUS(U("Undo Slippy " + method), U("Redo Slippy " + method));
		response["result"] = it->second.run(params.isNull() ? json::Value::MakeObject() : params);
	}
	catch (const CommandError& e) { return error(e.code, e.message, e.aiError); }
	catch (const json::Error& e) { return error(kErrInvalidParams, e.what(), kNoErr); }
	catch (const ai::Error& e) { return error(kErrIllustrator, "Illustrator error " + ErrText((AIErr) e), (AIErr) e); }
	catch (const ATE::Exception& e) { return error(kErrIllustrator, "text engine error " + std::to_string((long) e.error), kNoErr); }
	catch (const std::exception& e) { return error(kErrInternal, e.what(), kNoErr); }
	catch (...) { return error(kErrInternal, "unexpected error", kNoErr); }
	return response;
}

CallObserver gObserver;

// ~/Library/Application Support/Slippy/calls.log (or %APPDATA%): one line per
// call - time, method, params, and the error if it failed - so a failure can
// be read back later (app.log). About 1 MB, then it starts over.
std::string LogPath() { return platform::JoinPath(platform::SupportDir(), "calls.log"); }

void Log(const std::string& method, const json::Value& params, const json::Value& error, double ms, const std::string& agent)
{
	char when[32];
	time_t now = time(nullptr);
	strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
	std::string p = params.isNull() ? "{}" : params.dump();
	if (p.size() > 600) p = p.substr(0, 600) + "...";
	char took[32];
	snprintf(took, sizeof took, "%.0fms", ms);
	std::string line = std::string(when) + "  " + (agent.empty() ? "?" : agent) + "  " + method + "  " + took + "  " + p;
	if (!error.isNull()) line += "  ERROR " + error.dump();
	platform::AppendLine(LogPath(), line, 1 << 20);
}

json::Value AppLog(const json::Value& p)
{
	std::string text = platform::ReadFile(LogPath());
	size_t want = (size_t) std::max(1.0, p.num("lines", 30));
	bool errorsOnly = p.boolean("errors", false);
	std::vector<std::string> lines;
	for (size_t i = 0, j; i < text.size(); i = j + 1) {
		j = text.find('\n', i);
		if (j == std::string::npos) j = text.size();
		std::string l = text.substr(i, j - i);
		if (!l.empty() && (!errorsOnly || l.find("  ERROR ") != std::string::npos)) lines.push_back(l);
	}
	json::Value out = json::Value::MakeArray();
	for (size_t i = lines.size() > want ? lines.size() - want : 0; i < lines.size(); i++) out.push(lines[i]);
	return out;
}

json::Value RunOne(const json::Value& call)
{
	auto start = std::chrono::steady_clock::now();
	overlay::BeginCall();
	json::Value response = RunOneUntimed(call);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::string method = call.isObject() && call.get("method").isString() ? call.get("method").asString() : "(invalid)";
	auto it = Table().find(method);
	const json::Value& err = response.get("error");
	std::string message = err.isNull() ? "" : err.get("message").isString() ? err.get("message").asString() : "error";
	std::string line = Narrate(method, call.get("params"), response.get("result"), message);
	std::string agent = call.isObject() ? call.str("agent", "") : "";
	Log(method, call.get("params"), err, ms, agent);
	if (gObserver) gObserver(method, err.isNull(), it != Table().end() && it->second.changesDocument, ms, line, agent);
	overlay::EndCall(line, err.isNull());
	return response;
}

} // namespace

void SetCallObserver(CallObserver observer) { gObserver = std::move(observer); }

// One command from inside another (no feed line of its own); errors throw.
json::Value RunCommand(const std::string& method, const json::Value& params)
{
	auto it = Table().find(method);
	if (it == Table().end()) Fail(kErrInternal, "no command " + method);
	return it->second.run(params);
}

json::Value Describe()
{
	json::Value list = json::Value::MakeArray();
	for (auto& kv : Table()) {
		json::Value c;
		c["method"] = kv.first;
		c["description"] = kv.second.description;
		c["params"] = kv.second.params;
		c["changesDocument"] = kv.second.changesDocument;
		list.push(c);
	}
	return list;
}

bool WritesFile(const json::Value& call)
{
	if (!call.isObject() || !call.get("method").isString()) return false;
	const std::string& m = call.get("method").asString();
	return m == "document.save" || m == "document.export" || (m == "document.close" && call.get("params").isObject() && call.get("params").boolean("save", false));
}

bool StartsRun(const json::Value& request)
{
	return WritesFile(request.isArray() && request.size() ? request.asArray()[0] : request);
}

bool EndsRun(const json::Value& call)
{
	if (WritesFile(call)) return true;
	if (!call.isObject() || !call.get("method").isString()) return false;
	const std::string& m = call.get("method").asString();
	return m == "document.open" || m == "document.close" || m == "document.new" || m == "document.activate";
}

json::Value Handle(const json::Value& request, json::Value* rest)
{
	if (request.isArray()) {
		// A batch: one run, so the calls land as one undo step - up to a call
		// that opens / closes / switches documents, or around one that writes
		// a file (see Commands.h).
		json::Value out = json::Value::MakeArray();
		bool stopped = false;
		overlay::BeginBatch();   // one highlight around everything the batch touched
		const json::Array& calls = request.asArray();
		for (size_t i = 0; i < calls.size(); i++) {
			const json::Value& call = calls[i];
			if (stopped) {
				json::Value skipped;
				skipped["jsonrpc"] = "2.0";
				skipped["id"] = call.get("id");
				skipped["error"]["code"] = kErrInvalidRequest;
				skipped["error"]["message"] = "skipped: an earlier call in the batch failed";
				out.push(skipped);
				continue;
			}
			if (rest && i > 0 && WritesFile(call)) {   // the write starts a run of its own
				*rest = json::Value::MakeArray();
				for (size_t k = i; k < calls.size(); k++) rest->push(calls[k]);
				break;
			}
			json::Value r = RunOne(call);
			if (r.has("error") && call.boolean("stopOnError", true)) stopped = true;
			out.push(r);
			if (!stopped && rest && EndsRun(call) && i + 1 < calls.size()) {
				*rest = json::Value::MakeArray();
				for (size_t k = i + 1; k < calls.size(); k++) rest->push(calls[k]);
				break;
			}
		}
		overlay::EndBatch();
		return out;
	}
	return RunOne(request);
}

} // namespace slippy
