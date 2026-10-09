// What the person sees: zoom, scroll, screen and preview modes, guides /
// grid / edges, a screenshot of the window, and what's under a point.
// AIDocumentViewSuite with a NULL view means the current view.

#include "Kit.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>

namespace slippy {

namespace {

const char* ScreenModeName(AIScreenMode m)
{
	switch (m) {
	case kNormalScreenMode: return "normal";
	case kFullScreenWithMenuMode: return "fullWithMenu";
	case kFullScreenNoMenuMode: return "full";
	default: return "none";
	}
}

const char* PreviewName(ai::int16 style)
{
	if (style & kVsArtwork) return "outline";
	if ((style & kVsPreview) && (style & kVsInk)) return "overprint";
	if ((style & kVsPreview) && (style & kVsRaster)) return "pixel";
	return "preview";
}

json::Value ViewGet(const json::Value&)
{
	Need(sAIDocumentView, "The document view suite");
	ActiveDocument();
	json::Value v;
	AIReal zoom = 1;
	if (!sAIDocumentView->GetDocumentViewZoom(nullptr, &zoom)) v["zoom"] = (double) zoom;
	AIRealPoint center;
	if (!sAIDocumentView->GetDocumentViewCenter(nullptr, &center)) v["center"] = PointJson(center);
	AIRealRect bounds;
	if (!sAIDocumentView->GetDocumentViewBounds(nullptr, &bounds)) v["visible"] = RectJson(bounds);
	AIScreenMode mode = kNoScreenMode;
	if (!sAIDocumentView->GetScreenMode(nullptr, &mode)) v["screenMode"] = ScreenModeName(mode);
	ai::int16 style = 0;
	if (!sAIDocumentView->GetDocumentViewStyle(nullptr, &style)) v["mode"] = PreviewName(style);
	AIBoolean on = false, snap = false;
	if (!sAIDocumentView->GetShowGuides(nullptr, &on)) v["guides"] = (bool) on;
	if (!sAIDocumentView->GetShowEdges(nullptr, &on)) v["edges"] = (bool) on;
	if (!sAIDocumentView->GetGridOptions(nullptr, &on, &snap)) { v["grid"] = (bool) on; v["snapToGrid"] = (bool) snap; }
	if (!sAIDocumentView->GetShowTransparencyGrid(nullptr, &on)) v["transparencyGrid"] = (bool) on;
	if (!sAIDocumentView->IsArtboardRulerVisible(nullptr, &on)) v["rulers"] = (bool) on;
	v["note"] = "zoom 1 = 100%; center / visible are artwork points (y up)";
	return v;
}

json::Value ViewSet(const json::Value& p)
{
	Need(sAIDocumentView, "The document view suite");
	ActiveDocument();
	if (p.has("zoom")) {
		double z = ReqNum(p, "zoom");
		if (z < 0.0003 || z > 640) Fail(kErrInvalidParams, "'zoom' must be 0.0003 to 640 (1 = 100%)");
		Check(sAIDocumentView->SetDocumentViewZoom(nullptr, (AIReal) z), "SetDocumentViewZoom");
	}
	if (p.has("center")) {
		AIRealPoint c = Point(p.get("center"), "center");
		Check(sAIDocumentView->SetDocumentViewCenter(nullptr, &c), "SetDocumentViewCenter");
	}
	if (p.has("screenMode")) {
		std::string m = ReqStr(p, "screenMode");
		AIScreenMode mode = m == "normal" ? kNormalScreenMode : m == "fullWithMenu" ? kFullScreenWithMenuMode : m == "full" ? kFullScreenNoMenuMode : kNoScreenMode;
		if (mode == kNoScreenMode) Fail(kErrInvalidParams, "'screenMode' must be normal, fullWithMenu or full");
		Check(sAIDocumentView->SetScreenMode(nullptr, mode), "SetScreenMode");
	}
	if (p.has("mode")) {
		std::string m = ReqStr(p, "mode");
		ai::int16 style = m == "outline" ? kVsArtwork : m == "preview" ? kVsPreview : m == "pixel" ? (kVsPreview | kVsRaster) : m == "overprint" ? (kVsPreview | kVsInk) : 0;
		if (!style) Fail(kErrInvalidParams, "'mode' must be preview, outline, pixel or overprint");
		Check(sAIDocumentView->SetDocumentViewStyle(nullptr, style, kVsArtwork | kVsPreview | kVsRaster | kVsInk), "SetDocumentViewStyle");
	}
	auto flag = [&](const char* key, auto set) { if (p.has(key)) Check(set(p.boolean(key, false)), key); };
	flag("guides", [](bool b) { return sAIDocumentView->SetShowGuides(nullptr, b); });
	flag("edges", [](bool b) { return sAIDocumentView->SetShowEdges(nullptr, b); });
	flag("transparencyGrid", [](bool b) { return sAIDocumentView->SetShowTransparencyGrid(nullptr, b); });
	flag("rulers", [](bool b) { return sAIDocumentView->SetArtboardRulerVisible(nullptr, b); });
	if (p.has("grid") || p.has("snapToGrid")) {
		AIBoolean show = false, snap = false;
		sAIDocumentView->GetGridOptions(nullptr, &show, &snap);
		Check(sAIDocumentView->SetGridOptions(nullptr, p.boolean("grid", show), p.boolean("snapToGrid", snap)), "SetGridOptions");
	}
	return ViewGet(p);
}

AIRealRect ArtboardBounds(int index)
{
	Need(sAIArtboard, "The artboard suite");
	ai::ArtboardList list;
	Check(sAIArtboard->GetArtboardList(list), "GetArtboardList");
	ai::ArtboardID count = 0;
	sAIArtboard->GetCount(list, count);
	if (index < 0) { ai::ArtboardID active = 0; sAIArtboard->GetActive(list, active); index = (int) active; }
	AIRealRect r = {0, 0, 0, 0};
	bool ok = false;
	if (index >= 0 && index < count) {
		ai::ArtboardProperties props;
		if (!sAIArtboard->Init(props)) {
			ok = !sAIArtboard->GetArtboardProperties(list, index, props) && !sAIArtboard->GetPosition(props, r);
			sAIArtboard->Dispose(props);
		}
	}
	sAIArtboard->ReleaseArtboardList(list);
	if (!ok) Fail(kErrNotFound, "no artboard " + std::to_string(index));
	return r;
}

// Zoom and scroll so 'target' fills the window, with a margin.
json::Value ViewFit(const json::Value& p)
{
	Need(sAIDocumentView, "The document view suite");
	ActiveDocument();
	AIRealRect target;
	bool any = false;
	if (p.has("id") || p.has("ids")) {
		for (AIArtHandle a : ArtList(p)) {
			AIRealRect r;
			if (sAIArt->GetArtBounds(a, &r)) continue;
			if (!any) target = r;
			else { target.left = std::min(target.left, r.left); target.right = std::max(target.right, r.right);
				target.top = std::max(target.top, r.top); target.bottom = std::min(target.bottom, r.bottom); }
			any = true;
		}
		if (!any) Fail(kErrInvalidParams, "that art has no bounds");
	}
	else target = ArtboardBounds(p.get("artboard").isNumber() ? p.get("artboard").asInt() : -1);
	AIReal zoom = 1;
	AIRealRect visible;
	Check(sAIDocumentView->GetDocumentViewZoom(nullptr, &zoom), "GetDocumentViewZoom");
	Check(sAIDocumentView->GetDocumentViewBounds(nullptr, &visible), "GetDocumentViewBounds");
	double viewW = (visible.right - visible.left) * zoom, viewH = std::fabs(visible.top - visible.bottom) * zoom;
	double w = std::max(1.0, (double) (target.right - target.left)), h = std::max(1.0, std::fabs((double) (target.top - target.bottom)));
	double margin = p.num("margin", 0.9);
	double fit = std::max(0.0003, std::min(640.0, std::min(viewW / w, viewH / h) * margin));
	Check(sAIDocumentView->SetDocumentViewZoom(nullptr, (AIReal) fit), "SetDocumentViewZoom");
	AIRealPoint c;
	c.h = (target.left + target.right) / 2;
	c.v = (target.top + target.bottom) / 2;
	Check(sAIDocumentView->SetDocumentViewCenter(nullptr, &c), "SetDocumentViewCenter");
	return ViewGet(p);
}

json::Value ViewScreenshot(const json::Value& p)
{
	Need(sAIDocumentView, "The document view suite");
	ActiveDocument();
	std::string path = ReqStr(p, "path");
	if (!platform::IsAbsolutePath(path)) Fail(kErrInvalidParams, "'path' must be absolute");
	// ScreenShot needs a real view: unlike the other view calls it crashes on null (Windows, AI 30.8).
	AIDocumentViewHandle view = nullptr;
	Check(sAIDocumentView->GetNthDocumentView(0, &view), "GetNthDocumentView");
	Check(sAIDocumentView->ScreenShot(view, U(path)), "ScreenShot");
	json::Value v;
	v["path"] = path;
	v["format"] = "png";
	return v;
}

AIHitRequest HitRequest(const std::string& r)
{
	if (r == "all") return kAllHitRequest;
	if (r == "paint") return kPaintHitRequest;
	if (r == "text") return kTextHitRequest;
	if (r == "anchor") return kAllPHitRequest;
	if (r == "guide") return kGuideHitRequest;
	if (r == "selected") return kSelectedObjectHitRequest;
	if (r == "stroke") return kStrokeHitRequest;
	Fail(kErrInvalidParams, "'find' must be all, paint, text, anchor, guide, selected or stroke");
}

const char* HitTypeName(ai::int32 t)
{
	switch (t) {
	case kPHitType: return "anchor";
	case kInHitType: return "handleIn";
	case kOutHitType: return "handleOut";
	case kSegmentHitType: return "segment";
	case kFillHitType: return "fill";
	case kCenterHitType: return "center";
	case kTwoGuideHitType: return "guides";
	default: return "other";
	}
}

json::Value HitTest(const json::Value& p)
{
	Need(sAIHitTest, "The hit test suite");
	ActiveDocument();
	AIRealPoint at = Point(Required(p, "point"), "point");
	AIHitRef hit = nullptr;
	Check(sAIHitTest->HitTestEx(nullptr, &at, (AIReal) p.num("tolerance", 2), HitRequest(p.str("find", "all")), &hit), "HitTest");
	json::Value v;
	v["hit"] = false;
	if (hit && sAIHitTest->IsHit(hit)) {
		v["hit"] = true;
		v["type"] = HitTypeName(sAIHitTest->GetType(hit));
		if (AIArtHandle art = sAIHitTest->GetArt(hit)) v["art"] = ArtSummary(art, 0);
		if (AIArtHandle group = sAIHitTest->GetGroup(hit)) v["group"] = ArtId(group);
		v["point"] = PointJson(sAIHitTest->GetPoint(hit));
	}
	if (hit) sAIHitTest->Release(hit);
	return v;
}

} // namespace

void AddViewCommands(CommandTable& t)
{
	t["view.get"] = {"The window's view: zoom (1 = 100%), center, visible area, screen mode, preview / outline, guides, edges, grid, rulers.",
		Params({}), ViewGet, false};
	t["view.set"] = {"Change the view. Any of: zoom, center, screenMode, mode (preview | outline | pixel | overprint), guides, edges, grid, snapToGrid, transparencyGrid, rulers.",
		Params({{"zoom", "number - 1 = 100%"}, {"center", "[x, y]"}, {"screenMode", "normal | fullWithMenu | full"},
			{"mode", "preview | outline | pixel | overprint"}, {"guides", "boolean"}, {"edges", "boolean"}, {"grid", "boolean"},
			{"snapToGrid", "boolean"}, {"transparencyGrid", "boolean"}, {"rulers", "boolean"}}), ViewSet, false};
	t["view.fit"] = {"Zoom and scroll to fit art (id / ids) or an artboard (default: the active one) in the window.",
		Params({{"id", "string"}, {"ids", "string[]"}, {"artboard", "number"}, {"margin", "number - share of the window to fill (default 0.9)"}}), ViewFit, false};
	t["view.screenshot"] = {"Save what the document window shows right now as a PNG.", Params({{"path", "string - absolute .png path"}}), ViewScreenshot, false};
	t["hit.test"] = {"What's at a point (artwork coordinates): the art, and whether it's an anchor, handle, segment or fill.",
		Params({{"point", "[x, y]"}, {"tolerance", "number - points (default 2)"}, {"find", "all (default) | paint | text | anchor | guide | selected | stroke"}}), HitTest, false};
}

} // namespace slippy
