// The document around the art: artboards (AIArtboard), setup - units, bleed,
// color mode, XMP (AIDocument), the full layer tree, selecting by attributes,
// the clipboard, guides, placed and raster images (AIPlaced, AIRasterize,
// AIVectorize), key-value data on art and documents (AIDictionary),
// preferences (AIPreference), recent files and printing.

#include "Kit.h"
#include "Platform.h"
#include "IAIFilePath.hpp"

#include <algorithm>
#include <cmath>

namespace slippy {

namespace {

// ---- artboards

struct Artboards {
	ai::ArtboardList list;
	Artboards() { Need(sAIArtboard, "The artboard suite"); ActiveDocument(); Check(sAIArtboard->GetArtboardList(list), "GetArtboardList"); }
	~Artboards() { sAIArtboard->ReleaseArtboardList(list); }
	ai::ArtboardID Count() { ai::ArtboardID n = 0; sAIArtboard->GetCount(list, n); return n; }
	ai::ArtboardID Index(const json::Value& p)
	{
		ai::ArtboardID i = 0;
		if (p.get("index").isNumber()) i = (ai::ArtboardID) p.get("index").asInt();
		else sAIArtboard->GetActive(list, i);
		if (i < 0 || i >= Count()) Fail(kErrNotFound, "no artboard " + std::to_string((int) i) + " (there are " + std::to_string((int) Count()) + ", from 0)");
		return i;
	}
	Artboards(const Artboards&) = delete;
	Artboards& operator=(const Artboards&) = delete;
};

struct Props {
	ai::ArtboardProperties p;
	Props() { sAIArtboard->Init(p); }
	~Props() { sAIArtboard->Dispose(p); }
	Props(const Props&) = delete;
	Props& operator=(const Props&) = delete;
};

// x, y (top-left), width, height -> a rect in artwork points.
bool RectParam(const json::Value& p, AIRealRect& r)
{
	if (!p.has("x") && !p.has("y") && !p.has("width") && !p.has("height")) return false;
	double x = ReqNum(p, "x"), y = ReqNum(p, "y"), w = ReqNum(p, "width"), h = ReqNum(p, "height");
	if (w <= 0 || h <= 0) Fail(kErrInvalidParams, "'width' and 'height' must be positive");
	r.left = (AIReal) x; r.top = (AIReal) y; r.right = (AIReal) (x + w); r.bottom = (AIReal) (y - h);
	return true;
}

json::Value ArtboardJson(Artboards& ab, ai::ArtboardID i)
{
	json::Value a;
	Props props;
	if (sAIArtboard->GetArtboardProperties(ab.list, i, props.p)) return a;
	a["index"] = (int) i;
	ai::UnicodeString name;
	sAIArtboard->GetName(props.p, name);
	a["name"] = S(name);
	AIRealRect r;
	if (!sAIArtboard->GetPosition(props.p, r)) a["bounds"] = RectJson(r);
	ai::ArtboardID active = 0;
	sAIArtboard->GetActive(ab.list, active);
	a["active"] = i == active;
	AIBoolean flag = false;
	if (!sAIArtboard->GetLocked(props.p, flag) && flag) a["locked"] = true;
	if (!sAIArtboard->IsSelected(props.p, flag) && flag) a["selected"] = true;
	return a;
}

json::Value ArtboardList(const json::Value&)
{
	Artboards ab;
	json::Value list = json::Value::MakeArray();
	for (ai::ArtboardID i = 0; i < ab.Count(); i++) list.push(ArtboardJson(ab, i));
	return list;
}

json::Value ArtboardAdd(const json::Value& p)
{
	Artboards ab;
	AIRealRect r;
	if (!RectParam(p, r)) Fail(kErrInvalidParams, "pass x, y (top-left), width and height");
	Props props;
	Check(sAIArtboard->SetPosition(props.p, r), "SetPosition");
	if (p.get("name").isString()) sAIArtboard->SetName(props.p, U(p.get("name").asString()));
	ai::ArtboardID index = 0;
	Check(sAIArtboard->AddNew(ab.list, props.p, index), "AddNew");
	if (p.boolean("active", false)) sAIArtboard->SetActive(ab.list, index);
	return ArtboardJson(ab, index);
}

json::Value ArtboardSet(const json::Value& p)
{
	Artboards ab;
	ai::ArtboardID i = ab.Index(p);
	Props props;
	Check(sAIArtboard->GetArtboardProperties(ab.list, i, props.p), "GetArtboardProperties");
	AIRealRect r;
	if (RectParam(p, r)) Check(sAIArtboard->SetPosition(props.p, r), "SetPosition");
	if (p.get("name").isString()) Check(sAIArtboard->SetName(props.p, U(p.get("name").asString())), "SetName");
	if (p.has("locked")) { AIBoolean l = p.boolean("locked", false); sAIArtboard->SetLocked(props.p, l); }
	Check(sAIArtboard->Update(ab.list, i, props.p), "Update");
	if (p.boolean("active", false)) Check(sAIArtboard->SetActive(ab.list, i), "SetActive");
	return ArtboardJson(ab, i);
}

json::Value ArtboardDelete(const json::Value& p)
{
	Artboards ab;
	if (ab.Count() <= 1) Fail(kErrInvalidParams, "a document keeps at least one artboard");
	ai::ArtboardID i = ab.Index(p);
	Check(sAIArtboard->Delete(ab.list, i), "Delete");
	json::Value v;
	v["deleted"] = (int) i;
	return v;
}

AIRealRect UnionBounds(const std::vector<AIArtHandle>& arts, bool& any)
{
	AIRealRect all = {0, 0, 0, 0};
	any = false;
	for (AIArtHandle a : arts) {
		AIRealRect r;
		if (sAIArt->GetArtBounds(a, &r) || r.right <= r.left) continue;
		if (!any) all = r;
		else { all.left = std::min(all.left, r.left); all.right = std::max(all.right, r.right); all.top = std::max(all.top, r.top); all.bottom = std::min(all.bottom, r.bottom); }
		any = true;
	}
	return all;
}

std::vector<AIArtHandle> LayerGroups()
{
	std::vector<AIArtHandle> out;
	ai::int32 n = 0;
	sAILayer->CountLayers(&n);
	for (ai::int32 i = 0; i < n; i++) {
		AILayerHandle layer = nullptr;
		AIArtHandle group = nullptr;
		if (!sAILayer->GetNthLayer(i, &layer) && !sAIArt->GetFirstArtOfLayer(layer, &group) && group) out.push_back(group);
	}
	return out;
}

// Fit an artboard to art (ids / the selection) or, with neither, to all the art.
json::Value ArtboardFit(const json::Value& p)
{
	Artboards ab;
	ai::ArtboardID i = ab.Index(p);
	std::vector<AIArtHandle> arts = p.has("id") || p.has("ids") ? ArtList(p) : p.boolean("selection", false) ? ArtList(p, true) : LayerGroups();
	bool any = false;
	AIRealRect r = UnionBounds(arts, any);
	if (!any) Fail(kErrInvalidParams, "there's no art to fit to");
	double pad = p.num("padding", 0);
	r.left -= (AIReal) pad; r.right += (AIReal) pad; r.top += (AIReal) pad; r.bottom -= (AIReal) pad;
	Props props;
	Check(sAIArtboard->GetArtboardProperties(ab.list, i, props.p), "GetArtboardProperties");
	Check(sAIArtboard->SetPosition(props.p, r), "SetPosition");
	Check(sAIArtboard->Update(ab.list, i, props.p), "Update");
	return ArtboardJson(ab, i);
}

// ---- document settings

ai::int16 UnitsCode(const std::string& u)
{
	if (u == "points") return kPointsUnits;
	if (u == "inches") return kInchesUnits;
	if (u == "millimeters") return kMillimetersUnits;
	if (u == "centimeters") return kCentimetersUnits;
	if (u == "picas") return kPicasUnits;
	if (u == "pixels") return kPixelsUnits;
	if (u == "feet") return kFeetsUnits;
	if (u == "meters") return kMetersUnits;
	if (u == "yards") return kYardsUnits;
	Fail(kErrInvalidParams, "'units' must be points, inches, millimeters, centimeters, picas, pixels, feet, meters or yards");
}

const char* UnitsName(ai::int16 u)
{
	switch (u) {
	case kPointsUnits: return "points";
	case kInchesUnits: return "inches";
	case kMillimetersUnits: return "millimeters";
	case kCentimetersUnits: return "centimeters";
	case kPicasUnits: return "picas";
	case kPixelsUnits: return "pixels";
	case kFeetsUnits: return "feet";
	case kMetersUnits: return "meters";
	case kYardsUnits: return "yards";
	default: return "other";
	}
}

json::Value DocumentSettings(const json::Value& p)
{
	ActiveDocument();
	if (p.has("units")) Check(sAIDocument->SetDocumentRulerUnits(UnitsCode(ReqStr(p, "units"))), "SetDocumentRulerUnits");
	if (p.has("bleed")) {
		const json::Value& b = p.get("bleed");
		AIRealRect r;
		if (b.isNumber()) r.left = r.top = r.right = r.bottom = (AIReal) b.asNumber();
		else if (b.isArray() && b.size() == 4) { r.top = (AIReal) b.asArray()[0].asNumber(); r.right = (AIReal) b.asArray()[1].asNumber(); r.bottom = (AIReal) b.asArray()[2].asNumber(); r.left = (AIReal) b.asArray()[3].asNumber(); }
		else Fail(kErrInvalidParams, "'bleed' must be a number or [top, right, bottom, left] in points");
		Check(sAIDocument->SetDocumentBleeds(r), "SetDocumentBleeds");
	}
	if (p.has("colorMode")) {
		std::string m = ReqStr(p, "colorMode");
		if (m != "rgb" && m != "cmyk") Fail(kErrInvalidParams, "'colorMode' must be rgb or cmyk");
		json::Value call;
		call["command"] = m == "rgb" ? "doc-color-rgb" : "doc-color-cmyk";
		RunCommand("menu.run", call);
	}
	json::Value v;
	ai::int16 units = 0;
	if (!sAIDocument->GetDocumentRulerUnits(&units)) v["units"] = UnitsName(units);
	AIRealRect bleed;
	if (!sAIDocument->GetDocumentBleeds(&bleed)) v["bleed"] = json::Array{(double) bleed.top, (double) bleed.right, (double) bleed.bottom, (double) bleed.left};
	ai::int16 model = 0;
	if (!sAIDocument->GetDocumentColorModel(&model)) v["colorMode"] = model == kDocRGBColor ? "rgb" : model == kDocCMYKColor ? "cmyk" : "other";
	v["note"] = "Slippy's commands always take points; 'units' is what rulers and panels show";
	return v;
}

json::Value DocumentXmp(const json::Value& p)
{
	ActiveDocument();
	if (p.get("xmp").isString()) Check(sAIDocument->SetDocumentXAP(p.get("xmp").asString().c_str()), "SetDocumentXAP");
	const char* xmp = nullptr;
	json::Value v;
	if (!sAIDocument->GetDocumentXAP(&xmp) && xmp) v["xmp"] = xmp;
	return v;
}

json::Value DocumentRecent(const json::Value& p)
{
	if (p.get("open").isNumber()) {
		Check(sAIDocumentList->OpenNthRecentDocument(p.get("open").asInt()), "OpenNthRecentDocument");
		json::Value v;
		v["opened"] = p.get("open").asInt();
		return v;
	}
	json::Value list = json::Value::MakeArray();
	ai::int32 n = sAIDocumentList->CountRecentDocuments();
	for (ai::int32 i = 0; i < n; i++) {
		ai::UnicodeString path;
		if (!sAIDocumentList->GetNthRecentDocument(i, path)) list.push(S(path));
	}
	return list;
}

json::Value DocumentPrint(const json::Value& p)
{
	AIDocumentHandle doc = ActiveDocument();
	Check(sAIDocumentList->Print(doc, p.boolean("dialog", true) ? kDialogOn : kDialogOff), "Print");
	json::Value v;
	v["printed"] = true;
	return v;
}

// ---- layers, all the way down

json::Value LayerNode(AILayerHandle layer, AILayerHandle current)
{
	json::Value l;
	l["name"] = LayerTitle(layer);
	AIBoolean b = false;
	if (!sAILayer->GetLayerVisible(layer, &b) && !b) l["visible"] = false;
	if (!sAILayer->GetLayerEditable(layer, &b) && !b) l["locked"] = true;
	if (!sAILayer->GetLayerIsTemplate(layer, &b) && b) l["template"] = true;
	if (!sAILayer->GetLayerPrinted(layer, &b) && !b) l["printable"] = false;
	if (layer == current) l["current"] = true;
	AIArtHandle group = nullptr;
	if (!sAIArt->GetFirstArtOfLayer(layer, &group) && group) l["id"] = ArtId(group);
	AILayerHandle child = nullptr;
	if (!sAILayer->GetLayerFirstChild(layer, &child) && child) {
		json::Value kids = json::Value::MakeArray();
		for (; child; sAILayer->GetNextLayer(child, &child)) kids.push(LayerNode(child, current));
		l["sublayers"] = kids;
	}
	return l;
}

json::Value LayerTree(const json::Value&)
{
	ActiveDocument();
	AILayerHandle current = nullptr, layer = nullptr;
	sAILayer->GetCurrentLayer(&current);
	json::Value list = json::Value::MakeArray();
	for (sAILayer->GetFirstLayer(&layer); layer; sAILayer->GetNextLayer(layer, &layer)) list.push(LayerNode(layer, current));
	return list;
}

// ---- selection

json::Value SelectMatching(const json::Value& p)
{
	ActiveDocument();
	short type = kAnyArt;
	if (p.has("type")) {
		std::string t = ReqStr(p, "type");
		struct { const char* name; short type; } types[] = {{"path", kPathArt}, {"compoundPath", kCompoundPathArt}, {"group", kGroupArt}, {"text", kTextFrameArt},
			{"placed", kPlacedArt}, {"raster", kRasterArt}, {"symbol", kSymbolArt}, {"mesh", kMeshArt}, {"plugin", kPluginArt}};
		bool found = false;
		for (auto& ty : types) if (t == ty.name) { type = ty.type; found = true; }
		if (!found) Fail(kErrInvalidParams, "'type' must be path, compoundPath, group, text, placed, raster, symbol, mesh or plugin");
	}
	AIMatchingArtSpec spec(type, 0, 0);
	AIArtHandle** matches = nullptr;
	ai::int32 n = 0;
	std::vector<AIArtHandle> all;
	if (!sAIMatchingArt->GetMatchingArt(&spec, 1, &matches, &n) && matches) {
		for (ai::int32 i = 0; i < n; i++) all.push_back((*matches)[i]);
		sSPBlocks->FreeBlock(matches);
	}
	std::string fill = p.has("fill") ? ColorJson(ParseColor(p.get("fill"), "fill")).dump() : "";
	std::string stroke = p.has("stroke") ? ColorJson(ParseColor(p.get("stroke"), "stroke")).dump() : "";
	std::string name = Lower(p.str("name", ""));
	AILayerHandle onLayer = LayerByParam(p);
	std::vector<AIArtHandle> chosen;
	for (AIArtHandle a : all) {
		bool layerGroup = false;
		{ AIBoolean lg = false; sAIArt->IsArtLayerGroup(a, &lg); layerGroup = lg; }
		if (layerGroup || Attr(a, kArtLocked) || Attr(a, kArtHidden)) continue;
		if (onLayer) { AILayerHandle l = nullptr; if (sAIArt->GetLayerOfArt(a, &l) || l != onLayer) continue; }
		if (!name.empty()) {
			ai::UnicodeString n2;
			ASBoolean isDefault = true;
			if (sAIArt->GetArtName(a, n2, &isDefault) || isDefault || Lower(S(n2)).find(name) == std::string::npos) continue;
		}
		if (!fill.empty() || !stroke.empty()) {
			json::Value st = StyleJson(a);
			if (!fill.empty() && st.get("fill").dump() != fill) continue;
			if (!stroke.empty() && st.get("stroke").dump() != stroke) continue;
		}
		chosen.push_back(a);
	}
	if (!p.boolean("add", false)) sAIMatchingArt->DeselectAll();
	for (AIArtHandle a : chosen) sAIArt->SetArtUserAttr(a, kArtSelected, kArtSelected);
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : chosen) out.push(ArtSummary(a, 0));
	return out;
}

json::Value RunMenu(const char* command)
{
	json::Value call;
	call["command"] = command;
	return RunCommand("menu.run", call);
}

json::Value SelectionNow()
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : SelectedArt()) out.push(ArtSummary(a, 0));
	return out;
}

json::Value SelectSame(const json::Value& p)
{
	static const std::pair<const char*, const char*> kinds[] = {
		{"fill", "Find Fill Color menu item"}, {"stroke", "Find Stroke Color menu item"}, {"fillAndStroke", "Find Fill & Stroke menu item"},
		{"strokeWeight", "Find Stroke Weight menu item"}, {"opacity", "Find Opacity menu item"}, {"blendMode", "Find Blending Mode menu item"},
		{"appearance", "Find Appearance menu item"}, {"graphicStyle", "Find Style menu item"}, {"symbol", "Find Symbol Instance menu item"},
		{"textFill", "Find Text Fill Color menu item"}};
	std::string what = ReqStr(p, "what");
	const char* command = nullptr;
	for (auto& k : kinds) if (what == k.first) command = k.second;
	if (!command) Fail(kErrInvalidParams, "'what' must be fill, stroke, fillAndStroke, strokeWeight, opacity, blendMode, appearance, graphicStyle, symbol or textFill");
	SelectOnly({ArtById(IdText(Required(p, "id")))});
	RunMenu(command);
	return SelectionNow();
}

// ---- clipboard

json::Value Clipboard(const json::Value& p, const char* command, bool selects)
{
	if (selects) SelectOnly(ArtList(p, true));
	else ActiveDocument();
	RunMenu(command);
	return SelectionNow();
}

// ---- guides

json::Value GuideCreate(const json::Value& p)
{
	Need(sAIPath, "The path suite");
	ai::int16 order;
	AIArtHandle prep;
	Placement(p, order, prep);
	std::string o = ReqStr(p, "orientation");
	if (o != "horizontal" && o != "vertical") Fail(kErrInvalidParams, "'orientation' must be horizontal or vertical");
	double at = ReqNum(p, "position");
	const double reach = 16000;   // across the whole canvas
	AIPathSegment segs[2];
	AIRealPoint a = o == "horizontal" ? AIRealPoint{(AIReal) -reach, (AIReal) at} : AIRealPoint{(AIReal) at, (AIReal) reach};
	AIRealPoint b = o == "horizontal" ? AIRealPoint{(AIReal) reach, (AIReal) at} : AIRealPoint{(AIReal) at, (AIReal) -reach};
	segs[0].p = segs[0].in = segs[0].out = a; segs[0].corner = true;
	segs[1].p = segs[1].in = segs[1].out = b; segs[1].corner = true;
	AIArtHandle guide = nullptr;
	Check(sAIArt->NewArt(kPathArt, order, prep, &guide), "NewArt");
	sAIPath->SetPathSegmentCount(guide, 2);
	sAIPath->SetPathSegments(guide, 0, 2, segs);
	Check(sAIPath->SetPathGuide(guide, true), "SetPathGuide");
	return ArtSummary(guide, 0);
}

json::Value GuideList(const json::Value&)
{
	ActiveDocument();
	AIMatchingArtSpec spec(kPathArt, 0, 0);
	AIArtHandle** matches = nullptr;
	ai::int32 n = 0;
	json::Value list = json::Value::MakeArray();
	if (!sAIMatchingArt->GetMatchingArt(&spec, 1, &matches, &n) && matches) {
		for (ai::int32 i = 0; i < n; i++) {
			AIArtHandle a = (*matches)[i];
			AIBoolean guide = false;
			if (sAIPath->GetPathGuide(a, &guide) || !guide) continue;
			list.push(ArtSummary(a, 0));
		}
		sSPBlocks->FreeBlock(matches);
	}
	return list;
}

json::Value GuideSet(const json::Value& p, bool guide)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		if (ArtType(a) != kPathArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't a path");
		Check(sAIPath->SetPathGuide(a, guide), "SetPathGuide");
		out.push(ArtSummary(a, 0));
	}
	return out;
}

// ---- images

json::Value ImageInfo(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		json::Value v = ArtSummary(a, 0);
		short type = ArtType(a);
		if (type != kPlacedArt && type != kRasterArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't an image");
		v["linked"] = type == kPlacedArt;
		if (sAIVectorize) {
			AIReal dpi = 0;
			if (!sAIVectorize->GetEffectiveResolution(a, &dpi) && dpi > 0) v["effectiveDpi"] = (double) dpi;
		}
		out.push(v);
	}
	return out;
}

json::Value ImageEmbed(const json::Value& p)
{
	Need(sAIPlaced, "The placed art suite");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		if (ArtType(a) != kPlacedArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't a linked file");
		AIArtHandle native = nullptr;
		Check(sAIPlaced->MakePlacedObjectNative(a, &native, false), "MakePlacedObjectNative");
		if (native) out.push(ArtSummary(native, 1));
	}
	return out;
}

json::Value ImageRelink(const json::Value& p)
{
	Need(sAIPlaced, "The placed art suite");
	AIArtHandle a = ArtById(IdText(Required(p, "id")));
	if (ArtType(a) != kPlacedArt) Fail(kErrInvalidParams, "that isn't a linked file");
	std::string path = ReqStr(p, "path");
	if (!platform::Readable(path)) Fail(kErrNotFound, "can't read " + path);
	Check(sAIPlaced->SetPlacedFileSpecification(a, ai::FilePath(U(path))), "SetPlacedFileSpecification");
	return ArtSummary(a, 0);
}

// Object > Rasterize: art becomes an image (the art goes, unless keepOriginal).
json::Value ArtRasterize(const json::Value& p)
{
	Need(sAIRasterize, "The rasterize suite");
	Need(sAIArtSet, "The art set suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIArtSet set = nullptr;
	Check(sAIArtSet->NewArtSet(&set), "NewArtSet");
	for (AIArtHandle a : arts) sAIArtSet->AddArtToArtSet(set, a);
	std::string model = p.str("colorModel", "rgb");
	bool transparent = p.str("background", "transparent") == "transparent";
	AIRasterizeSettings settings;
	settings.type = model == "cmyk" ? (transparent ? kRasterizeACMYK : kRasterizeCMYK) : model == "gray" ? (transparent ? kRasterizeAGrayscale : kRasterizeGrayscale)
		: (transparent ? kRasterizeARGB : kRasterizeRGB);
	settings.resolution = (AIReal) p.num("resolution", 150);
	settings.antialiasing = p.boolean("antialias", true) ? 2 : 1;   // 1 = none, >1 = supersampling
	AIRealRect bounds;
	AIErr e = sAIRasterize->ComputeArtBounds(set, &bounds, false);
	AIArtHandle raster = nullptr;
	if (!e) e = sAIRasterize->RasterizeWithPadding(set, &settings, &bounds, kPlaceAbove, arts.front(), &raster, nullptr, (AIReal) p.num("padding", 0));
	sAIArtSet->DisposeArtSet(&set);
	Check(e, "Rasterize");
	if (!p.boolean("keepOriginal", false)) for (AIArtHandle a : arts) sAIArt->DisposeArt(a);
	return raster ? ArtSummary(raster, 0) : json::Value();
}

// Image Trace: the default settings, or the Image Trace panel's main options
// (mode, threshold, colors, fidelities, ignore white); expand=true (default)
// leaves plain paths.
json::Value ImageTrace(const json::Value& p)
{
	Need(sAIVectorize, "The image trace suite");
	AIArtHandle image = ArtById(IdText(Required(p, "id")));
	AIArtHandle tracing = nullptr;
	Check(sAIVectorize->CreateTracing(kPlaceAbove, image, image, &tracing), "CreateTracing");
	std::string mode = p.str("mode", "");
	bool custom = !mode.empty() || p.has("threshold") || p.has("colors") || p.has("ignoreWhite") || p.has("paths") || p.has("corners") || p.has("noise");
	if (custom && sAIDictionary) {
		AIDictionaryRef options = nullptr;
		Check(sAIVectorize->AcquireTracingOptions(tracing, &options), "AcquireTracingOptions");
		auto key = [](const char* k) { return sAIDictionary->Key(k); };
		AIErr e = kNoErr;
		if (mode == "bw" || mode == "blackAndWhite" || mode == "blackandwhite") e = sAIDictionary->SetIntegerEntry(options, key(kTracingModeKey), kAIVectorizeModeBlackAndWhite);
		else if (mode == "gray" || mode == "grayscale") e = sAIDictionary->SetIntegerEntry(options, key(kTracingModeKey), kAIVectorizeModeGray);
		else if (mode == "color") e = sAIDictionary->SetIntegerEntry(options, key(kTracingModeKey), kAIVectorizeModeColor);
		if (!e && p.has("threshold")) e = sAIDictionary->SetIntegerEntry(options, key(kTracingThresholdKey), (ai::int32) p.num("threshold", 128));
		if (!e && p.has("colors")) {
			e = sAIDictionary->SetIntegerEntry(options, key(kTracingTypeColorKey), kAILimitedTracingColors);
			if (!e) e = sAIDictionary->SetIntegerEntry(options, key(kTracingLimitedColorsKey), (ai::int32) p.num("colors", 6));
		}
		if (!e && p.has("paths")) e = sAIDictionary->SetRealEntry(options, key(kTracingPathFidelityKey), (AIReal) p.num("paths", 50));
		if (!e && p.has("corners")) e = sAIDictionary->SetRealEntry(options, key(kTracingCornerFidelityKey), (AIReal) p.num("corners", 50));
		if (!e && p.has("noise")) e = sAIDictionary->SetIntegerEntry(options, key(kTracingNoiseFidelityKey), (ai::int32) p.num("noise", 50));
		if (!e && p.has("ignoreWhite")) e = sAIDictionary->SetBooleanEntry(options, key(kTracingIgnoreWhiteKey), p.boolean("ignoreWhite", false));
		// Abutting cuts the holes into the shapes, so dropping the white ones
		// below leaves counters open instead of filled.
		if (!e && p.boolean("ignoreWhite", false)) e = sAIDictionary->SetIntegerEntry(options, key(kTracingOverlappingOrAbuttingKey), kAIAbutting);
		sAIDictionary->Release(options);
		if (e) sAIArt->DisposeArt(tracing);
		Check(e, "Tracing options");
	}
	Check(sAIVectorize->Update(tracing), "Update tracing");
	if (!p.boolean("expand", true)) return ArtSummary(tracing, 0);
	AIArtHandle art = nullptr;
	Check(sAIVectorize->CopyTracingArt(tracing, kPlaceAbove, tracing, &art, false), "CopyTracingArt");
	sAIArt->DisposeArt(tracing);
	// The engine doesn't always honor ignore white; drop any white shapes left.
	if (art && p.boolean("ignoreWhite", false) && sAIPathStyle) {
		std::vector<AIArtHandle> white;
		AIArtHandle c = nullptr;
		for (sAIArt->GetArtFirstChild(art, &c); c; sAIArt->GetArtSibling(c, &c)) {
			AIPathStyle style;
			if (sAIPathStyle->GetPathStyle(c, &style, nullptr) || !style.fillPaint) continue;
			const AIColor& f = style.fill.color;
			bool isWhite = (f.kind == kGrayColor && f.c.g.gray <= 0.002)
				|| (f.kind == kFourColor && f.c.f.cyan + f.c.f.magenta + f.c.f.yellow + f.c.f.black <= 0.008)
				|| (f.kind == kThreeColor && f.c.rgb.red >= 0.998 && f.c.rgb.green >= 0.998 && f.c.rgb.blue >= 0.998);
			if (isWhite) white.push_back(c);
		}
		for (AIArtHandle w : white) sAIArt->DisposeArt(w);
	}
	return art ? ArtSummary(art, 1) : json::Value();
}

// ---- data on art and documents

json::Value DataOn(const json::Value& p)
{
	Need(sAIDictionary, "The dictionary suite");
	AIDictionaryRef dict = nullptr;
	if (IsId(p.get("id"))) Check(sAIArt->GetDictionary(ArtById(IdText(p.get("id"))), &dict), "GetDictionary");
	else { ActiveDocument(); Check(sAIDocument->GetDictionary(&dict), "GetDictionary"); }
	json::Value v;
	try {
		if (p.has("set")) JsonIntoDict(p.get("set"), dict);
		if (p.get("delete").isArray())
			for (const json::Value& k : p.get("delete").asArray()) sAIDictionary->DeleteEntry(dict, sAIDictionary->Key(k.asString().c_str()));
		v = DictJson(dict);
	}
	catch (...) { sAIDictionary->Release(dict); throw; }
	sAIDictionary->Release(dict);
	return v;
}

// ---- preferences

json::Value Preference(const json::Value& p)
{
	Need(sAIPreference, "The preference suite");
	std::string prefix = ReqStr(p, "prefix"), suffix = p.str("suffix", "");
	std::string type = p.str("type", "");
	const json::Value& value = p.get("value");
	if (!value.isNull()) {
		if (type.empty()) type = value.isBool() ? "boolean" : value.isString() ? "string" : value.asNumber() == std::floor(value.asNumber()) ? "integer" : "real";
		if (type == "boolean") Check(sAIPreference->PutBooleanPreference(prefix.c_str(), suffix.c_str(), value.asBool()), "PutBooleanPreference");
		else if (type == "integer") Check(sAIPreference->PutIntegerPreference(prefix.c_str(), suffix.c_str(), (ai::int32) value.asNumber()), "PutIntegerPreference");
		else if (type == "real") Check(sAIPreference->PutRealPreference(prefix.c_str(), suffix.c_str(), value.asNumber()), "PutRealPreference");
		else if (type == "string") Check(sAIPreference->PutUnicodeStringPreference(prefix.c_str(), suffix.c_str(), U(value.asString())), "PutUnicodeStringPreference");
		else Fail(kErrInvalidParams, "'type' must be boolean, integer, real or string");
	}
	if (type.empty()) Fail(kErrInvalidParams, "reading needs 'type' (boolean, integer, real or string)");
	json::Value v;
	v["prefix"] = prefix;
	v["suffix"] = suffix;
	if (type == "boolean") { AIBoolean b = false; Check(sAIPreference->GetBooleanPreference(prefix.c_str(), suffix.c_str(), &b), "GetBooleanPreference"); v["value"] = (bool) b; }
	else if (type == "integer") { ai::int32 i = 0; Check(sAIPreference->GetIntegerPreference(prefix.c_str(), suffix.c_str(), &i), "GetIntegerPreference"); v["value"] = i; }
	else if (type == "real") { double d = 0; Check(sAIPreference->GetRealPreference(prefix.c_str(), suffix.c_str(), &d), "GetRealPreference"); v["value"] = d; }
	else if (type == "string") { ai::UnicodeString s; Check(sAIPreference->GetUnicodeStringPreference(prefix.c_str(), suffix.c_str(), s), "GetUnicodeStringPreference"); v["value"] = S(s); }
	else Fail(kErrInvalidParams, "'type' must be boolean, integer, real or string");
	return v;
}

} // namespace

void AddDocumentCommands(CommandTable& t)
{
	const char* ids = "string[] - art ids (default: the selection)";
	const char* rect = "number - x, y (top-left), width, height, in points";
	t["artboard.list"] = {"Artboards: index, name, bounds, active, locked, selected.", Params({}), ArtboardList, false};
	t["artboard.add"] = {"New artboard at x, y (top-left) with width and height.",
		Params({{"x", rect}, {"y", "number"}, {"width", "number"}, {"height", "number"}, {"name", "string"}, {"active", "boolean"}}), ArtboardAdd, true};
	t["artboard.set"] = {"Change an artboard (default: the active one): name, x / y / width / height, locked, or make it active.",
		Params({{"index", "number"}, {"name", "string"}, {"x", rect}, {"y", "number"}, {"width", "number"}, {"height", "number"}, {"locked", "boolean"}, {"active", "boolean"}}), ArtboardSet, true};
	t["artboard.delete"] = {"Delete an artboard (the art stays).", Params({{"index", "number"}}), ArtboardDelete, true};
	t["artboard.fit"] = {"Fit an artboard (default: the active one) to art: ids, the selection (selection=true), or all the art; optional padding.",
		Params({{"index", "number"}, {"ids", "string[]"}, {"id", "string"}, {"selection", "boolean"}, {"padding", "number"}}), ArtboardFit, true};
	t["document.settings"] = {"Read or change document settings: ruler units, bleed, color mode (rgb / cmyk).",
		Params({{"units", "points | inches | millimeters | centimeters | picas | pixels | feet | meters | yards"}, {"bleed", "number | [top, right, bottom, left]"},
			{"colorMode", "rgb | cmyk"}}), DocumentSettings, true};
	t["document.xmp"] = {"The document's XMP metadata; pass 'xmp' to replace it.", Params({{"xmp", "string - XMP packet (optional)"}}), DocumentXmp, true};
	t["document.recent"] = {"Recent files; 'open' opens one by index.", Params({{"open", "number (optional)"}}), DocumentRecent, false};
	t["document.print"] = {"Print the active document with its print settings. dialog=true (default) shows the Print dialog so a person confirms.",
		Params({{"dialog", "boolean (default true)"}}), DocumentPrint, false};
	t["layer.tree"] = {"Every layer and sub-layer, nested, with visibility, lock, template, printable and the id of its art group.", Params({}), LayerTree, false};
	t["select.matching"] = {"Select art by attributes: type, fill, stroke, name (contains), layer. Replaces the selection unless add=true. Locked and hidden art is skipped.",
		Params({{"type", "path | compoundPath | group | text | placed | raster | symbol | mesh | plugin"}, {"fill", kPaint}, {"stroke", kPaint},
			{"name", "string - part of the name"}, {"layer", "string | number"}, {"add", "boolean"}}), SelectMatching, false};
	t["select.same"] = {"Select > Same: everything sharing an attribute with art 'id'.",
		Params({{"id", "string"}, {"what", "fill | stroke | fillAndStroke | strokeWeight | opacity | blendMode | appearance | graphicStyle | symbol | textFill"}}), SelectSame, false};
	t["select.all"] = {"Select > All (onArtboard=true: all on the active artboard).", Params({{"onArtboard", "boolean"}}),
		[](const json::Value& p) { ActiveDocument(); RunMenu(p.boolean("onArtboard", false) ? "selectallinartboard" : "selectall"); return SelectionNow(); }, false};
	t["select.none"] = {"Select > Deselect.", Params({}), [](const json::Value&) { ActiveDocument(); sAIMatchingArt->DeselectAll(); return SelectionNow(); }, false};
	t["select.inverse"] = {"Select > Inverse.", Params({}), [](const json::Value&) { ActiveDocument(); RunMenu("Inverse menu item"); return SelectionNow(); }, false};
	t["edit.copy"] = {"Copy art (default: the selection) to the clipboard.", Params({{"ids", ids}}), [](const json::Value& p) { return Clipboard(p, "copy", true); }, false};
	t["edit.cut"] = {"Cut art (default: the selection).", Params({{"ids", ids}}), [](const json::Value& p) { return Clipboard(p, "cut", true); }, true};
	t["edit.paste"] = {"Paste: where = center (default), front, back, inPlace or allArtboards. Returns what was pasted.",
		Params({{"where", "center | front | back | inPlace | allArtboards"}}), [](const json::Value& p) {
			std::string w = p.str("where", "center");
			const char* c = w == "front" ? "pasteFront" : w == "back" ? "pasteBack" : w == "inPlace" ? "pasteInPlace" : w == "allArtboards" ? "pasteInAllArtboard" : "paste";
			return Clipboard(p, c, false); }, true};
	t["guide.create"] = {"A ruler guide: horizontal at y = position, or vertical at x = position.",
		Params({{"orientation", "horizontal | vertical"}, {"position", "number - points"}, {"layer", kWhere}}), GuideCreate, true};
	t["guide.list"] = {"Every guide (as art ids; paths made into guides included).", Params({}), GuideList, false};
	t["guide.make"] = {"View > Guides > Make Guides: paths (default: the selection) become guides.", Params({{"ids", ids}}), [](const json::Value& p) { return GuideSet(p, true); }, true};
	t["guide.release"] = {"View > Guides > Release Guides: guides go back to paths.", Params({{"ids", "string[]"}}), [](const json::Value& p) { return GuideSet(p, false); }, true};
	t["guide.clear"] = {"View > Guides > Clear Guides: delete every guide.", Params({}), [](const json::Value&) { ActiveDocument(); RunMenu("clearguide"); json::Value v; v["cleared"] = true; return v; }, true};
	t["image.info"] = {"Images (default: the selection): linked or embedded, file, effective resolution.", Params({{"ids", ids}}), ImageInfo, false};
	t["image.embed"] = {"Embed linked files (default: the selection).", Params({{"ids", ids}}), ImageEmbed, true};
	t["image.relink"] = {"Point a linked image at another file.", Params({{"id", "string"}, {"path", "string - absolute path"}}), ImageRelink, true};
	t["image.trace"] = {"Image Trace an image: the default settings, or mode / threshold / colors / fidelities / ignoreWhite as in the Image Trace panel; expand=true (default) leaves plain paths. The image is used up by the trace - art.duplicate it first (or art.place it again) to keep a copy.",
		Params({{"id", "string"}, {"mode", "color | gray | bw"}, {"threshold", "number 0-255 - bw"}, {"colors", "number - color: limited palette size"},
			{"paths", "number 0-100 - path fidelity"}, {"corners", "number 0-100"}, {"noise", "number - px"}, {"ignoreWhite", "boolean"}, {"expand", "boolean (default true)"}}), ImageTrace, true};
	t["art.rasterize"] = {"Object > Rasterize: art (default: the selection) becomes an image.",
		Params({{"ids", ids}, {"resolution", "number - ppi (default 150)"}, {"colorModel", "rgb (default) | cmyk | gray"}, {"background", "transparent (default) | white"},
			{"antialias", "boolean (default true)"}, {"padding", "number - points"}, {"keepOriginal", "boolean"}}), ArtRasterize, true};
	t["art.transformAgain"] = {"Object > Transform > Transform Again on art (default: the selection).", Params({{"ids", ids}}),
		[](const json::Value& p) { SelectOnly(ArtList(p, true)); RunMenu("transformagain"); return SelectionNow(); }, true};
	t["data"] = {"Key-value data kept in the file: on art 'id', or on the document. 'set' adds / changes keys, 'delete' removes them; returns all of it.",
		Params({{"id", "string - art (default: the document)"}, {"set", "object"}, {"delete", "string[] - keys"}}), DataOn, true};
	t["preference"] = {"Read or write an Illustrator preference by prefix / suffix (as in the Prefs file); pass 'value' to write.",
		Params({{"prefix", "string"}, {"suffix", "string"}, {"type", "boolean | integer | real | string"}, {"value", "any (optional)"}}), Preference, false};
}

} // namespace slippy
