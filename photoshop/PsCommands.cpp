// Slippy for Photoshop's command table: every method agents can call, run on
// Photoshop's main thread with native action descriptors (no JavaScript).
// Implements Commands.h, so the server, MCP and the panel are the same code
// Slippy for Illustrator uses.
//
// History: an edit is one History step, named after what it did ("Slippy:
// Renamed a layer to “Logo”"); a batch's edits in a row are one step too.
// Reads never touch History; opening, saving, exporting and closing run on
// their own, as Photoshop records them.

#include "Commands.h"
#include "CrashLog.h"
#include "Narrate.h"
#include "Platform.h"
#include "PsDescriptor.h"
#include "Version.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <vector>

namespace slippy {

using namespace ps;
using json::Value;

namespace {

enum class Kind {
	Read,    // looks, changes nothing
	Edit,    // changes the document: one History step (several, in a batch, share one)
	Plain,   // documents, files, undo: Photoshop records these itself
};

struct Command {
	std::string description;
	Value params;   // name -> "type - meaning"
	std::function<Value(const Value&)> run;
	Kind kind;
};
using CommandTable = std::map<std::string, Command>;

Value Params(std::initializer_list<std::pair<const char*, const char*>> list)
{
	Value p = Value::MakeObject();
	for (auto& kv : list) p[kv.first] = kv.second;
	return p;
}

Value Arr(std::initializer_list<Value> items) { return Value(json::Array(items)); }

Value Prop(const char* property, const Value& ref)
{
	Value p;
	p["_property"] = property;
	return Arr({p, ref});
}

double Required(const Value& p, const char* key)
{
	if (!p.get(key).isNumber()) Fail(kErrInvalidParams, std::string("'") + key + "' (a number) is required");
	return p.get(key).asNumber();
}

// ---------------------------------------------------------------- documents

int DocumentCount()
{
	Value app;
	app["_ref"] = "application";
	app["_enum"] = "ordinal";
	app["_value"] = "targetEnum";
	return (int) Get(Prop("numberOfDocuments", app), "count the documents").num("numberOfDocuments");
}

int ActiveDocumentId()
{
	if (DocumentCount() == 0) Fail(kErrUnavailable, "No document is open. Open or make one first (document.open / document.new).");
	return (int) Get(Prop("documentID", RefTarget("document")), "find the active document").num("documentID");
}

// A distance in pixels: Photoshop reports document sizes in points.
double Pixels(const Value& v, double resolution)
{
	if (v.isNumber()) return v.asNumber();
	double n = v.num("_value");
	return v.str("_unit") == "distanceUnit" ? n * resolution / 72.0 : n;
}

Value DocumentSummary(const Value& d, int activeId)
{
	double res = d.get("resolution").isObject() ? d.get("resolution").num("_value", 72) : 72;
	Value s;
	s["id"] = d.get("documentID");
	s["name"] = d.get("title");
	s["path"] = d.get("fileReference").isObject() ? d.get("fileReference").get("_path") : Value();
	s["width"] = std::round(Pixels(d.get("width"), res));
	s["height"] = std::round(Pixels(d.get("height"), res));
	s["resolution"] = res;
	std::string mode = d.get("mode").isObject() ? d.get("mode").str("_class", d.get("mode").str("_value")) : "";
	for (const char* suffix : {"ColorMode", "Color", "Mode"})   // RGBColor -> RGB, grayscaleMode -> grayscale
		if (mode.size() > strlen(suffix) && mode.compare(mode.size() - strlen(suffix), strlen(suffix), suffix) == 0) { mode.resize(mode.size() - strlen(suffix)); break; }
	s["mode"] = mode;
	s["bitsPerChannel"] = d.get("depth");
	s["layerCount"] = d.get("numberOfLayers");
	s["active"] = d.get("documentID").isNumber() && d.get("documentID").asInt() == activeId;
	return s;
}

Value DocumentById(int id) { return Get(Arr({Ref("document", id)}), "read document " + std::to_string(id)); }

int FindDocumentId(const Value& p)
{
	if (p.get("id").isNumber()) return p.get("id").asInt();
	if (p.get("name").isString()) {
		int n = DocumentCount();
		for (int i = 1; i <= n; i++) {
			Value r;
			r["_ref"] = "document";
			r["_index"] = i;
			Value d = Get(Arr({r}), "read the documents");
			if (d.str("title") == p.str("name")) return d.get("documentID").asInt();
		}
		Fail(kErrNotFound, "No open document named " + p.str("name") + " (document.list shows them)");
	}
	return ActiveDocumentId();
}

// ---------------------------------------------------------------- layers

const char* KindName(int k)
{
	static const char* names[] = {"any", "pixel", "adjustment", "text", "shape", "smartObject", "video", "group", "3D",
		"gradientFill", "patternFill", "solidColorFill", "background", "groupEnd"};
	return k >= 0 && k < (int) (sizeof names / sizeof *names) ? names[k] : "other";
}

Value Bounds(const Value& b)
{
	if (!b.isObject()) return Value();
	Value out;
	double l = b.get("left").num("_value"), t = b.get("top").num("_value"), r = b.get("right").num("_value"), btm = b.get("bottom").num("_value");
	out["left"] = l;
	out["top"] = t;
	out["right"] = r;
	out["bottom"] = btm;
	out["width"] = r - l;
	out["height"] = btm - t;
	return out;
}

Value LayerSummary(const Value& l)
{
	Value s;
	s["id"] = l.get("layerID");
	s["name"] = l.get("name");
	int kind = l.get("layerKind").isNumber() ? l.get("layerKind").asInt() : 0;
	if (l.boolean("background")) kind = 12;
	s["kind"] = KindName(kind);
	s["visible"] = l.get("visible");
	s["opacity"] = l.get("opacity").isNumber() ? std::round(l.get("opacity").asNumber() / 2.55) : 100;
	s["blendMode"] = l.get("mode").isObject() ? l.get("mode").get("_value") : Value("normal");
	s["locked"] = l.get("layerLocking").isObject() && l.get("layerLocking").boolean("protectAll");
	s["bounds"] = Bounds(l.get("bounds"));
	s["index"] = l.get("itemIndex");
	return s;
}

Value LayerById(int id)
{
	try {
		return Get(Arr({Ref("layer", id)}), "read layer " + std::to_string(id));
	}
	catch (const PsError&) {
		Fail(kErrNotFound, "no layer with id " + std::to_string(id) + " in the active document (layer.tree lists them)");
	}
}

std::vector<int> ActiveLayerIds()
{
	Value d = Get(Prop("targetLayersIDs", RefTarget("document")), "read the active layers");
	std::vector<int> ids;
	const Value& list = d.get("targetLayersIDs");
	if (list.isArray()) for (const Value& ref : list.asArray()) {
		const Value& first = ref.isArray() && ref.size() ? ref.asArray()[0] : ref;
		if (first.get("_id").isNumber()) ids.push_back(first.get("_id").asInt());
	}
	return ids;
}

// The layer 'id' names, or the active one.
int LayerId(const Value& p)
{
	ActiveDocumentId();
	if (p.get("id").isNumber()) { LayerById(p.get("id").asInt()); return p.get("id").asInt(); }
	std::vector<int> ids = ActiveLayerIds();
	if (ids.empty()) Fail(kErrInvalidParams, "No layer given and none is active. Pass 'id' (layer.tree lists them).");
	return ids[0];
}

void SelectLayer(int id, bool add = false)
{
	Value d;
	d["_obj"] = "select";
	d["_target"] = Arr({Ref("layer", id)});
	if (add) d["selectionModifier"] = Enum("selectionModifierType", "addToSelection");
	d["makeVisible"] = false;
	Play(d, "select layer " + std::to_string(id));
}

// Every layer of the active document, top to bottom, in its groups.
Value LayerTree(int docId, int depthLimit)
{
	Value doc = DocumentById(docId);
	int count = doc.get("numberOfLayers").asInt();
	bool background = doc.boolean("hasBackgroundLayer");
	Value root = Value::MakeArray();
	std::vector<Value*> stack{&root};
	std::vector<Value> groups;   // open groups' summaries, built as we go
	groups.reserve(64);
	int depth = 0;
	for (int i = count; i >= (background ? 0 : 1); i--) {
		Value r;
		r["_ref"] = "layer";
		r["_index"] = i;
		Value l = Get(Arr({r, Ref("document", docId)}), "read the layers");
		std::string section = l.get("layerSection").isObject() ? l.get("layerSection").str("_value") : "layerSectionContent";
		if (section == "layerSectionEnd") {   // the end of a group: close it
			if (stack.size() > 1) { stack.pop_back(); depth--; }
			continue;
		}
		Value s = LayerSummary(l);
		if (section == "layerSectionStart") {
			s["children"] = Value::MakeArray();
			stack.back()->push(s);
			Value& added = stack.back()->asArray().back();
			stack.push_back(&added["children"]);
			depth++;
			continue;
		}
		if (depthLimit < 0 || depth <= depthLimit) stack.back()->push(s);
	}
	return root;
}

// Blend modes by any spelling: "soft light", "softLight", "SOFT_LIGHT".
std::string BlendMode(const std::string& name)
{
	static const char* modes[] = {"normal", "dissolve", "darken", "multiply", "colorBurn", "linearBurn", "darkerColor", "lighten",
		"screen", "colorDodge", "linearDodge", "lighterColor", "overlay", "softLight", "hardLight", "vividLight", "linearLight",
		"pinLight", "hardMix", "difference", "exclusion", "blendSubtraction", "blendDivide", "hue", "saturation", "color",
		"luminosity", "passThrough"};
	auto squash = [](std::string s) {
		std::string out;
		for (char c : s) if (c != ' ' && c != '_' && c != '-') out += (char) tolower((unsigned char) c);
		return out;
	};
	std::string want = squash(name);
	if (want == "subtract") want = "blendsubtraction";
	if (want == "divide") want = "blenddivide";
	for (const char* m : modes) if (squash(m) == want) return m;
	Fail(kErrInvalidParams, "unknown blend mode '" + name + "' - e.g. normal, multiply, screen, overlay, softLight, darken, lighten, difference");
}

Value RgbColor(const Value& v)
{
	std::string hex = v.isString() ? v.asString() : "#000000";
	if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
	if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
		Fail(kErrInvalidParams, "colors are \"#RRGGBB\"");
	long n = strtol(hex.c_str(), nullptr, 16);
	// Whole numbers would go in as integers; Photoshop wants doubles here.
	auto channel = [](long v) { Value f; f["_float"] = (double) v; return f; };
	Value c;
	c["_obj"] = "RGBColor";
	c["red"] = channel((n >> 16) & 255);
	c["grain"] = channel((n >> 8) & 255);
	c["blue"] = channel(n & 255);
	return c;
}

Value Rect(const Value& p)
{
	Value r;
	r["_obj"] = "rectangle";
	r["top"] = Unit("pixelsUnit", Required(p, "top"));
	r["left"] = Unit("pixelsUnit", Required(p, "left"));
	r["bottom"] = Unit("pixelsUnit", Required(p, "bottom"));
	r["right"] = Unit("pixelsUnit", Required(p, "right"));
	return r;
}

Value SelectionRef()
{
	Value s;
	s["_ref"] = "channel";
	s["_property"] = "selection";
	return Arr({s});
}

// ---------------------------------------------------------------- perspective

using Pt = std::pair<double, double>;

// Solves a small dense system in place (Gaussian elimination, partial pivoting).
bool Solve(std::vector<std::vector<double>>& a, std::vector<double>& b)
{
	size_t n = b.size();
	for (size_t c = 0; c < n; c++) {
		size_t best = c;
		for (size_t r = c + 1; r < n; r++) if (std::fabs(a[r][c]) > std::fabs(a[best][c])) best = r;
		if (std::fabs(a[best][c]) < 1e-12) return false;
		std::swap(a[c], a[best]);
		std::swap(b[c], b[best]);
		for (size_t r = 0; r < n; r++) {
			if (r == c) continue;
			double f = a[r][c] / a[c][c];
			for (size_t k = c; k < n; k++) a[r][k] -= f * a[c][k];
			b[r] -= f * b[c];
		}
	}
	for (size_t c = 0; c < n; c++) b[c] /= a[c][c];
	return true;
}

// The projective map taking src[i] to dst[i].
std::vector<double> Homography(const Pt* src, const Pt* dst)
{
	std::vector<std::vector<double>> a(8, std::vector<double>(8, 0));
	std::vector<double> b(8, 0);
	for (int i = 0; i < 4; i++) {
		double x = src[i].first, y = src[i].second, u = dst[i].first, v = dst[i].second;
		a[2 * i] = {x, y, 1, 0, 0, 0, -u * x, -u * y};
		b[2 * i] = u;
		a[2 * i + 1] = {0, 0, 0, x, y, 1, -v * x, -v * y};
		b[2 * i + 1] = v;
	}
	if (!Solve(a, b)) Fail(kErrInvalidParams, "those corners don't make a quadrilateral (three in a line?)");
	b.push_back(1);
	return b;
}

Pt Map(const std::vector<double>& h, double x, double y)
{
	double w = h[6] * x + h[7] * y + h[8];
	return {(h[0] * x + h[1] * y + h[2]) / w, (h[3] * x + h[4] * y + h[5]) / w};
}

Pt PointParam(const Value& v, const char* what)
{
	if (!v.isArray() || v.size() != 2 || !v.asArray()[0].isNumber() || !v.asArray()[1].isNumber())
		Fail(kErrInvalidParams, std::string(what) + " must be [x, y]");
	return {v.asArray()[0].asNumber(), v.asArray()[1].asNumber()};
}

// Photoshop's custom warp is a bicubic mesh of 16 points. Fitted (least
// squares) to the perspective map, it matches it to a fraction of a pixel.
Value PerspectiveWarp(const std::vector<double>& h, double left, double top, double right, double bottom)
{
	auto bern = [](int i, double t) {
		static const double c[4] = {1, 3, 3, 1};
		return c[i] * std::pow(t, i) * std::pow(1 - t, 3 - i);
	};
	const int samples = 21;
	std::vector<std::vector<double>> ata(16, std::vector<double>(16, 0));
	std::vector<double> atx(16, 0), aty(16, 0);
	for (int sv = 0; sv < samples; sv++) for (int su = 0; su < samples; su++) {
		double u = su / (samples - 1.0), v = sv / (samples - 1.0);
		double basis[16];
		for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) basis[j * 4 + i] = bern(i, u) * bern(j, v);
		Pt target = Map(h, left + u * (right - left), top + v * (bottom - top));
		for (int r = 0; r < 16; r++) {
			for (int c = 0; c < 16; c++) ata[r][c] += basis[r] * basis[c];
			atx[r] += basis[r] * target.first;
			aty[r] += basis[r] * target.second;
		}
	}
	std::vector<std::vector<double>> ata2 = ata;
	if (!Solve(ata, atx) || !Solve(ata2, aty)) Fail(kErrInternal, "couldn't fit the warp mesh");
	Value mesh = Value::MakeArray();
	for (int k = 0; k < 16; k++) {
		Value pt;
		pt["_obj"] = "rationalPoint";
		pt["horizontal"] = Unit("pixelsUnit", atx[k]);
		pt["vertical"] = Unit("pixelsUnit", aty[k]);
		mesh.push(pt);
	}
	Value warp;
	warp["_obj"] = "warp";
	warp["warpStyle"] = Enum("warpStyle", "warpCustom");
	warp["warpValue"]["_float"] = 0;
	warp["warpPerspective"]["_float"] = 0;
	warp["warpPerspectiveOther"]["_float"] = 0;
	warp["warpRotate"] = Enum("orientation", "horizontal");
	Value b;
	b["_obj"] = "rectangle";
	b["top"] = Unit("pixelsUnit", top);
	b["left"] = Unit("pixelsUnit", left);
	b["bottom"] = Unit("pixelsUnit", bottom);
	b["right"] = Unit("pixelsUnit", right);
	warp["bounds"] = b;
	warp["uOrder"] = 4;
	warp["vOrder"] = 4;
	warp["customEnvelopeWarp"]["_obj"] = "customEnvelopeWarp";
	warp["customEnvelopeWarp"]["meshPoints"] = mesh;
	return warp;
}

// ---------------------------------------------------------------- commands

Value AppInfo(const Value&)
{
	Value app;
	app["_ref"] = "application";
	app["_enum"] = "ordinal";
	app["_value"] = "targetEnum";
	Value v = Get(Prop("hostVersion", app), "read Photoshop's version").get("hostVersion");
	Value out;
	out["photoshop"] = std::to_string(v.get("versionMajor").asInt()) + "." + std::to_string(v.get("versionMinor").asInt()) + "." +
		std::to_string(v.get("versionFix").asInt());
	out["slippy"] = kSlippyVersion;
	out["plugin"] = "native";
	int n = DocumentCount();
	int active = n ? (int) Get(Prop("documentID", RefTarget("document")), "find the active document").num("documentID") : -1;
	Value docs = Value::MakeArray();
	for (int i = 1; i <= n; i++) {
		Value r;
		r["_ref"] = "document";
		r["_index"] = i;
		Value d = Get(Arr({r}), "read the documents");
		Value e;
		e["id"] = d.get("documentID");
		e["name"] = d.get("title");
		e["active"] = d.get("documentID").asInt() == active;
		docs.push(e);
	}
	out["documents"] = docs;
	return out;
}

Value DocumentList(const Value&)
{
	int n = DocumentCount();
	int active = n ? ActiveDocumentId() : -1;
	Value out = Value::MakeArray();
	for (int i = 1; i <= n; i++) {
		Value r;
		r["_ref"] = "document";
		r["_index"] = i;
		out.push(DocumentSummary(Get(Arr({r}), "read the documents"), active));
	}
	return out;
}

Value DocumentInfo(const Value& p)
{
	int active = ActiveDocumentId();
	int id = FindDocumentId(p);
	Value s = DocumentSummary(DocumentById(id), active);
	if (id == active) {
		Value ids = Value::MakeArray();
		for (int l : ActiveLayerIds()) ids.push(l);
		s["activeLayers"] = ids;
	}
	s["coordinates"] = "pixels from the top-left, y down";
	return s;
}

Value DocumentActivate(const Value& p)
{
	int id = FindDocumentId(p);
	Value d;
	d["_obj"] = "select";
	d["_target"] = Arr({Ref("document", id)});
	Play(d, "switch documents");
	return DocumentSummary(DocumentById(id), id);
}

Value DocumentNew(const Value& p)
{
	Value doc;
	doc["_obj"] = "document";
	doc["width"] = Unit("pixelsUnit", p.num("width", 1000));
	doc["height"] = Unit("pixelsUnit", p.num("height", 1000));
	doc["resolution"] = Unit("densityUnit", p.num("resolution", 72));
	Value mode;
	mode["_class"] = "RGBColorMode";
	doc["mode"] = mode;
	std::string fill = p.str("fill", "white");
	doc["fill"] = Enum("fill", fill == "transparent" ? "transparent" : fill == "background" ? "backgroundColor" : "white");
	if (p.get("name").isString()) doc["name"] = p.get("name");
	Value d;
	d["_obj"] = "make";
	d["new"] = doc;
	Play(d, "make a document");
	int id = ActiveDocumentId();
	return DocumentSummary(DocumentById(id), id);
}

Value DocumentOpen(const Value& p)
{
	std::string path = p.str("path");
	if (!platform::IsAbsolutePath(path)) Fail(kErrInvalidParams, "'path' must be absolute");
	Value file;
	file["_path"] = path;
	Value d;
	d["_obj"] = "open";
	d["null"] = file;
	Play(d, "open " + path);
	int id = ActiveDocumentId();
	return DocumentSummary(DocumentById(id), id);
}

Value DocumentSave(const Value& p)
{
	int id = ActiveDocumentId();
	Value d;
	d["_obj"] = "save";
	if (p.get("path").isString()) {
		std::string path = p.str("path");
		PrepareSaveTarget(path);
		Value as;
		as["_obj"] = "photoshop35Format";
		as["maximizeCompatibility"] = true;
		d["as"] = as;
		Value file;
		file["_path"] = path;
		d["in"] = file;
		d["documentID"] = id;
		d["lowerCase"] = true;
	}
	else {
		Value doc = DocumentById(id);
		if (!doc.get("fileReference").isObject()) Fail(kErrInvalidParams, "This document was never saved - pass 'path' (a .psd) for the first save");
	}
	Play(d, "save the document");
	Value out;
	out["saved"] = true;
	out["path"] = DocumentById(id).get("fileReference").isObject() ? DocumentById(id).get("fileReference").get("_path") : p.get("path");
	return out;
}

// A copy as PNG or JPEG: the document isn't renamed, moved or marked saved.
Value DocumentExport(const Value& p)
{
	int id = ActiveDocumentId();
	std::string path = p.str("path");
	std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.') + 1);
	for (char& c : ext) c = (char) tolower((unsigned char) c);
	Value as;
	if (ext == "png") {
		as["_obj"] = "PNGFormat";
		as["method"] = Enum("PNGMethod", "quick");
		as["PNGInterlaceType"] = Enum("PNGInterlaceType", "PNGInterlaceNone");
		as["PNGFilter"] = Enum("PNGFilter", "PNGFilterAdaptive");
		as["compression"] = 6;
	}
	else if (ext == "jpg" || ext == "jpeg") {
		as["_obj"] = "JPEG";
		as["extendedQuality"] = (int) std::max(1.0, std::min(12.0, p.num("quality", 10)));
		as["matteColor"] = Enum("matteColor", "none");
	}
	else Fail(kErrInvalidParams, "export to a path ending .png or .jpg");
	PrepareSaveTarget(path);
	Value file;
	file["_path"] = path;
	Value d;
	d["_obj"] = "save";
	d["as"] = as;
	d["in"] = file;
	d["documentID"] = id;
	d["copy"] = true;
	d["lowerCase"] = true;
	Play(d, "export " + path);
	Value doc = DocumentById(id);
	double res = doc.get("resolution").num("_value", 72);
	Value out;
	out["path"] = path;
	out["format"] = ext == "png" ? "png" : "jpg";
	out["width"] = std::round(Pixels(doc.get("width"), res));
	out["height"] = std::round(Pixels(doc.get("height"), res));
	return out;
}

// Never asks "Save changes?": closing discards them unless save=true.
Value DocumentClose(const Value& p)
{
	int id = FindDocumentId(p);
	std::string name = DocumentById(id).str("title");
	Value d;
	d["_obj"] = "close";
	d["saving"] = Enum("yesNo", p.boolean("save") ? "yes" : "no");
	d["documentID"] = id;
	Play(d, "close " + name);
	Value out;
	out["closed"] = name;
	out["saved"] = p.boolean("save");
	return out;
}

Value DocumentCrop(const Value& p)
{
	ActiveDocumentId();
	Value d;
	d["_obj"] = "crop";
	d["to"] = Rect(p);
	d["angle"] = Unit("angleUnit", 0);
	d["delete"] = true;
	Play(d, "crop");
	int id = ActiveDocumentId();
	return DocumentSummary(DocumentById(id), id);
}

Value HistoryStep(const Value& p, const char* direction)
{
	ActiveDocumentId();
	int steps = (int) std::max(1.0, p.num("steps", 1));
	Value state;
	state["_ref"] = "historyState";
	state["_enum"] = "ordinal";
	state["_value"] = direction;
	for (int i = 0; i < steps; i++) {
		Value d;
		d["_obj"] = "select";
		d["_target"] = Arr({state});
		Play(d, std::string(direction) == "previous" ? "undo" : "redo");
	}
	Value out;
	out["steps"] = steps;
	return out;
}

Value LayerTreeCommand(const Value& p)
{
	int id = ActiveDocumentId();
	Value out;
	out["document"] = DocumentById(id).get("title");
	out["layers"] = LayerTree(id, p.get("depth").isNumber() ? p.get("depth").asInt() : -1);
	return out;
}

Value LayerGet(const Value& p)
{
	Value l = LayerById(LayerId(p));
	Value s = LayerSummary(l);
	if (l.get("fillOpacity").isNumber()) s["fillOpacity"] = std::round(l.get("fillOpacity").asNumber() / 2.55);
	if (l.get("parentLayerID").isNumber() && l.get("parentLayerID").asInt() >= 0) s["parent"] = l.get("parentLayerID");
	if (l.get("textKey").isObject()) s["text"] = l.get("textKey").get("textKey");
	return s;
}

Value LayerSelect(const Value& p)
{
	ActiveDocumentId();
	std::vector<int> ids;
	if (p.get("ids").isArray()) for (const Value& v : p.get("ids").asArray()) ids.push_back(v.asInt());
	else if (p.get("id").isNumber()) ids.push_back(p.get("id").asInt());
	if (ids.empty()) Fail(kErrInvalidParams, "pass 'id' or 'ids'");
	for (size_t i = 0; i < ids.size(); i++) { LayerById(ids[i]); SelectLayer(ids[i], p.boolean("add") || i > 0); }
	Value out = Value::MakeArray();
	for (int id : ActiveLayerIds()) out.push(LayerSummary(LayerById(id)));
	return out;
}

Value LayerSet(const Value& p)
{
	int id = LayerId(p);
	Value to;
	to["_obj"] = "layer";
	if (p.has("name")) to["name"] = p.str("name");
	if (p.has("opacity")) to["opacity"] = Unit("percentUnit", std::max(0.0, std::min(100.0, p.num("opacity"))));
	if (p.has("fillOpacity")) to["fillOpacity"] = Unit("percentUnit", std::max(0.0, std::min(100.0, p.num("fillOpacity"))));
	if (p.has("blendMode")) to["mode"] = Enum("blendMode", BlendMode(p.str("blendMode")).c_str());
	if (to.size() > 1) {
		// Photoshop applies opacity and blend mode to the active layer,
		// whichever layer the reference names - so make it the active one.
		SelectLayer(id);
		Value d;
		d["_obj"] = "set";
		d["_target"] = Arr({RefTarget("layer")});
		d["to"] = to;
		Play(d, "change the layer");
	}
	if (p.has("visible")) {
		Value d;
		d["_obj"] = p.boolean("visible") ? "show" : "hide";
		d["null"] = Value(json::Array{Arr({Ref("layer", id)})});
		Play(d, p.boolean("visible") ? "show the layer" : "hide the layer");
	}
	if (p.has("locked")) {
		Value d;
		d["_obj"] = "applyLocking";
		d["_target"] = Arr({Ref("layer", id)});
		d["layerLocking"]["_obj"] = "layerLocking";
		d["layerLocking"]["protectAll"] = p.boolean("locked");
		Play(d, "lock the layer");
	}
	return LayerSummary(LayerById(id));
}

Value LayerCreate(const Value& p)
{
	ActiveDocumentId();
	bool group = p.str("kind", "pixel") == "group";
	Value target;
	target["_ref"] = group ? "layerSection" : "layer";
	Value with;
	with["_obj"] = group ? "layerSection" : "layer";
	with["name"] = p.str("name", group ? "Group" : "Layer");
	Value d;
	d["_obj"] = "make";
	d["_target"] = Arr({target});
	d["using"] = with;
	Play(d, group ? "make a group" : "make a layer");
	std::vector<int> ids = ActiveLayerIds();
	if (ids.empty()) Fail(kErrInternal, "made it, but can't find it");
	return LayerSummary(LayerById(ids[0]));
}

Value LayerDelete(const Value& p)
{
	ActiveDocumentId();
	std::vector<int> ids;
	if (p.get("ids").isArray()) for (const Value& v : p.get("ids").asArray()) ids.push_back(v.asInt());
	else ids.push_back(LayerId(p));
	for (int id : ids) LayerById(id);   // all exist before any goes
	for (int id : ids) {
		Value d;
		d["_obj"] = "delete";
		d["_target"] = Arr({Ref("layer", id)});
		Play(d, "delete layer " + std::to_string(id));
	}
	Value out;
	out["deleted"] = (int) ids.size();
	return out;
}

Value LayerDuplicate(const Value& p)
{
	int id = LayerId(p);
	Value d;
	d["_obj"] = "duplicate";
	d["_target"] = Arr({Ref("layer", id)});
	if (p.get("name").isString()) d["name"] = p.get("name");
	Value r = Play(d, "duplicate the layer");
	int copy = r.get("ID").isArray() && r.get("ID").size() ? r.get("ID").asArray()[0].asInt() : 0;
	if (!copy) { std::vector<int> ids = ActiveLayerIds(); if (!ids.empty()) copy = ids[0]; }
	return LayerSummary(LayerById(copy));
}

Value LayerTransform(const Value& p)
{
	int id = LayerId(p);
	SelectLayer(id);
	double sx = 100, sy = 100;
	const Value& scale = p.get("scale");
	if (scale.isNumber()) sx = sy = scale.asNumber();
	else if (scale.isArray() && scale.size() == 2) { sx = scale.asArray()[0].asNumber(); sy = scale.asArray()[1].asNumber(); }
	Value d;
	d["_obj"] = "transform";
	d["_target"] = Arr({RefTarget("layer")});
	d["freeTransformCenterState"] = Enum("quadCenterState", "QCSAverage");
	d["offset"]["_obj"] = "offset";
	d["offset"]["horizontal"] = Unit("pixelsUnit", p.num("dx"));
	d["offset"]["vertical"] = Unit("pixelsUnit", p.num("dy"));
	d["width"] = Unit("percentUnit", sx);
	d["height"] = Unit("percentUnit", sy);
	d["angle"] = Unit("angleUnit", p.num("rotate"));
	d["interfaceIconFrameDimmed"] = Enum("interpolationType", "bicubic");
	Play(d, "transform the layer");
	return LayerSummary(LayerById(id));
}

// Restacks with Photoshop's "move to index", then checks where it landed:
// the index it wants depends on which way the layer travels.
Value LayerMove(const Value& p)
{
	int id = LayerId(p);
	int other = 0;
	bool into = false, above = true;
	if (p.get("into").isNumber()) { other = p.get("into").asInt(); into = true; }
	else if (p.get("above").isNumber()) other = p.get("above").asInt();
	else if (p.get("below").isNumber()) { other = p.get("below").asInt(); above = false; }
	else Fail(kErrInvalidParams, "pass 'above', 'below' or 'into' (a layer id)");
	if (other == id) Fail(kErrInvalidParams, "a layer can't move relative to itself");
	auto index = [](int layer) { return LayerById(layer).get("itemIndex").asInt(); };
	// Where it should end up: directly above the other (or just inside the
	// group, at its top), or directly below it.
	auto placed = [&] {
		int mine = index(id), theirs = index(other);
		return above && !into ? mine == theirs + 1 : mine == theirs - 1;
	};
	SelectLayer(id);
	int theirs = index(other);
	int base = into ? theirs - 1 : above ? theirs : theirs - 1;
	for (int tryIndex : {base, base - 1, base + 1, base + 2, base - 2}) {
		Value to;
		to["_ref"] = "layer";
		to["_index"] = tryIndex;
		Value d;
		d["_obj"] = "move";
		d["_target"] = Arr({RefTarget("layer")});
		d["to"] = to;
		d["adjustment"] = false;
		d["version"] = 5;
		try { Play(d, "restack the layer"); } catch (const PsError&) { continue; }
		if (placed()) return LayerSummary(LayerById(id));
	}
	Fail(kErrHost, "Photoshop didn't put the layer where asked");
}

Value TextCreate(const Value& p)
{
	int docId = ActiveDocumentId();
	std::string text = p.str("contents");
	if (text.empty()) Fail(kErrInvalidParams, "'contents' is required");
	Value doc = DocumentById(docId);
	double res = doc.get("resolution").num("_value", 72);
	double w = Pixels(doc.get("width"), res), h = Pixels(doc.get("height"), res);
	double size = p.num("size", 24);
	// UTF-16 length: text ranges count in UTF-16 units.
	int units = 0;
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = (unsigned char) text[i];
		if ((c & 0xC0) != 0x80) units += c >= 0xF0 ? 2 : 1;
	}
	Value style;
	style["_obj"] = "textStyle";
	style["fontPostScriptName"] = p.str("font", "ArialMT");
	style["size"] = Unit("pointsUnit", size * 72.0 / res);
	style["color"] = RgbColor(p.get("color").isString() ? p.get("color") : Value("#000000"));
	Value range;
	range["_obj"] = "textStyleRange";
	range["from"] = 0;
	range["to"] = units;
	range["textStyle"] = style;
	Value layer;
	layer["_obj"] = "textLayer";
	layer["textKey"] = text;
	layer["textClickPoint"]["_obj"] = "paint";
	layer["textClickPoint"]["horizontal"] = Unit("percentUnit", p.num("x") / w * 100);
	layer["textClickPoint"]["vertical"] = Unit("percentUnit", p.num("y", size) / h * 100);
	layer["textStyleRange"] = Arr({range});
	Value target;
	target["_ref"] = "textLayer";
	Value d;
	d["_obj"] = "make";
	d["_target"] = Arr({target});
	d["using"] = layer;
	Play(d, "add text");
	std::vector<int> ids = ActiveLayerIds();
	if (ids.empty()) Fail(kErrInternal, "made the text, but can't find it");
	int id = ids[0];
	if (p.get("name").isString()) {
		Value set;
		set["_obj"] = "set";
		set["_target"] = Arr({Ref("layer", id)});
		set["to"]["_obj"] = "layer";
		set["to"]["name"] = p.get("name");
		Play(set, "name the text");
	}
	return LayerSummary(LayerById(id));
}

Value SelectShape(const Value& p, const char* shape)
{
	ActiveDocumentId();
	std::string mode = p.str("mode", "replace");
	Value to = Rect(p);
	to["_obj"] = shape;
	Value d;
	d["_obj"] = mode == "add" ? "addTo" : mode == "subtract" ? "subtractFrom" : mode == "intersect" ? "intersectWith" : "set";
	d["_target"] = SelectionRef();
	d["to"] = to;
	d["antiAlias"] = p.boolean("antiAlias", true);
	if (p.num("feather") > 0) d["feather"] = Unit("pixelsUnit", p.num("feather"));
	Play(d, std::string("select an ") + shape);
	Value out;
	out["selected"] = shape;
	return out;
}

Value SelectAll(const Value&, bool all)
{
	ActiveDocumentId();
	Value d;
	d["_obj"] = "set";
	d["_target"] = SelectionRef();
	d["to"] = Enum("ordinal", all ? "allEnum" : "none");
	Play(d, all ? "select all" : "deselect");
	Value out;
	out["selection"] = all ? "all" : "none";
	return out;
}

Value EditFill(const Value& p)
{
	ActiveDocumentId();
	Value d;
	d["_obj"] = "fill";
	std::string with = p.str("with", p.has("color") ? "color" : "foregroundColor");
	if (with == "color") {
		d["using"] = Enum("fillContents", "color");
		d["color"] = RgbColor(p.get("color"));
	}
	else d["using"] = Enum("fillContents", with.c_str());   // black, white, gray, foregroundColor, backgroundColor, contentAware
	d["opacity"] = Unit("percentUnit", p.num("opacity", 100));
	d["mode"] = Enum("blendMode", BlendMode(p.str("blendMode", "normal")).c_str());
	Play(d, "fill");
	Value out;
	out["filled"] = with;
	return out;
}

Value FilterBlur(const Value& p)
{
	ActiveDocumentId();
	Value d;
	d["_obj"] = "gaussianBlur";
	d["radius"] = Unit("pixelsUnit", Required(p, "radius"));
	Play(d, "blur");
	Value out;
	out["radius"] = p.get("radius");
	return out;
}

// Straightens a photographed page, sign or screen: the four corners of the
// thing (top-left, top-right, bottom-right, bottom-left) go to a rectangle.
Value LayerPerspective(const Value& p)
{
	int docId = ActiveDocumentId();
	const Value& c = p.get("corners");
	if (!c.isArray() || c.size() != 4) Fail(kErrInvalidParams, "'corners' must be 4 points [x, y]: top-left, top-right, bottom-right, bottom-left");
	Pt src[4];
	for (int i = 0; i < 4; i++) src[i] = PointParam(c.asArray()[i], "each corner");
	auto dist = [](Pt a, Pt b) { return std::hypot(a.first - b.first, a.second - b.second); };
	double left, top, right, bottom;
	if (p.get("to").isObject()) {
		const Value& to = p.get("to");
		left = Required(to, "left"); top = Required(to, "top"); right = Required(to, "right"); bottom = Required(to, "bottom");
	}
	else {   // keep its size: the average of opposite sides
		double w = (dist(src[0], src[1]) + dist(src[3], src[2])) / 2, h = (dist(src[0], src[3]) + dist(src[1], src[2])) / 2;
		left = std::min(src[0].first, src[3].first);
		top = std::min(src[0].second, src[1].second);
		right = left + w;
		bottom = top + h;
	}
	Pt dst[4] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
	std::vector<double> h = Homography(src, dst);

	int id = LayerId(p);
	Value layer = LayerById(id);
	if (layer.boolean("background")) {   // the background can't be warped: make it a layer
		Value bg;
		bg["_ref"] = "layer";
		bg["_property"] = "background";
		Value d;
		d["_obj"] = "set";
		d["_target"] = Arr({bg});
		d["to"]["_obj"] = "layer";
		d["to"]["opacity"] = Unit("percentUnit", 100);
		d["to"]["mode"] = Enum("blendMode", "normal");
		Play(d, "unlock the background");
		std::vector<int> ids = ActiveLayerIds();
		if (!ids.empty()) id = ids[0];
		layer = LayerById(id);
	}
	SelectLayer(id);
	Value b = Bounds(layer.get("bounds"));
	Value d;
	d["_obj"] = "transform";
	d["_target"] = Arr({RefTarget("layer")});
	d["freeTransformCenterState"] = Enum("quadCenterState", "QCSAverage");
	d["offset"]["_obj"] = "offset";
	d["offset"]["horizontal"] = Unit("pixelsUnit", 0);
	d["offset"]["vertical"] = Unit("pixelsUnit", 0);
	d["warp"] = PerspectiveWarp(h, b.num("left"), b.num("top"), b.num("right"), b.num("bottom"));
	Play(d, "warp the layer");
	if (p.boolean("crop")) {
		Value r;
		r["left"] = left; r["top"] = top; r["right"] = right; r["bottom"] = bottom;
		Value cd;
		cd["_obj"] = "crop";
		cd["to"] = Rect(r);
		cd["angle"] = Unit("angleUnit", 0);
		cd["delete"] = true;
		Play(cd, "crop to the result");
	}
	Value out = LayerSummary(LayerById(id));
	Value rect;
	rect["left"] = left; rect["top"] = top; rect["right"] = right; rect["bottom"] = bottom;
	out["rectangle"] = rect;
	(void) docId;
	return out;
}

Value PsGet(const Value& p)
{
	Value target = p.get("target").isArray() ? p.get("target") : Value(json::Array{p.get("target")});
	if (p.get("property").isString()) {
		Value prop;
		prop["_property"] = p.get("property");
		json::Array refs{prop};
		for (const Value& r : target.asArray()) refs.push_back(r);
		target = Value(refs);
	}
	return Get(target, "read it");
}

Value PsBatchPlay(const Value& p)
{
	const Value& list = p.get("descriptors");
	if (!list.isArray() || list.size() == 0) Fail(kErrInvalidParams, "'descriptors' must be a non-empty array of {_obj, ...}");
	Value out = Value::MakeArray();
	int i = 0;
	for (const Value& d : list.asArray()) {
		i++;
		std::string event = d.isObject() ? d.str("_obj", "?") : "?";
		out.push(Play(d, "play descriptor " + std::to_string(i) + " (" + event + ")"));
	}
	return out;
}

// ---------------------------------------------------------------- the table

Value Describe_();

CommandTable& Table()
{
	static CommandTable t = {
		{"app.info", {"Photoshop and Slippy versions, open documents.", Params({}), AppInfo, Kind::Read}},
		{"commands.list", {"Every command with its parameters.", Params({}), [](const Value&) { return Describe_(); }, Kind::Read}},
		{"document.list", {"Open documents: id, name, path, size in pixels, resolution, mode.", Params({}), DocumentList, Kind::Read}},
		{"document.info", {"The active document (or 'id' / 'name'): size in pixels, resolution, mode, layer count, active layers.",
			Params({{"id", "number"}, {"name", "string"}}), DocumentInfo, Kind::Read}},
		{"document.activate", {"Switch to an open document by 'id' or 'name'.", Params({{"id", "number"}, {"name", "string"}}), DocumentActivate, Kind::Plain}},
		{"document.new", {"New RGB document, no dialog.", Params({{"width", "number - pixels (default 1000)"}, {"height", "number - pixels (default 1000)"},
			{"resolution", "number (default 72)"}, {"name", "string"}, {"fill", "white | transparent | background"}}), DocumentNew, Kind::Plain}},
		{"document.open", {"Open a file by absolute path, no dialog.", Params({{"path", "string - absolute"}}), DocumentOpen, Kind::Plain}},
		{"document.save", {"Save the active document; with 'path', save as a PSD there (the document moves to it).",
			Params({{"path", "string - absolute .psd (needed the first time)"}}), DocumentSave, Kind::Plain}},
		{"document.export", {"Export a copy of the active document as PNG or JPEG (from the extension). The document itself is untouched.",
			Params({{"path", "string - absolute, .png or .jpg"}, {"quality", "number - JPEG 1-12 (default 10)"}}), DocumentExport, Kind::Plain}},
		{"document.close", {"Close a document (default: active). Discards changes unless save=true - never asks.",
			Params({{"id", "number"}, {"name", "string"}, {"save", "boolean (default false)"}}), DocumentClose, Kind::Plain}},
		{"document.crop", {"Crop the document to a rectangle (pixels), deleting what's outside.",
			Params({{"left", "number"}, {"top", "number"}, {"right", "number"}, {"bottom", "number"}}), DocumentCrop, Kind::Edit}},
		{"history.undo", {"Step back in History.", Params({{"steps", "number (default 1)"}}), [](const Value& p) { return HistoryStep(p, "previous"); }, Kind::Plain}},
		{"history.redo", {"Step forward in History.", Params({{"steps", "number (default 1)"}}), [](const Value& p) { return HistoryStep(p, "next"); }, Kind::Plain}},
		{"layer.tree", {"Every layer, top to bottom, nested in its groups: id, name, kind, visible, opacity, blend mode, bounds (pixels).",
			Params({{"depth", "number - levels of groups to open (default all)"}}), LayerTreeCommand, Kind::Read}},
		{"layer.get", {"One layer in detail (default: the active layer).", Params({{"id", "number"}}), LayerGet, Kind::Read}},
		{"layer.select", {"Make layers active ('id' or 'ids'; add=true keeps the current ones).",
			Params({{"id", "number"}, {"ids", "number[]"}, {"add", "boolean"}}), LayerSelect, Kind::Plain}},
		{"layer.set", {"Change a layer (default: active): name, visible, opacity 0-100, fillOpacity, blendMode, locked.",
			Params({{"id", "number"}, {"name", "string"}, {"visible", "boolean"}, {"opacity", "number - 0-100"}, {"fillOpacity", "number - 0-100"},
				{"blendMode", "string - normal, multiply, screen, overlay, softLight..."}, {"locked", "boolean"}}), LayerSet, Kind::Edit}},
		{"layer.create", {"New pixel layer (default) or group above the active layer.", Params({{"kind", "pixel | group"}, {"name", "string"}}), LayerCreate, Kind::Edit}},
		{"layer.delete", {"Delete layers ('id' or 'ids'; default: active).", Params({{"id", "number"}, {"ids", "number[]"}}), LayerDelete, Kind::Edit}},
		{"layer.duplicate", {"Duplicate a layer (default: active); returns the copy.", Params({{"id", "number"}, {"name", "string"}}), LayerDuplicate, Kind::Edit}},
		{"layer.transform", {"Move (dx, dy pixels), scale (percent, or [x, y]) and / or rotate (degrees, clockwise) a layer about its center.",
			Params({{"id", "number"}, {"dx", "number"}, {"dy", "number"}, {"scale", "number | [x, y] - percent"}, {"rotate", "number - degrees"}}),
			LayerTransform, Kind::Edit}},
		{"layer.move", {"Restack a layer: directly 'above' or 'below' another layer, or 'into' a group (at its top).",
			Params({{"id", "number"}, {"above", "number"}, {"below", "number"}, {"into", "number - group id"}}), LayerMove, Kind::Edit}},
		{"layer.perspective", {"Straighten perspective (a photographed page, sign, screen): the four corners of the thing in the photo - top-left, "
			"top-right, bottom-right, bottom-left - go to a rectangle ('to', default: the same size). A background becomes a layer first. crop=true crops to it.",
			Params({{"corners", "[[x, y] x4] - pixels"}, {"to", "{left, top, right, bottom} (optional)"}, {"id", "number - layer (default: active)"},
				{"crop", "boolean"}}), LayerPerspective, Kind::Edit}},
		{"text.create", {"New point text layer: contents at (x, y) pixels (the first baseline's start), size in pixels, font (PostScript name), color #RRGGBB.",
			Params({{"contents", "string"}, {"x", "number"}, {"y", "number"}, {"size", "number - pixels (default 24)"}, {"font", "string - PostScript name"},
				{"color", "#RRGGBB"}, {"name", "string"}}), TextCreate, Kind::Edit}},
		{"select.rect", {"Select a rectangle (pixels). mode: replace (default), add, subtract, intersect.",
			Params({{"left", "number"}, {"top", "number"}, {"right", "number"}, {"bottom", "number"}, {"mode", "string"}, {"feather", "number"}}),
			[](const Value& p) { return SelectShape(p, "rectangle"); }, Kind::Edit}},
		{"select.ellipse", {"Select the ellipse inside a rectangle (pixels). mode: replace (default), add, subtract, intersect.",
			Params({{"left", "number"}, {"top", "number"}, {"right", "number"}, {"bottom", "number"}, {"mode", "string"}, {"feather", "number"}}),
			[](const Value& p) { return SelectShape(p, "ellipse"); }, Kind::Edit}},
		{"select.all", {"Select the whole canvas.", Params({}), [](const Value& p) { return SelectAll(p, true); }, Kind::Edit}},
		{"select.none", {"Deselect.", Params({}), [](const Value& p) { return SelectAll(p, false); }, Kind::Edit}},
		{"edit.fill", {"Fill the selection (or the layer) with a color, or 'with': black, white, gray, foregroundColor, backgroundColor, contentAware.",
			Params({{"color", "#RRGGBB"}, {"with", "string"}, {"opacity", "number - 0-100"}, {"blendMode", "string"}}), EditFill, Kind::Edit}},
		{"filter.blur", {"Gaussian blur the selection (or the layer).", Params({{"radius", "number - pixels"}}), FilterBlur, Kind::Edit}},
		{"ps.get", {"Read Photoshop properties: 'target' is a reference ([{\"_ref\": \"layer\", \"_id\": 3}] or {\"_ref\": \"document\", \"_enum\": \"ordinal\", "
			"\"_value\": \"targetEnum\"}); 'property' narrows it to one.", Params({{"target", "object[] - reference"}, {"property", "string (optional)"}}),
			PsGet, Kind::Read}},
		{"ps.batchplay", {"Play Photoshop action descriptors - batchPlay's JSON ({_obj, _target, ...}; Photoshop's \"Copy As JavaScript\" shows them) - as one "
			"History step. For anything the named commands don't cover.", Params({{"descriptors", "object[] - descriptors"}}), PsBatchPlay, Kind::Edit}},
	};
	return t;
}

Value Describe_()
{
	Value list = Value::MakeArray();
	for (auto& kv : Table()) {
		Value c;
		c["method"] = kv.first;
		c["description"] = kv.second.description;
		c["params"] = kv.second.params;
		c["changesDocument"] = kv.second.kind != Kind::Read;
		list.push(c);
	}
	return list;
}

// ---------------------------------------------------------------- running calls

CallObserver gObserver;

std::string LogPath() { return platform::JoinPath(platform::SupportDir(), "calls.log"); }

void Log(const std::string& method, const Value& params, const Value& error, double ms, const std::string& agent)
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

Value AppLog(const Value& p)
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
	Value out = Value::MakeArray();
	for (size_t i = lines.size() > want ? lines.size() - want : 0; i < lines.size(); i++) out.push(lines[i]);
	return out;
}

const Command* Find(const Value& call)
{
	if (!call.isObject() || !call.get("method").isString()) return nullptr;
	auto it = Table().find(call.get("method").asString());
	return it == Table().end() ? nullptr : &it->second;
}

Value RunOneUntimed(const Value& call)
{
	Value response;
	response["jsonrpc"] = "2.0";
	response["id"] = call.get("id");
	auto error = [&](int code, const std::string& message, int psError) {
		response["error"]["code"] = code;
		response["error"]["message"] = message;
		if (psError) response["error"]["data"]["psError"] = psError;
		return response;
	};
	if (!call.isObject() || !call.get("method").isString()) return error(kErrInvalidRequest, "each call needs a 'method' string", 0);
	std::string method = call.get("method").asString();
	if (method == "app.log") {
		response["result"] = AppLog(call.get("params").isObject() ? call.get("params") : Value::MakeObject());
		return response;
	}
	const Command* c = Find(call);
	if (!c) return error(kErrMethodNotFound, "unknown method '" + method + "' - see commands.list (or slippy_find)", 0);
	const Value& params = call.get("params");
	if (!params.isNull() && !params.isObject()) return error(kErrInvalidParams, "'params' must be an object", 0);
	crashlog::SetCurrentCall(method, params.isNull() ? "{}" : params.dump());
	struct Done { ~Done() { crashlog::ClearCurrentCall(); } } done;
	try {
		response["result"] = c->run(params.isNull() ? Value::MakeObject() : params);
	}
	catch (const PsError& e) { return error(e.code, e.message, e.psError); }
	catch (const json::Error& e) { return error(kErrInvalidParams, e.what(), 0); }
	catch (const std::exception& e) { return error(kErrInternal, e.what(), 0); }
	catch (...) { return error(kErrInternal, "unexpected error", 0); }
	return response;
}

Value RunOne(const Value& call)
{
	auto start = std::chrono::steady_clock::now();
	Value response = RunOneUntimed(call);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::string method = call.isObject() && call.get("method").isString() ? call.get("method").asString() : "(invalid)";
	const Command* c = Find(call);
	const Value& err = response.get("error");
	std::string message = err.isNull() ? "" : err.get("message").isString() ? err.get("message").asString() : "error";
	std::string line = Narrate(method, call.get("params"), response.get("result"), message);
	std::string agent = call.isObject() ? call.str("agent", "") : "";
	Log(method, call.get("params"), err, ms, agent);
	if (gObserver) gObserver(method, err.isNull(), c && c->kind != Kind::Read, ms, line, agent);
	return response;
}

bool IsEdit(const Value& call)
{
	const Command* c = Find(call);
	return c && c->kind == Kind::Edit;
}

// Runs calls as one History step named 'name' (in the active document), or
// plainly when no document is open. Stops after a failure (unless that call
// says stopOnError: false). Returns their responses, in order.
std::vector<Value> RunAsOneStep(const std::vector<const Value*>& calls, const std::string& name)
{
	std::vector<Value> out;
	bool stopped = false;
	auto runAll = [&] {
		for (const Value* c : calls) {
			if (stopped) break;
			out.push_back(RunOne(*c));
			if (out.back().has("error") && c->boolean("stopOnError", true)) stopped = true;
		}
	};
	bool haveDocument = false;
	try { haveDocument = DocumentCount() > 0; } catch (...) {}
	if (!haveDocument) { runAll(); return out; }

	struct Context { std::function<void()> run; } ctx{runAll};
	PIActionReference doc = ReferenceFrom(Arr({RefTarget("document")}));
	ASZString z = ToZ(name.size() > 70 ? name.substr(0, 69) + "…" : name);
	OSErr e = sControl->SuspendHistory(doc, [](void* data) -> SPErr {
		// Keep whatever ran, even after a failure (the call reports it).
		try { ((Context*) data)->run(); } catch (...) {}
		return 0;
	}, &ctx, z);
	sZString->Release(z);
	sRef->Free(doc);
	if (e && out.empty()) runAll();   // couldn't group them: run them anyway
	return out;
}

std::string StepName(const std::vector<const Value*>& calls)
{
	if (calls.size() == 1) {
		const Value& c = *calls[0];
		std::string line = Narrate(c.str("method"), c.get("params"), Value(), "");
		return "Slippy: " + line;
	}
	return "Slippy: " + std::to_string(calls.size()) + " changes";
}

Value Skipped(const Value& call)
{
	Value r;
	r["jsonrpc"] = "2.0";
	r["id"] = call.isObject() ? call.get("id") : Value();
	r["error"]["code"] = kErrInvalidRequest;
	r["error"]["message"] = "skipped: an earlier call in the batch failed";
	return r;
}

} // namespace

void SetCallObserver(CallObserver observer) { gObserver = std::move(observer); }

Value Describe() { return Describe_(); }

bool WritesFile(const Value& call)
{
	if (!call.isObject() || !call.get("method").isString()) return false;
	const std::string& m = call.get("method").asString();
	return m == "document.save" || m == "document.export" || (m == "document.close" && call.get("params").isObject() && call.get("params").boolean("save", false));
}

// Photoshop runs every call to completion, documents and files included, so
// nothing has to wait for an event of its own.
bool StartsRun(const Value&) { return false; }
bool EndsRun(const Value&) { return false; }

Value Handle(const Value& request, Value* rest, const Value*)
{
	if (rest) *rest = Value();
	if (!request.isArray()) {
		if (IsEdit(request)) return RunAsOneStep({&request}, StepName({&request}))[0];
		return RunOne(request);
	}
	// A batch: consecutive edits share one History step; anything else runs
	// on its own between them.
	Value out = Value::MakeArray();
	const json::Array& calls = request.asArray();
	bool stopped = false;
	for (size_t i = 0; i < calls.size();) {
		if (stopped) { out.push(Skipped(calls[i++])); continue; }
		if (!IsEdit(calls[i])) {
			Value r = RunOne(calls[i]);
			if (r.has("error") && calls[i].boolean("stopOnError", true)) stopped = true;
			out.push(r);
			i++;
			continue;
		}
		std::vector<const Value*> group;
		size_t j = i;
		while (j < calls.size() && IsEdit(calls[j])) group.push_back(&calls[j++]);
		std::vector<Value> results = RunAsOneStep(group, StepName(group));
		for (Value& r : results) out.push(r);
		if (results.size() < group.size() || (results.size() && results.back().has("error"))) stopped = true;
		i += results.size();
	}
	return out;
}

} // namespace slippy
