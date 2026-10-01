#include "IllustratorSDK.h"
#include "Overlay.h"
#include "SlippySuites.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace slippy {
namespace overlay {

namespace {

// Seconds. The cursor glides to the art, then the box, label and cursor
// fade out one after another once agents go quiet.
const double kGlide = 0.35;
const double kBoxIn = 0.15;
const double kBoxHold = 2.5, kBoxFade = 0.6;
const double kLabelHold = 3.5, kLabelFade = 0.6;
const double kCursorHold = 6.0, kCursorFade = 1.0;

const size_t kLabelChars = 48;

const AIRGBColor kGreen = {0x9292, 0xF6F6, 0x0707};   // SlippyGreen(), #92F607
const AIRGBColor kInk = {0x1A1A, 0x1A1A, 0x1A1A};     // text on the lime label (white wouldn't read)
const AIRGBColor kRed = {0xE0E0, 0x4545, 0x4545};
const AIRGBColor kWhite = {0xFFFF, 0xFFFF, 0xFFFF};

SPPluginRef gPluginRef = nullptr;
AIAnnotatorHandle gAnnotator = nullptr;
AITimerHandle gTimer = nullptr;
bool gTimerOn = false;

struct Touched {
	AIArtHandle art;
	AIRealRect bounds;   // when touched; used if the call deleted it
};
std::vector<Touched> gTouched;
int gBatch = 0;
std::string gBatchLine;
bool gBatchOk = true;

// What's on screen, in artwork coordinates so it follows scrolling and zoom.
struct Show {
	AIDocumentHandle doc = nullptr;
	bool hasBox = false;
	AIRealRect box{};
	bool hasCursor = false;
	AIRealPoint from{}, to{};   // the cursor's tip glides from -> to
	double start = 0;           // when the glide began
	std::string label;
	bool ok = true;
};
Show gShow;

AIRect gDrawn{0, 0, 0, 0};   // view area painted last time, to erase
bool gDrawnAny = false;

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

double Fade(double t, double hold, double fade)
{
	if (t <= hold) return 1;
	if (t >= hold + fade) return 0;
	return 1 - (t - hold) / fade;
}

AIDocumentHandle CurrentDoc()
{
	AIDocumentHandle doc = nullptr;
	ai::int32 count = 0;
	if (sAIDocumentList->Count(&count) || count == 0 || sAIDocument->GetDocument(&doc)) return nullptr;
	return doc;
}

AIDocumentViewHandle CurrentView()
{
	AIDocumentViewHandle view = nullptr;
	if (!sAIDocumentView || !CurrentDoc() || sAIDocumentView->GetNthDocumentView(0, &view)) return nullptr;
	return view;
}

AIPoint ToView(AIDocumentViewHandle view, AIRealPoint p)
{
	AIPoint v{0, 0};
	sAIDocumentView->ArtworkPointToViewPoint(view, &p, &v);
	return v;
}

AIRealPoint CursorAt(double now)
{
	double e = std::min(1.0, std::max(0.0, (now - gShow.start) / kGlide));
	e = 1 - std::pow(1 - e, 3);   // ease out
	AIRealPoint p;
	p.h = (AIReal) (gShow.from.h + (gShow.to.h - gShow.from.h) * e);
	p.v = (AIReal) (gShow.from.v + (gShow.to.v - gShow.from.v) * e);
	return p;
}

// The box corner that's bottom-right on screen: where the cursor rests.
AIRealPoint CursorSpot(const AIRealRect& box)
{
	AIRealPoint corners[4] = {{box.left, box.top}, {box.right, box.top}, {box.left, box.bottom}, {box.right, box.bottom}};
	AIDocumentViewHandle view = CurrentView();
	if (!view) return corners[3];
	int best = 0;
	long bestScore = 0;
	for (int i = 0; i < 4; i++) {
		AIPoint v = ToView(view, corners[i]);
		if (i == 0 || v.h + v.v > bestScore) { best = i; bestScore = v.h + v.v; }
	}
	return corners[best];
}

AIRealPoint ViewCenter()
{
	AIRealPoint c{0, 0};
	AIDocumentViewHandle view = CurrentView();
	AIRealRect b;
	if (view && !sAIDocumentView->GetDocumentViewBounds(view, &b)) {
		c.h = (b.left + b.right) / 2;
		c.v = (b.top + b.bottom) / 2;
	}
	return c;
}

// First line only (a failure comes as "short line\nfull error"), capped.
std::string LabelText(const std::string& line)
{
	std::string s = line.substr(0, line.find('\n'));
	size_t chars = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (((unsigned char) s[i] & 0xC0) == 0x80) continue;   // inside a UTF-8 character
		if (++chars > kLabelChars) return s.substr(0, i) + "\xE2\x80\xA6";
	}
	return s;
}

// ------------------------------------------------------------------ frame

struct Frame {
	bool box = false;
	AIRect boxRect{0, 0, 0, 0};
	double boxAlpha = 0;
	bool cursor = false;
	AIPoint tip{0, 0};
	double cursorAlpha = 0, labelAlpha = 0;
};

Frame Compute(AIDocumentViewHandle view, double now)
{
	Frame f;
	if (!view || !gShow.doc || gShow.doc != CurrentDoc()) return f;
	double t = now - (gShow.start + kGlide);   // < 0 while gliding
	if (gShow.hasBox) {
		AIPoint a = ToView(view, {gShow.box.left, gShow.box.top});
		AIPoint b = ToView(view, {gShow.box.right, gShow.box.bottom});
		int pad = 5;   // between the art and the box
		f.boxRect.left = std::min(a.h, b.h) - pad;
		f.boxRect.right = std::max(a.h, b.h) + pad;
		f.boxRect.top = std::min(a.v, b.v) - pad;
		f.boxRect.bottom = std::max(a.v, b.v) + pad;
		f.boxAlpha = t < 0 ? std::max(0.0, 1 + t / kBoxIn) : Fade(t, kBoxHold, kBoxFade);
		f.box = f.boxAlpha > 0;
	}
	if (gShow.hasCursor) {
		f.tip = ToView(view, CursorAt(now));
		f.cursorAlpha = Fade(t, kCursorHold, kCursorFade);
		f.labelAlpha = gShow.label.empty() ? 0 : Fade(t, kLabelHold, kLabelFade);
		f.cursor = f.cursorAlpha > 0;
	}
	return f;
}

// Everything a frame could paint (the label's width is a generous guess).
AIRect Extent(const Frame& f)
{
	AIRect r{0, 0, 0, 0};
	bool any = false;
	auto add = [&](const AIRect& a) {
		if (!any) r = a;
		else {
			r.left = std::min(r.left, a.left);
			r.top = std::min(r.top, a.top);
			r.right = std::max(r.right, a.right);
			r.bottom = std::max(r.bottom, a.bottom);
		}
		any = true;
	};
	if (f.box) add({f.boxRect.left - 6, f.boxRect.top - 6, f.boxRect.right + 6, f.boxRect.bottom + 6});
	if (f.cursor) add({f.tip.h - 4, f.tip.v - 4, f.tip.h + 420, f.tip.v + 56});
	return r;
}

AIRect Union(const AIRect& a, const AIRect& b)
{
	return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}

bool Empty(const AIRect& r) { return r.right <= r.left || r.bottom <= r.top; }

// Erase what was drawn and mark where the next frame goes.
void Invalidate(double now)
{
	if (!gAnnotator || !sAIAnnotator) return;
	AIDocumentViewHandle view = CurrentView();
	if (!view) return;
	AIRect r = Extent(Compute(view, now));
	if (gDrawnAny) r = Empty(r) ? gDrawn : Union(r, gDrawn);
	if (!Empty(r)) sAIAnnotator->InvalAnnotationRect(view, &r);
}

// True while something is changing on screen (holds need no frames).
bool Changing(double now)
{
	if (!gShow.doc) return false;
	double t = now - (gShow.start + kGlide);
	auto fading = [t](double hold, double fade) { return t >= hold && t <= hold + fade + 0.05; };
	return t < 0.05 || fading(kBoxHold, kBoxFade) || fading(kLabelHold, kLabelFade) || fading(kCursorHold, kCursorFade);
}

bool Finished(double now) { return !gShow.doc || now > gShow.start + kGlide + kCursorHold + kCursorFade + 0.05; }

void StartTimer()
{
	// Added on first use, like the calls timer: not during startup.
	if (!gTimer && SlippyAddTimer(gPluginRef, "Slippy Overlay", 1, &gTimer, false)) gTimer = nullptr;
	if (gTimer && !gTimerOn && !SlippySetTimerActive(gTimer, true)) gTimerOn = true;
}

bool EnsureAnnotator()
{
	if (gAnnotator) return true;
	if (!gPluginRef || !sAIAnnotator || !sAIAnnotatorDrawer || !sAIDocumentView) return false;
	if (sAIAnnotator->AddAnnotator(gPluginRef, "Slippy Overlay", &gAnnotator) || !gAnnotator) {
		gAnnotator = nullptr;
		return false;
	}
	sAIAnnotator->SetAnnotatorActive(gAnnotator, true);
	return true;
}

void Present(const std::string& line, bool ok)
{
	if (!EnsureAnnotator()) { gTouched.clear(); return; }
	double now = Now();
	Invalidate(now);

	bool any = false;
	AIRealRect u{};
	for (const Touched& t : gTouched) {
		AIRealRect b = t.bounds;
		AIRealRect live;
		if (sAIArt->ValidArt(t.art, true) && !sAIArt->GetArtBounds(t.art, &live)) b = live;
		if (b.left == b.right && b.top == b.bottom) continue;   // nothing to see (an empty group)
		AIReal l = std::min(b.left, b.right), r = std::max(b.left, b.right);
		AIReal hi = std::max(b.top, b.bottom), lo = std::min(b.top, b.bottom);
		if (!any) u = {l, hi, r, lo};
		else {
			u.left = std::min(u.left, l);
			u.right = std::max(u.right, r);
			u.top = std::max(u.top, hi);
			u.bottom = std::min(u.bottom, lo);
		}
		any = true;
	}
	gTouched.clear();

	AIDocumentHandle doc = CurrentDoc();
	if (!doc) {   // nothing open: nothing to point at
		gShow = Show();
		return;
	}
	bool had = gShow.hasCursor && gShow.doc == doc && !Finished(now);
	AIRealPoint here = had ? CursorAt(now) : AIRealPoint{0, 0};
	AIRealPoint target = any ? CursorSpot(u) : had ? here : ViewCenter();
	gShow.doc = doc;
	gShow.hasBox = any;
	if (any) gShow.box = u;
	gShow.hasCursor = true;
	gShow.from = had ? here : target;   // a fresh cursor appears in place
	gShow.to = target;
	gShow.start = now;
	gShow.label = LabelText(line);
	gShow.ok = ok;
	Invalidate(now);
	StartTimer();
}

// ------------------------------------------------------------------ drawing

void Draw(AIAnnotatorMessage* m)
{
	AIAnnotatorDrawer* d = m->drawer;
	if (!d || !sAIAnnotatorDrawer || !sAIDocumentView) return;
	Frame f = Compute(m->view, Now());
	gDrawn = Extent(f);
	gDrawnAny = f.box || f.cursor;
	if (!gDrawnAny) return;
	const AIAnnotatorDrawerSuite& D = *sAIAnnotatorDrawer;
	D.Save(d);
	D.SetLineDashed(d, false);

	if (f.box) {
		const AIRect& b = f.boxRect;
		D.SetColor(d, kGreen);
		D.SetOpacity(d, (AIReal) (0.08 * f.boxAlpha));
		D.DrawRect(d, b, true);
		// Outline and handles as filled shapes: the drawer strokes rectangles
		// 1 px wide whatever the line width.
		D.SetOpacity(d, (AIReal) f.boxAlpha);
		const int w = 2;
		D.DrawRect(d, {b.left, b.top, b.right, b.top + w}, true);
		D.DrawRect(d, {b.left, b.bottom - w, b.right, b.bottom}, true);
		D.DrawRect(d, {b.left, b.top, b.left + w, b.bottom}, true);
		D.DrawRect(d, {b.right - w, b.top, b.right, b.bottom}, true);
		AIPoint corners[4] = {{b.left, b.top}, {b.right, b.top}, {b.left, b.bottom}, {b.right, b.bottom}};
		for (AIPoint c : corners) {
			c.h += c.h == b.left ? w / 2 : -w / 2;   // centered on the outline
			c.v += c.v == b.top ? w / 2 : -w / 2;
			D.SetColor(d, kGreen);
			D.DrawRect(d, {c.h - 5, c.v - 5, c.h + 5, c.v + 5}, true);
			D.SetColor(d, kWhite);
			D.DrawRect(d, {c.h - 3, c.v - 3, c.h + 3, c.v + 3}, true);
		}
	}

	if (f.cursor) {
		const AIPoint& t = f.tip;
		// Slippy's cursor: a plain triangle, its tip on the art.
		static const int shape[][2] = {{0, 0}, {18, 7}, {7, 18}};
		AIPoint tri[3];
		for (int i = 0; i < 3; i++) tri[i] = {t.h + shape[i][0], t.v + shape[i][1]};
		D.SetOpacity(d, (AIReal) f.cursorAlpha);
		D.SetColor(d, kGreen);
		D.DrawPolygon(d, tri, 3, true);
		D.SetColor(d, kWhite);
		D.SetLineWidth(d, 2);
		D.DrawPolygon(d, tri, 3, false);

		if (f.labelAlpha > 0) {
			ai::UnicodeString text = ai::UnicodeString::FromUTF8(gShow.label);
			D.SetFontPreset(d, kAIAFMedium);
			D.SetFontSize(d, 12);
			AIRect tb{0, 0, 0, 0};
			D.GetTextBounds(d, text, nullptr, true, tb, false);
			const int h = std::max(22, (int) (tb.bottom - tb.top) + 8);
			int x = t.h + 16, y = t.v + 18, w = (tb.right - tb.left) + 20;
			AIRect pill = {x, y, x + std::max(w, h), y + h};
			D.SetOpacity(d, (AIReal) (f.labelAlpha * f.cursorAlpha));
			D.SetColor(d, gShow.ok ? kGreen : kRed);
			D.DrawRect(d, {pill.left + h / 2, pill.top, pill.right - h / 2, pill.bottom}, true);
			D.DrawEllipse(d, {pill.left, pill.top, pill.left + h, pill.bottom}, true);
			D.DrawEllipse(d, {pill.right - h, pill.top, pill.right, pill.bottom}, true);
			D.SetColor(d, gShow.ok ? kInk : kWhite);
			D.DrawTextAligned(d, text, kAICenter, kAIMiddle, pill, false);
		}
	}

	D.SetOpacity(d, 1);
	D.Restore(d);
}

} // namespace

// ------------------------------------------------------------------ public

void Init(SPPluginRef plugin)
{
	gPluginRef = plugin;
	EnsureAnnotator();
}

void Shutdown()
{
	if (gTimer && gTimerOn) SlippySetTimerActive(gTimer, false);
	gTimerOn = false;
	gShow = Show();
	gTouched.clear();
}

void BeginCall()
{
	if (!gBatch) gTouched.clear();
}

void Touch(AIArtHandle art)
{
	Touched t{art, {}};
	if (art && !sAIArt->GetArtBounds(art, &t.bounds)) gTouched.push_back(t);
}

void EndCall(const std::string& line, bool ok)
{
	if (gBatch) {
		gBatchLine = line;
		gBatchOk = gBatchOk && ok;
		return;
	}
	Present(line, ok);
}

void BeginBatch()
{
	if (gBatch++ == 0) {
		gTouched.clear();
		gBatchLine.clear();
		gBatchOk = true;
	}
}

void EndBatch()
{
	if (gBatch > 0 && --gBatch == 0) Present(gBatchLine, gBatchOk);
}

bool IsTimer(AITimerHandle timer) { return gTimer && timer == gTimer; }

void Tick()
{
	double now = Now();
	if (Finished(now)) {
		Invalidate(now);   // erase the last frame
		gShow = Show();
		gDrawnAny = false;
		if (gTimer) SlippySetTimerActive(gTimer, false);
		gTimerOn = false;
		return;
	}
	if (Changing(now)) Invalidate(now);
}

AIErr Annotate(const char* selector, AIAnnotatorMessage* message)
{
	if (!strcmp(selector, kSelectorAIDrawAnnotation)) Draw(message);
	else if (!strcmp(selector, kSelectorAIInvalAnnotation) && gDrawnAny && sAIAnnotator)
		sAIAnnotator->InvalAnnotationRect(message->view, &gDrawn);
	return kNoErr;
}

} // namespace overlay
} // namespace slippy
