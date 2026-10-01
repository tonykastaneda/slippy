// The Slippy panel on Windows: the same panel and the same Slippy as
// SlippyPanelView.mm, drawn with GDI+ into a child window of Illustrator's
// docked panel. Core Animation isn't there to lean on, so a small engine
// below plays the same keyframes, curves and springs (Track), and one frame
// timer drives everything. Geometry, timings and curves match the Mac file
// line for line; read that one for the why of each move. Main thread only.

#include "IllustratorSDK.h"
#include "SlippyPanel.h"
#include "SlippySuites.h"
#include "SlippyID.h"
#include "AIUITheme.h"
#include "Platform.h"
#include "FrogShape.h"
#include "AgentLogo.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }   // gdiplus.h wants them; NOMINMAX took the macros
#include <gdiplus.h>

#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" SPBasicSuite* sSPBasic;

namespace {

using namespace Gdiplus;

const double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ time + curves

double Now()
{
	static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double) t.QuadPart / (double) freq.QuadPart;
}

double Random(double lo, double hi)
{
	unsigned char b[4];
	slippy::platform::RandomBytes(b, sizeof b);
	unsigned int n = ((unsigned) b[0] << 24 | (unsigned) b[1] << 16 | (unsigned) b[2] << 8 | b[3]) % 10001;
	return lo + (hi - lo) * (n / 10000.0);
}

// A CSS / Core Animation cubic-bezier timing curve.
struct Curve {
	double x1, y1, x2, y2;
	double Bezier(double a, double b, double t) const { double u = 1 - t; return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t; }
	double operator()(double x) const
	{
		if (x <= 0) return 0;
		if (x >= 1) return 1;
		double lo = 0, hi = 1, t = x;
		for (int i = 0; i < 40; i++) {   // x(t) is monotonic: bisect
			double cx = Bezier(x1, x2, t);
			if (std::fabs(cx - x) < 1e-6) break;
			if (cx < x) lo = t; else hi = t;
			t = (lo + hi) / 2;
		}
		return Bezier(y1, y2, t);
	}
};
const Curve kLinear = {0, 0, 1, 1};
const Curve kEaseOut = {0.22, 1, 0.36, 1};      // quick start, soft landing
const Curve kEaseIn = {0.55, 0, 1, 0.45};       // gravity
const Curve kEaseInOut = {0.65, 0, 0.35, 1};
const Curve kOvershoot = {0.34, 1.56, 0.64, 1}; // a little past, then back
const Curve kCAEaseOut = {0, 0, 0.58, 1};       // kCAMediaTimingFunctionEaseOut
const Curve kCAEaseInOut = {0.42, 0, 0.58, 1};  // kCAMediaTimingFunctionEaseInEaseOut
const Curve kZDrift = {0.3, 0.1, 0.6, 1};

// Keyframes: values at times (0..1), one curve per gap.
double Keyframes(const std::vector<double>& values, const std::vector<double>& times, const std::vector<Curve>& curves, double p)
{
	if (values.empty()) return 0;
	if (p <= times.front()) return values.front();
	for (size_t i = 0; i + 1 < values.size(); i++) {
		if (p <= times[i + 1]) {
			double span = times[i + 1] - times[i];
			double local = span > 0 ? (p - times[i]) / span : 1;
			double eased = i < curves.size() ? curves[i](local) : local;
			return values[i] + (values[i + 1] - values[i]) * eased;
		}
	}
	return values.back();
}

std::vector<double> Even(size_t n)
{
	std::vector<double> t;
	for (size_t i = 0; i < n; i++) t.push_back(n > 1 ? (double) i / (n - 1) : 0);
	return t;
}

// ------------------------------------------------------------------ Track
// One animatable number, like a CALayer key path: a model value plus the
// animations playing on it, by key. The last-added active one wins; one that
// ends goes away (unless it holds its last value), leaving the model value.

struct Anim {
	std::string key;
	double start = 0, duration = 0;
	bool hold = false;   // fillMode forwards
	std::function<double(double elapsed)> value;
};

struct Track {
	double model = 0;
	std::vector<Anim> anims;

	explicit Track(double m = 0) : model(m) {}

	double Value(double now)
	{
		double v = model;
		for (size_t i = 0; i < anims.size();) {
			Anim& a = anims[i];
			double e = now - a.start;
			if (e >= a.duration && !a.hold) { anims.erase(anims.begin() + (long) i); continue; }
			if (e >= 0) v = a.value(std::min(e, a.duration));
			i++;
		}
		return v;
	}
	double Value() { return Value(Now()); }
	bool Has(const std::string& key) { Value(); for (auto& a : anims) if (a.key == key) return true; return false; }
	bool Animating() { Value(); return !anims.empty(); }
	void Remove(const std::string& key) { anims.erase(std::remove_if(anims.begin(), anims.end(), [&](const Anim& a) { return a.key == key; }), anims.end()); }
	Anim& Add(const std::string& key, double duration, std::function<double(double)> value)
	{
		Remove(key);
		Anim a;
		a.key = key;
		a.start = Now();
		a.duration = duration;
		a.value = std::move(value);
		anims.push_back(std::move(a));
		return anims.back();
	}
};

// Keyframes from wherever the track is now through 'values'. times has one
// more entry than values; curves one fewer.
Anim& Play(Track& t, const std::string& key, std::vector<double> values, std::vector<double> times, std::vector<Curve> curves, double duration)
{
	values.insert(values.begin(), t.Value());
	return t.Add(key, duration, [=](double e) { return Keyframes(values, times, curves, duration > 0 ? e / duration : 1); });
}

// A spring (mass 1) from where the track is now to 'to', which becomes the model value.
void Spring(Track& t, const std::string& key, double to, double stiffness, double damping)
{
	double from = t.Value();
	t.model = to;
	double w0 = std::sqrt(stiffness), zeta = damping / (2 * w0);
	double decay = zeta * w0;
	double settle = std::min(4.0, std::log(1000.0) / std::max(decay, 0.1));
	t.Add(key, settle, [=](double e) {
		double x;
		if (zeta < 1) {
			double wd = w0 * std::sqrt(1 - zeta * zeta);
			x = std::exp(-decay * e) * (std::cos(wd * e) + decay / wd * std::sin(wd * e));
		}
		else x = std::exp(-w0 * e) * (1 + w0 * e);
		return to + (from - to) * x;
	});
}

// ------------------------------------------------------------------ scheduler

struct Scheduled { int id; double due; std::function<void()> work; };
std::vector<Scheduled> gScheduled;
int gNextScheduled = 1;

int After(double seconds, std::function<void()> work)
{
	gScheduled.push_back({gNextScheduled, Now() + seconds, std::move(work)});
	return gNextScheduled++;
}

void Cancel(int& id)
{
	if (!id) return;
	int target = id;
	gScheduled.erase(std::remove_if(gScheduled.begin(), gScheduled.end(), [&](const Scheduled& s) { return s.id == target; }), gScheduled.end());
	id = 0;
}

void RunDue()
{
	double now = Now();
	std::vector<Scheduled> due;
	for (size_t i = 0; i < gScheduled.size();) {
		if (gScheduled[i].due <= now) { due.push_back(std::move(gScheduled[i])); gScheduled.erase(gScheduled.begin() + (long) i); }
		else i++;
	}
	for (auto& s : due) s.work();
}

// ------------------------------------------------------------------ look

using slippy::AgentLogo;
#include "AgentLogos.inc"

const char* kGroups[] = {"document", "layer", "art", "shape", "path", "text", "menu", "action", "history", "app"};
const int kGroupCount = sizeof kGroups / sizeof kGroups[0];
const int kFeedRows = 8;
const double kPad = 16;
const double kRowH = 18, kRowGap = 4, kHandleH = 18, kTerminalH = 300;

enum class Kind { Read, Edit, Error };

bool gDark = true;

struct RGBA { double r, g, b, a; };
RGBA Rgb(int rgb, double a = 1) { return {((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, a}; }
RGBA Mix(RGBA x, RGBA y, double t) { return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t}; }
Color C(RGBA c, double alpha = 1)
{
	auto b = [](double v) { return (BYTE) std::max(0.0, std::min(255.0, std::round(v * 255))); };
	return Color(b(c.a * alpha), b(c.r), b(c.g), b(c.b));
}

// The macOS system colors the Mac panel uses, light and dark.
RGBA ReadColor() { return gDark ? Rgb(0x40C8E0) : Rgb(0x30B0C7); }
RGBA EditColor() { return gDark ? Rgb(0xFF9F0A) : Rgb(0xFF9500); }
RGBA ErrorColor() { return gDark ? Rgb(0xFF453A) : Rgb(0xFF3B30); }
RGBA KindColor(Kind k) { return k == Kind::Error ? ErrorColor() : k == Kind::Edit ? EditColor() : ReadColor(); }
RGBA SlippyGreen() { return Rgb(0x92F607); }
RGBA LabelColor() { return gDark ? RGBA{1, 1, 1, 0.85} : RGBA{0, 0, 0, 0.85}; }
RGBA SecondaryLabel() { return gDark ? RGBA{1, 1, 1, 0.55} : RGBA{0, 0, 0, 0.5}; }
RGBA TertiaryLabel() { return gDark ? RGBA{1, 1, 1, 0.25} : RGBA{0, 0, 0, 0.26}; }
RGBA Black() { return {0, 0, 0, 1}; }

// Windows' "Show animations in Windows" off = Reduce Motion, unless
// SLIPPY_FULL_MOTION=1 asks for the full bounce anyway.
bool ReduceMotion()
{
	static const bool full = slippy::platform::EnvFlag("SLIPPY_FULL_MOTION", "1");
	if (full) return false;
	BOOL animate = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
	return !animate;
}
double Motion() { return ReduceMotion() ? 0.3 : 1.0; }

std::wstring W(const std::string& s)
{
	if (s.empty()) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
	std::wstring w((size_t) n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n);
	return w;
}

// Illustrator's panel background, and whether its theme is dark.
AIUIThemeSuite* Theme()
{
	static AIUIThemeSuite* suite = nullptr;
	static bool tried = false;
	if (!tried) {
		tried = true;
		const void* s = nullptr;
		if (sSPBasic && !sSPBasic->AcquireSuite(kAIUIThemeSuite, kAIUIThemeSuiteVersion, &s)) suite = (AIUIThemeSuite*) s;
	}
	return suite;
}

RGBA Background()
{
	if (AIUIThemeSuite* t = Theme()) {
		AIUIThemeColor c;
		if (!t->GetUIThemeColor(kAIUIThemeSelectorPanel, kAIUIComponentColorBackground, c)) {
			gDark = t->IsUIThemeDark();
			return {c.red, c.green, c.blue, 1};
		}
	}
	gDark = true;
	return Rgb(0x323232);
}

// ------------------------------------------------------------------ drawing helpers

void RoundRect(GraphicsPath& p, RectF r, REAL radius)
{
	REAL d = std::min(radius * 2, std::min(r.Width, r.Height));
	if (d <= 0) { p.AddRectangle(r); return; }
	p.AddArc(r.X, r.Y, d, d, 180, 90);
	p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
	p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
	p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
	p.CloseFigure();
}

enum Align { AlignLeft, AlignCenter, AlignRight };

// Fonts are made once each; ClearFonts before GDI+ shuts down.
std::map<std::wstring, std::unique_ptr<Font>> gFonts;

Font& CachedFont(const wchar_t* family, REAL size, int style)
{
	std::wstring key = std::wstring(family) + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(style);
	auto it = gFonts.find(key);
	if (it != gFonts.end()) return *it->second;
	FontFamily fam(family);
	bool have = fam.IsAvailable() != FALSE;
	// Segoe UI Black is Windows 10's heavy weight; without it, bold.
	std::unique_ptr<Font> font(new Font(have ? family : L"Segoe UI", size, have ? style : (style | FontStyleBold), UnitPixel));
	return *(gFonts[key] = std::move(font));
}

void ClearFonts() { gFonts.clear(); }

void Text(Graphics& g, const std::wstring& s, RectF r, REAL size, int style, RGBA color, Align align, double alpha = 1)
{
	Font& font = CachedFont(L"Segoe UI", size, style);
	StringFormat f;
	f.SetAlignment(align == AlignLeft ? StringAlignmentNear : align == AlignCenter ? StringAlignmentCenter : StringAlignmentFar);
	f.SetLineAlignment(StringAlignmentCenter);
	f.SetTrimming(StringTrimmingEllipsisCharacter);
	f.SetFormatFlags(StringFormatFlagsNoWrap);
	SolidBrush brush(C(color, alpha));
	g.DrawString(s.c_str(), -1, &font, r, &f, &brush);
}

REAL TextWidth(Graphics& g, const std::wstring& s, REAL size, int style)
{
	Font& font = CachedFont(L"Segoe UI", size, style);
	RectF box;
	g.MeasureString(s.c_str(), -1, &font, PointF(0, 0), &box);
	return box.Width;
}

// ------------------------------------------------------------------ Slippy
// Units of D, the body's diameter; y grows upward (as in the Mac layers).

enum class Face { Sleep, Happy, Awake, Ouch };
enum class Mood { Asleep, Waking, Working, Dozing };

using slippy::kEyeX;   // the shape lives in FrogShape.h
using slippy::kEyeY;
using slippy::kBumpR;
const double kBumpFollow = 0.35;
double EyeLift(Face f) { return f == Face::Sleep ? -0.03 : f == Face::Happy ? 0.02 : 0.0; }
double EyeWidth(Face f) { return f == Face::Sleep ? 0.045 : f == Face::Awake ? 0 : 0.055; }

struct ZSpot { double x, y, size; };
const ZSpot kZs[] = {{-0.56, 0.3, 0.13}, {-0.64, 0.5, 0.16}, {-0.68, 0.72, 0.21}};
const double kClickAwake = 20;

struct Ripple { double start; RGBA color; };

// A Z caught mid-drift when the snoring stopped, fading out where it was.
struct FrozenZ { double x, y, scale, angle, opacity; };

class Mascot {
public:
	double sleepAfter = 6;
	bool shownOnScreen = true;   // the panel's window is up

	Mascot()
		: fAlpha(1), fRootX(0), fRootY(0), fRot(0), fSquashX(1), fSquashY(1), fBreathX(1), fBreathY(1),
		  fEyesX(0), fEyesY(0), fBumpsX(0), fBumpsY(0), fFlash(0)
	{
		fLid[0].model = fLid[1].model = 1;
	}

	void Layout(double width, double height)
	{
		double oldD = fD;
		fW = width; fH = height;
		fD = std::floor(std::min(width, height) * 0.64);
		fCenterX = width - fD / 2 - width * 0.06;
		fCenterY = fD / 2 + height * 0.05;
		fEyesX.model = fLook[0] * fD; fEyesY.model = fLook[1] * fD;
		fBumpsX.model = fLook[0] * kBumpFollow * fD; fBumpsY.model = fLook[1] * kBumpFollow * fD;
		if (fD != oldD) { fSnoring = false; Snore(WantsSnore()); }
	}

	void Start() { EnterMood(fMood); }

	// The frog's box, y-up in the mascot's own space.
	bool Hit(double x, double yUp) const
	{
		return x >= fCenterX - fD / 2 && x <= fCenterX + fD / 2 && yUp >= fCenterY - fD / 2 && yUp <= fCenterY + fD / 2;
	}

	void SetAvailable(bool available, bool paused)
	{
		if (available == fAvailable && paused == fPaused) return;
		fAvailable = available;
		fPaused = paused;
		double target = available && !paused ? 1.0 : 0.55;
		Play(fAlpha, "alpha", {target}, {0, 1}, {kCAEaseInOut}, 0.35);
		fAlpha.model = target;
		if (!available || paused) {
			Cancel(fSleepTimer);
			LookAt(0, 0);
			ShowFace(Face::Sleep);
		}
		EnterMood((!available || paused) ? Mood::Asleep : fMood);
	}

	void SetBusy(double busy) { fBusy = busy; }

	void CallArrived(Kind kind)
	{
		RippleOut(KindColor(kind));
		RestartSleepTimer();
		if (fMood == Mood::Asleep || fMood == Mood::Dozing) { WakeUp(kind != Kind::Error); return; }
		if (fMood != Mood::Working) return;
		if (kind == Kind::Error) { Wince(); return; }
		LookAt(Random(-0.12, 0.12), Random(-0.05, 0.08));
		if (kind == Kind::Edit && !fRootY.Has("hop")) Hop(0.05, 0.06, 0.4);
		ScheduleIdle();
	}

	void Click()
	{
		if (!fAvailable || fPaused) { Snuffle(); return; }
		fAwakeUntil = Now() + kClickAwake;
		RestartSleepTimer();
		if (fMood == Mood::Asleep || fMood == Mood::Dozing) { WakeUp(true); return; }
		if (fMood != Mood::Working || fRootY.Has("hop")) return;
		int mood = fMoodGen;
		ShowFace(Face::Happy);
		Hop(0.1, 0.1, 0.5);
		After(0.7, [this, mood] { if (fMoodGen == mood && fShown == Face::Happy) ShowFace(Face::Awake); });
	}

	void StopTimers() { Cancel(fIdleTimer); Cancel(fSleepTimer); }

	// Draws into (left, top, width, height) of the panel, in DIPs.
	void Draw(Graphics& g, double left, double top)
	{
		double now = Now();
		double alpha = fAlpha.Value(now);
		GraphicsState outer = g.Save();
		// Mascot space: y up from the box's bottom-left.
		g.TranslateTransform((REAL) left, (REAL) (top + fH));
		g.ScaleTransform(1, -1);

		// Ripples, under Slippy: Slippy's own outline, growing from the middle of it.
		static const std::vector<slippy::FrogPoint> outline = slippy::FrogOutline();
		for (size_t i = 0; i < fRipples.size();) {
			double p = (now - fRipples[i].start) / 0.75;
			if (p >= 1) { fRipples.erase(fRipples.begin() + (long) i); continue; }
			double e = kEaseOut(p), s = 1 + 0.8 * Motion() * e, opacity = 0.9 * (1 - e);
			std::vector<PointF> pts;
			double mx = slippy::kFrogMidX, my = slippy::kFrogMidY;
			double ox = fCenterX - fD / 2, oy = fCenterY - fD / 2;
			for (auto& q : outline)
				pts.push_back(PointF((REAL) (ox + (mx + (q.x - mx) * s) * fD), (REAL) (oy + (my + (q.y - my) * s) * fD)));
			Pen pen(C(fRipples[i].color, opacity * alpha), (REAL) (2 * s));
			pen.SetLineJoin(LineJoinRound);
			g.DrawPolygon(&pen, pts.data(), (INT) pts.size());
			i++;
		}

		// root (hop, shake, tilt) > squash > breath, all pivoting on the ground under Slippy.
		GraphicsState body = g.Save();
		g.TranslateTransform((REAL) (fCenterX + fRootX.Value(now)), (REAL) (fCenterY - fD / 2 + fRootY.Value(now)));
		g.RotateTransform((REAL) (fRot.Value(now) * 180 / kPi));
		g.ScaleTransform((REAL) (fSquashX.Value(now) * fBreathX.Value(now)), (REAL) (fSquashY.Value(now) * fBreathY.Value(now)));
		g.TranslateTransform((REAL) (-fD / 2), 0);

		RGBA skin = Mix(SlippyGreen(), ErrorColor(), fFlash.Value(now));
		SolidBrush skinBrush(C(skin, alpha));
		g.FillEllipse(&skinBrush, (REAL) ((slippy::kHeadCX - slippy::kHeadRX) * fD), (REAL) ((slippy::kHeadCY - slippy::kHeadRY) * fD),
			(REAL) (2 * slippy::kHeadRX * fD), (REAL) (2 * slippy::kHeadRY * fD));
		double bx = fBumpsX.Value(now), by = fBumpsY.Value(now);
		for (double side : {-1.0, 1.0})
			g.FillEllipse(&skinBrush, (REAL) ((0.5 + side * kEyeX - kBumpR) * fD + bx), (REAL) ((kEyeY - kBumpR) * fD + by),
				(REAL) (2 * kBumpR * fD), (REAL) (2 * kBumpR * fD));

		double ex = fEyesX.Value(now), ey = fEyesY.Value(now);
		for (int i = 0; i < 2; i++) {
			GraphicsState eye = g.Save();
			g.TranslateTransform((REAL) ((0.5 + (i ? kEyeX : -kEyeX)) * fD + ex), (REAL) ((kEyeY + EyeLift(fDrawn)) * fD + ey));
			g.ScaleTransform(1, (REAL) std::max(0.001, fLid[i].Value(now)));
			DrawEye(g, fDrawn, i == 1, alpha);
			g.Restore(eye);
		}
		g.Restore(body);
		g.Restore(outer);

		DrawZs(g, left, top, now, alpha);
	}

private:
	double fW = 0, fH = 0, fD = 0, fCenterX = 0, fCenterY = 0;
	double fLook[2] = {0, 0};
	Track fAlpha, fRootX, fRootY, fRot, fSquashX, fSquashY, fBreathX, fBreathY;
	Track fEyesX, fEyesY, fBumpsX, fBumpsY, fFlash;
	Track fLid[2];
	std::vector<Ripple> fRipples;
	Face fShown = Face::Sleep, fDrawn = Face::Sleep;
	Mood fMood = Mood::Asleep;
	int fBlink = 0, fMoodGen = 0;
	double fBusy = 0;
	bool fAvailable = true, fPaused = false;
	int fIdleTimer = 0, fSleepTimer = 0;
	double fAwakeUntil = 0;
	bool fSnoring = false;
	double fSnoreStart = 0;
	bool fSnoreInPlace = false;
	double fStopStart = -1;
	FrozenZ fFrozen[3] = {};

	void DrawEye(Graphics& g, Face f, bool right, double alpha)
	{
		double D = fD;
		if (f == Face::Awake) {   // a solid black eye
			SolidBrush b(C(Black(), alpha));
			g.FillEllipse(&b, (REAL) (-0.087 * D), (REAL) (-0.095 * D), (REAL) (0.174 * D), (REAL) (0.19 * D));
			return;
		}
		Pen pen(C(Black(), alpha), (REAL) (EyeWidth(f) * D));
		pen.SetLineCap(LineCapFlat, LineCapFlat, DashCapFlat);
		pen.SetLineJoin(LineJoinMiter);
		GraphicsPath p;
		if (f == Face::Sleep) {   // a soft ‿: the quadratic (end, (0, -0.07), end) as a cubic
			PointF a((REAL) (-0.095 * D), (REAL) (0.012 * D)), b((REAL) (0.095 * D), (REAL) (0.012 * D)), c(0, (REAL) (-0.07 * D));
			p.AddBezier(a, PointF(a.X + (c.X - a.X) * 2 / 3, a.Y + (c.Y - a.Y) * 2 / 3), PointF(b.X + (c.X - b.X) * 2 / 3, b.Y + (c.Y - b.Y) * 2 / 3), b);
		}
		else if (f == Face::Happy) {
			PointF pts[] = {PointF((REAL) (-0.075 * D), (REAL) (-0.06 * D)), PointF(0, (REAL) (0.07 * D)), PointF((REAL) (0.075 * D), (REAL) (-0.06 * D))};
			p.AddLines(pts, 3);
		}
		else {
			double s = right ? -1 : 1;
			PointF pts[] = {PointF((REAL) (-0.06 * D * s), (REAL) (0.065 * D)), PointF((REAL) (0.06 * D * s), 0), PointF((REAL) (-0.06 * D * s), (REAL) (-0.065 * D))};
			p.AddLines(pts, 3);
		}
		g.DrawPath(&pen, &p);
	}

	// ---- Z's

	bool WantsSnore() const { return fMood == Mood::Asleep && fAvailable && !fPaused && shownOnScreen && fD >= 1; }

	// Where each Z is at 'now' while snoring, in mascot space.
	FrozenZ ZAt(int i, double now) const
	{
		FrozenZ z = {};
		double big = kZs[2].size;
		if (fSnoreInPlace) {
			z.x = fCenterX + kZs[i].x * fD;
			z.y = fCenterY + kZs[i].y * fD;
			z.scale = kZs[i].size / big;
			double cycle = 3.2, p = std::fmod(now - fSnoreStart, cycle) / cycle, on = 0.1 + i * 0.18;
			z.opacity = Keyframes({0, 0, 0.85, 0.85, 0}, {0, on, on + 0.15, 0.78, 0.95}, {kEaseInOut, kEaseOut, kEaseInOut, kEaseIn}, p);
			return z;
		}
		double cycle = 3.0, p = std::fmod(now - fSnoreStart + i * cycle / 3, cycle) / cycle;
		double fx = fCenterX + kZs[0].x * fD, fy = fCenterY + kZs[0].y * fD;
		double cx = fCenterX + kZs[1].x * fD + 0.1 * fD, cy = fCenterY + kZs[1].y * fD;
		double tx = fCenterX + kZs[2].x * fD, ty = fCenterY + (kZs[2].y + 0.14) * fD;
		// Paced along the curve: the eased time is a share of its length.
		auto at = [&](double u, double& x, double& y) {
			double v = 1 - u;
			x = v * v * fx + 2 * v * u * cx + u * u * tx;
			y = v * v * fy + 2 * v * u * cy + u * u * ty;
		};
		const int n = 32;
		double len[n + 1] = {0}, px, py, qx, qy;
		at(0, px, py);
		for (int k = 1; k <= n; k++) { at((double) k / n, qx, qy); len[k] = len[k - 1] + std::hypot(qx - px, qy - py); px = qx; py = qy; }
		double want = kZDrift(p) * len[n], u = 1;
		for (int k = 1; k <= n; k++) {
			if (len[k] >= want) { double seg = len[k] - len[k - 1]; u = (k - 1 + (seg > 0 ? (want - len[k - 1]) / seg : 0)) / n; break; }
		}
		at(u, z.x, z.y);
		z.scale = kZs[0].size / big + (1.08 - kZs[0].size / big) * kEaseOut(p);
		z.angle = Keyframes({0.12, -0.1, 0.08, -0.04}, Even(4), {kEaseInOut, kEaseInOut, kEaseInOut}, p);
		z.opacity = Keyframes({0, 0.9, 0.85, 0}, {0, 0.18, 0.7, 1}, {kEaseOut, kEaseInOut, kEaseIn}, p);
		return z;
	}

	void Snore(bool on)
	{
		if (on == fSnoring) return;
		double now = Now();
		if (!on && fSnoring) {   // freeze each Z where it is, then fade it out
			for (int i = 0; i < 3; i++) fFrozen[i] = ZAt(i, now);
			fStopStart = now;
		}
		fSnoring = on;
		if (on) {
			fSnoreStart = now;
			fSnoreInPlace = ReduceMotion();
			fStopStart = -1;
		}
	}

	void DrawZs(Graphics& g, double left, double top, double now, double alpha)
	{
		FrozenZ zs[3];
		if (fSnoring) for (int i = 0; i < 3; i++) zs[i] = ZAt(i, now);
		else if (fStopStart >= 0 && now - fStopStart < 0.35) {
			double fade = 1 - kEaseOut((now - fStopStart) / 0.35);
			for (int i = 0; i < 3; i++) { zs[i] = fFrozen[i]; zs[i].opacity *= fade; }
		}
		else return;
		double size = kZs[2].size * fD;
		for (auto& z : zs) {
			if (z.opacity <= 0.01) continue;
			GraphicsState s = g.Save();
			g.TranslateTransform((REAL) (left + z.x), (REAL) (top + fH - z.y));
			g.RotateTransform((REAL) (-z.angle * 180 / kPi));   // y-down here: counterclockwise is negative
			g.ScaleTransform((REAL) z.scale, (REAL) z.scale);
			// A CATextLayer draws its text from the top of its box.
			RectF box((REAL) (-size * 0.7), (REAL) (-size * 0.8), (REAL) (size * 1.4), (REAL) (size * 1.6));
			Font& font = CachedFont(L"Segoe UI Black", (REAL) (size * 1.25), FontStyleRegular);
			StringFormat f;
			f.SetAlignment(StringAlignmentCenter);
			f.SetLineAlignment(StringAlignmentNear);
			SolidBrush b(C(Black(), z.opacity * alpha));
			g.DrawString(L"Z", -1, &font, box, &f, &b);
			g.Restore(s);
		}
	}

	// ---- eyes

	void CloseEyes(double close, double hold, std::function<void()> change, double open)
	{
		int gen = ++fBlink;
		for (int i = 0; i < 2; i++) Play(fLid[i], "lid", {0.08}, {0, 1}, {kEaseIn}, close).hold = true;   // shut until the open half
		After(close, [this, gen, hold, open, change] {
			if (gen != fBlink) return;
			if (change) change();
			for (int i = 0; i < 2; i++) Play(fLid[i], "lid", {0.08, 1}, {0, hold / (hold + open), 1}, {kEaseInOut, kOvershoot}, hold + open);
		});
	}

	void ShowFace(Face face)
	{
		if (face == fShown) return;
		fShown = face;
		CloseEyes(0.07, 0.02, [this] { fDrawn = fShown; }, 0.16);
	}

	void BlinkTwice(bool twice)
	{
		if (fShown != Face::Awake) return;
		CloseEyes(0.06, 0.03, nullptr, 0.12);
		if (twice) {
			int mood = fMoodGen;
			After(0.28, [this, mood] { if (fMoodGen == mood) CloseEyes(0.06, 0.03, nullptr, 0.12); });
		}
	}

	void LookAt(double x, double y)
	{
		fLook[0] = x; fLook[1] = y;
		Spring(fEyesX, "position", x * fD, 300, 20);
		Spring(fEyesY, "position", y * fD, 300, 20);
		Spring(fBumpsX, "position", x * kBumpFollow * fD, 260, 20);
		Spring(fBumpsY, "position", y * kBumpFollow * fD, 260, 20);
	}

	// ---- body

	void SquashXY(std::vector<double> xs, std::vector<double> ys, std::vector<double> times, std::vector<Curve> curves, double duration)
	{
		Play(fSquashX, "squash.x", xs, times, curves, duration);
		Play(fSquashY, "squash.y", ys, times, curves, duration);
	}

	void Hop(double height, double amount, double duration)
	{
		double m = Motion(), top = height * fD * m, k = amount * m;
		Play(fRootY, "hop", {0, top, 0, 0}, {0, 0.2, 0.5, 0.76, 1}, {kEaseInOut, kEaseOut, kEaseIn, kEaseOut}, duration);
		std::vector<double> times = {0, 0.2, 0.32, 0.5, 0.8, 1};
		std::vector<Curve> curves = {kEaseOut, kEaseOut, kEaseInOut, kEaseIn, kOvershoot};
		SquashXY({1 + k, 1 - 0.6 * k, 1, 1 + 1.1 * k, 1}, {1 - k, 1 + 0.8 * k, 1, 1 - 1.1 * k, 1}, times, curves, duration);
	}

	void Stretch()
	{
		double k = 0.1 * Motion();
		SquashXY({1 + 0.5 * k, 1 - 0.5 * k, 1}, {1 - 0.5 * k, 1 + k, 1}, {0, 0.25, 0.75, 1}, {kEaseInOut, kEaseInOut, kOvershoot}, 0.55);
	}

	void Snuffle()
	{
		double k = 0.035 * Motion() + 0.01;
		SquashXY({1 + k, 1 - 0.4 * k, 1}, {1 - k, 1 + 0.4 * k, 1}, {0, 0.3, 0.6, 1}, {kEaseInOut, kEaseInOut, kEaseOut}, 0.7);
	}

	void Tilt()
	{
		double angle = (Random(0, 1) < 0.5 ? -1 : 1) * 0.08 * Motion();
		Spring(fRot, "rotation", angle, 140, 11);
		int mood = fMoodGen;
		After(Random(0.7, 1.2), [this, mood] { if (fMoodGen == mood) Spring(fRot, "rotation", 0, 140, 11); });
	}

	void Breathe()
	{
		bool asleep = fMood == Mood::Asleep || fMood == Mood::Dozing;
		double inhale = asleep ? 1.5 : 0.55, exhale = asleep ? 2.0 : 0.75, settle = 0.45;
		struct { Track* track; double base, depth; } axes[] = {
			{&fBreathX, asleep ? 1.01 : 1.0, asleep ? 0.03 : 0.01},
			{&fBreathY, asleep ? 0.975 : 1.0, asleep ? 0.045 : 0.018},
		};
		for (auto& ax : axes) {
			double from = ax.track->Value(), base = ax.base, depth = ax.depth, cycle = inhale + exhale;
			ax.track->Add("breath", 1e12, [=](double e) {
				if (e < settle) return from + (base - from) * kEaseInOut(e / settle);
				return Keyframes({base, base + depth, base}, {0, inhale / cycle, 1}, {kEaseInOut, kEaseInOut}, std::fmod(e - settle, cycle) / cycle);
			});
		}
	}

	// ---- moods

	void EnterMood(Mood mood)
	{
		bool changed = mood != fMood;
		fMood = mood;
		fMoodGen++;
		if (changed || !fBreathY.Has("breath")) Breathe();
		Snore(WantsSnore());
		if (mood != Mood::Working && fRot.Has("rotation")) Spring(fRot, "rotation", 0, 140, 14);
		ScheduleIdle();
	}

	void ScheduleIdle()
	{
		Cancel(fIdleTimer);
		if (!shownOnScreen) return;
		double wait;
		if (fMood == Mood::Working) wait = Random(0.6, 1.8) * (1.0 - 0.6 * fBusy);
		else if (fMood == Mood::Asleep && fAvailable && !fPaused) wait = Random(7, 16);
		else return;
		fIdleTimer = After(wait, [this] { fIdleTimer = 0; Idle(); });
	}

	void Idle()
	{
		if (fMood == Mood::Working) {
			double r = Random(0, 1);
			if (r < 0.2) BlinkTwice(Random(0, 1) < 0.25);
			else if (r < 0.28 && fBusy < 0.5) Tilt();
			else LookAt(Random(-0.12, 0.12), Random(-0.07, 0.08));
		}
		else if (fMood == Mood::Asleep) Snuffle();
		ScheduleIdle();
	}

	void Wince()
	{
		ShowFace(Face::Ouch);
		fFlash.Add("flash", 0.9, [](double e) { return 1 - kEaseIn(e / 0.9); });
		double s = 0.05 * fD * Motion();
		Play(fRootX, "shake", {-s, 0.8 * s, -0.5 * s, 0.25 * s, 0}, {0, 0.12, 0.32, 0.52, 0.74, 1}, {kEaseOut, kEaseInOut, kEaseInOut, kEaseInOut, kEaseOut}, 0.5);
		double k = 0.1 * Motion();
		SquashXY({1 + k, 1 - 0.3 * k, 1}, {1 - k, 1 + 0.3 * k, 1}, {0, 0.2, 0.55, 1}, {kEaseOut, kEaseInOut, kOvershoot}, 0.5);
		int mood = fMoodGen;
		After(0.8, [this, mood] { if (fMoodGen == mood && fShown == Face::Ouch) ShowFace(Face::Awake); });
	}

	void RestartSleepTimer()
	{
		Cancel(fSleepTimer);
		double wait = std::max(sleepAfter, fAwakeUntil - Now());
		fSleepTimer = After(wait, [this] { fSleepTimer = 0; Doze(); });
	}

	void Doze()
	{
		if (fMood != Mood::Working) return;
		EnterMood(Mood::Dozing);
		int mood = fMoodGen;
		LookAt(0, 0);
		ShowFace(Face::Happy);
		double dip = 0.035 * fD * Motion();
		After(0.6, [this, mood, dip] {
			if (fMoodGen != mood) return;
			Play(fRootY, "nod", {-dip, -dip, 0}, {0, 0.6, 0.7, 1}, {kEaseInOut, kEaseInOut, kOvershoot}, 1.1);
			Play(fRot, "nodTilt", {-0.05 * Motion(), -0.05 * Motion(), 0}, {0, 0.6, 0.7, 1}, {kEaseInOut, kEaseInOut, kOvershoot}, 1.1);
		});
		After(1.9, [this, mood] {
			if (fMoodGen != mood) return;
			ShowFace(Face::Sleep);
			EnterMood(Mood::Asleep);
		});
	}

	void WakeUp(bool ok)
	{
		EnterMood(Mood::Waking);
		int mood = fMoodGen;
		ShowFace(Face::Happy);
		Stretch();
		After(0.45, [this, mood] { if (fMoodGen == mood) Hop(0.16, 0.12, 0.62); });
		After(1.0, [this, mood, ok] {
			if (fMoodGen != mood) return;
			EnterMood(Mood::Working);
			ShowFace(Face::Awake);
			if (!ok) Wince();
		});
	}

	void RippleOut(RGBA color)
	{
		if (fD < 1) return;
		fRipples.push_back({Now(), color});
	}
};

// ------------------------------------------------------------------ bars + feed

struct Bar {
	Track height{3};
	Track tint{0};
	RGBA active = {0, 0, 0, 0};
};

struct Row {
	std::wstring line, tip, time;
	RGBA color;
	bool ok;
	Track x{0}, y{0}, alpha{0};
	bool dropping = false;
};

// ------------------------------------------------------------------ the panel

class Panel {
public:
	HWND hwnd = nullptr;
	std::function<std::string()> connectionInfo;
	std::function<void(bool open, double extraHeight)> onDrawer;   // grow / shrink the panel for the terminal
	std::wstring version = L"0.1";

	Panel()
	{
		for (int i = 0; i < kGroupCount; i++) fBars.emplace_back(new Bar);
	}

	void Start() { fSlippy.Start(); }
	void Stop() { fSlippy.StopTimers(); Cancel(fCopiedTimer); }

	void SetStatus(const std::wstring& text, bool listening)
	{
		fStatus = text;
		fListening = listening;
		fSlippy.SetAvailable(fListening, fPaused);
	}

	void SetPaused(bool paused)
	{
		fPaused = paused;
		fSlippy.SetAvailable(fListening, fPaused);
	}

	void Call(const std::string& method, const std::string& line, bool ok, bool edit, double ms, const std::string& agent)
	{
		if (!agent.empty()) fAgent = W(agent);
		fCalls++;
		if (!ok) fErrors++;
		fRecent.push_back(Now());
		Kind kind = !ok ? Kind::Error : edit ? Kind::Edit : Kind::Read;
		RefreshBusy();
		fSlippy.CallArrived(kind);
		Bump(GroupOf(method), KindColor(kind), 0.35 + std::min(0.5, ms / 400.0));
		// "shown line\nmore detail": the detail joins the tooltip.
		size_t nl = line.find('\n');
		std::string shown = line.substr(0, nl);
		std::string detail = nl == std::string::npos ? "" : line.substr(nl + 1) + "\n";
		char took[32];
		if (ms < 1) snprintf(took, sizeof took, "under 1 ms");
		else snprintf(took, sizeof took, "%.0f ms", ms);
		std::string who = agent.empty() ? "" : " \xC2\xB7 " + agent;
		AddRow(W(shown), W(shown + "\n" + detail + method + " \xC2\xB7 " + took + who), KindColor(kind), ok);
	}

	void Tick()
	{
		RunDue();
		RefreshBusy();
		bool visible = IsWindowVisible(hwnd) != FALSE;
		if (visible != fSlippy.shownOnScreen) {
			fSlippy.shownOnScreen = visible;
			if (visible) fSlippy.Start();   // picks the Z's and idle looks back up
		}
		if (visible) InvalidateRect(hwnd, nullptr, FALSE);
	}

	void Paint(HDC dc)
	{
		RECT client;
		GetClientRect(hwnd, &client);
		int pw = client.right - client.left, ph = client.bottom - client.top;
		if (pw <= 0 || ph <= 0) return;
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP bmp = CreateCompatibleBitmap(dc, pw, ph);
		HGDIOBJ old = SelectObject(mem, bmp);
		{
			Graphics g(mem);
			g.SetSmoothingMode(SmoothingModeAntiAlias);
			g.SetPixelOffsetMode(PixelOffsetModeHalf);
			g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
			g.Clear(C(Background()));
			double scale = Scale();
			g.ScaleTransform((REAL) scale, (REAL) scale);
			Layout(pw / scale, ph / scale);
			DrawAll(g);
		}
		BitBlt(dc, 0, 0, pw, ph, mem, 0, 0, SRCCOPY);
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
	}

	// Mouse, in pixels of this window.
	void MouseDown(int px, int py)
	{
		double x = px / Scale(), y = py / Scale();
		if (OverFrog(x, y)) { fSlippy.Click(); return; }
		if (Inside(fHandleRect, x, y)) {   // the drawer handle: open / close the terminal
			fDrawerOpen = !fDrawerOpen;
			if (onDrawer) onDrawer(fDrawerOpen, kTerminalH);
			return;
		}
		if (Inside(fCopyRect, x, y)) { fPressed = true; SetCapture(hwnd); }
	}

	void MouseUp(int px, int py)
	{
		if (!fPressed) return;
		fPressed = false;
		ReleaseCapture();
		if (Inside(fCopyRect, px / Scale(), py / Scale())) CopyConnection();
	}

	void MouseMove(int px, int py)
	{
		double x = px / Scale(), y = py / Scale();
		fHover = Inside(fCopyRect, x, y);
		int row = RowAt(x, y);
		if (row != fTipRow) {
			fTipRow = row;
			if (fTip) SendMessageW(fTip, TTM_UPDATE, 0, 0);
		}
		if (!fTracking) {
			TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd, 0};
			fTracking = TrackMouseEvent(&t) != FALSE;
		}
	}

	void MouseLeave() { fHover = false; fTracking = false; fTipRow = -1; }

	bool OverFrogPx(int px, int py) { return OverFrog(px / Scale(), py / Scale()); }
	bool OverHandlePx(int px, int py) { return Inside(fHandleRect, px / Scale(), py / Scale()); }

	const wchar_t* TipText()
	{
		fTipBuffer.clear();
		if (fTipRow >= 0 && fTipRow < (int) fRows.size()) fTipBuffer = fRows[(size_t) fTipRow]->tip;
		return fTipBuffer.c_str();
	}

	void MakeTooltip()
	{
		fTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, nullptr, nullptr);
		if (!fTip) return;
		TOOLINFOW ti = {sizeof ti};
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ti.hwnd = hwnd;
		ti.uId = (UINT_PTR) hwnd;
		ti.lpszText = LPSTR_TEXTCALLBACKW;
		SendMessageW(fTip, TTM_ADDTOOLW, 0, (LPARAM) &ti);
		SendMessageW(fTip, TTM_SETMAXTIPWIDTH, 0, 420);
	}

private:
	Mascot fSlippy;
	std::vector<std::unique_ptr<Bar>> fBars;
	std::vector<std::unique_ptr<Row>> fRows;       // newest first
	std::vector<std::unique_ptr<Row>> fDropping;   // fading out
	std::deque<double> fRecent;
	std::wstring fStatus = L"Starting…";
	long fCalls = 0, fErrors = 0;
	bool fListening = false, fPaused = false, fDrawerOpen = false;
	std::wstring fAgent;   // who sent the last call
	bool fHover = false, fPressed = false, fTracking = false;
	bool fCopied = false;
	int fCopiedTimer = 0;
	HWND fTip = nullptr;
	int fTipRow = -1;
	std::wstring fTipBuffer;

	// Layout, in DIPs from the top-left.
	double fW = 0, fH = 0;
	RectF fSlippyRect, fBarsRect, fFeedRect, fCopyRect, fStatusRect, fTerminalRect, fHandleRect;
	REAL fTextX = 0;

	double Scale() const { UINT dpi = GetDpiForWindow(hwnd); return dpi ? dpi / 96.0 : 1.0; }
	static bool Inside(const RectF& r, double x, double y) { return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height; }

	int GroupOf(const std::string& method)
	{
		std::string g = method.substr(0, method.find('.'));
		for (int i = 0; i < kGroupCount; i++) if (g == kGroups[i]) return i;
		return kGroupCount - 1;
	}

	// The same layout as the Mac panel (SlippyPanelView.mm), in DIPs from the top-left.
	void Layout(double w, double h)
	{
		fW = w; fH = h;
		const double slippy = 110;
		fSlippyRect = RectF((REAL) (kPad - 4), 4, (REAL) slippy, (REAL) slippy);
		fSlippy.Layout(slippy, slippy);
		fTextX = (REAL) (kPad + 120);
		fBarsRect = RectF((REAL) kPad, 130, (REAL) (w - 2 * kPad), 46);
		fCopyRect.Y = 190;   // its x and width follow the label, in DrawControls
		fStatusRect = RectF((REAL) kPad, 194, (REAL) std::max(0.0, w - 2 * kPad - 120), 15);
		double y = 228, bottom = h - kHandleH;
		double feedH = std::max(0.0, bottom - y - 8);
		if (fDrawerOpen) feedH = std::min(feedH, kFeedRows * (kRowH + kRowGap) - kRowGap);
		fFeedRect = RectF((REAL) kPad, (REAL) y, (REAL) (w - 2 * kPad), (REAL) feedH);
		double ty = y + feedH + 12;
		fTerminalRect = RectF((REAL) kPad, (REAL) ty, (REAL) (w - 2 * kPad), (REAL) std::max(0.0, bottom - ty - 10));
		fHandleRect = RectF(0, (REAL) (h - kHandleH), (REAL) w, (REAL) kHandleH);
	}

	bool OverFrog(double x, double y)
	{
		return fSlippy.Hit(x - fSlippyRect.X, fSlippyRect.Y + fSlippyRect.Height - y);
	}

	void DrawAll(Graphics& g)
	{
		double w = fW;
		fSlippy.Draw(g, fSlippyRect.X, fSlippyRect.Y);
		// Name and version.
		REAL titleW = TextWidth(g, L"Slippy", 20, FontStyleBold);
		Text(g, L"Slippy", RectF(fTextX, 50, titleW + 4, 26), 20, FontStyleBold, LabelColor(), AlignLeft);
		std::wstring v = L"v." + version;
		REAL vw = TextWidth(g, v, 11, FontStyleRegular) + 10;
		RectF badge(fTextX + titleW + 8, 55, vw, 18);
		GraphicsPath bp;
		RoundRect(bp, badge, 4);
		RGBA edge = SecondaryLabel();
		edge.a *= 0.6;
		Pen bpen(C(edge), 1);
		g.DrawPath(&bpen, &bp);
		Text(g, v, badge, 11, FontStyleRegular, SecondaryLabel(), AlignCenter);
		// Who's calling, and the counts.
		REAL cx = fTextX;
		if (!fAgent.empty()) { DrawAgent(g, RectF(fTextX, 84, 15, 15)); cx += 21; }
		wchar_t counts[96];
		swprintf(counts, 96, L"%ld call%ls \u2022 %ld error%ls", fCalls, fCalls == 1 ? L"" : L"s", fErrors, fErrors == 1 ? L"" : L"s");
		Text(g, counts, RectF(cx, 83, (REAL) std::max(0.0, w - cx - kPad), 16), 11, FontStyleRegular, LabelColor(), AlignLeft);
		DrawBars(g);
		Text(g, fStatus, fStatusRect, 10, FontStyleRegular, SecondaryLabel(), AlignLeft);
		DrawControls(g);
		DrawFeed(g);
		DrawDrawer(g);
	}

	// The agent's logo (Resources/agents), or a colored disc with its initial.
	void DrawAgent(Graphics& g, RectF r)
	{
		for (const slippy::AgentLogo& logo : kAgentLogos) {
			if (fAgent != W(logo.agent)) continue;
			REAL k = std::min(r.Width, r.Height) / 24;
			GraphicsPath path(logo.evenOdd ? FillModeAlternate : FillModeWinding);
			PointF pen(0, 0), start(0, 0);
			auto at = [&](double x, double y) { return PointF((REAL) (r.X + x * k), (REAL) (r.Y + y * k)); };
			for (const slippy::LogoOp& o : slippy::ParseLogo(logo.path)) {
				if (o.op == 'M') { path.StartFigure(); pen = start = at(o.v[0], o.v[1]); }
				else if (o.op == 'L') { PointF p = at(o.v[0], o.v[1]); path.AddLine(pen, p); pen = p; }
				else if (o.op == 'C') { PointF p = at(o.v[4], o.v[5]); path.AddBezier(pen, at(o.v[0], o.v[1]), at(o.v[2], o.v[3]), p); pen = p; }
				else { path.CloseFigure(); pen = start; }
			}
			SolidBrush fill(C(logo.color < 0 ? LabelColor() : Rgb(logo.color)));
			g.FillPath(&fill, &path);
			return;
		}
		static const std::pair<const wchar_t*, int> colors[] = {
			{L"Claude", 0xD97757}, {L"Codex", 0x10A37F}, {L"ChatGPT", 0x10A37F}, {L"Cursor", 0x9A9A9A}, {L"Gemini", 0x4C8DF6},
			{L"Qwen", 0x6E5CF0}, {L"Kimi", 0x2F7BF5}, {L"Grok", 0xE6E6E6}, {L"Copilot", 0x8957E5}, {L"VS Code", 0x23A9F2},
			{L"Windsurf", 0x09B6A2}, {L"Cline", 0xF2A93B}, {L"Roo Code", 0xE5484D}, {L"OpenCode", 0xB0B0B0}, {L"Zed", 0x5A8DEE}, {L"Script", 0x8E8E93}};
		int rgb = 0x8E8E93;
		for (auto& c : colors) if (fAgent == c.first) rgb = c.second;
		REAL rad = std::min(r.Width, r.Height) / 2;
		SolidBrush disc(C(Rgb(rgb)));
		g.FillEllipse(&disc, r);
		std::wstring initial(1, (wchar_t) towupper(fAgent[0]));
		Text(g, initial, r, (REAL) (rad * 1.1), FontStyleBold, RGBA{0.08, 0.08, 0.08, 1}, AlignCenter);
	}

	// The terminal's home (the terminal itself comes next) and the handle that opens it.
	void DrawDrawer(Graphics& g)
	{
		if (fDrawerOpen && fTerminalRect.Height > 0) {
			GraphicsPath p;
			RoundRect(p, fTerminalRect, 10);
			SolidBrush well(Color(46, 0, 0, 0));
			g.FillPath(&well, &p);
		}
		SolidBrush strip(Color(36, 0, 0, 0));
		g.FillRectangle(&strip, fHandleRect);
		REAL cx = fHandleRect.X + fHandleRect.Width / 2, cy = fHandleRect.Y + fHandleRect.Height / 2;
		REAL tip = fDrawerOpen ? -2.5f : 2.5f;   // closed: points down (open me); open: points up
		PointF chevron[] = {PointF(cx - 7, cy - tip), PointF(cx, cy + tip), PointF(cx + 7, cy - tip)};
		RGBA c = SecondaryLabel();
		c.a *= 0.6;
		SolidBrush b(C(c));
		g.FillPolygon(&b, chevron, 3);
	}

	// ---- bars

	void Bump(int group, RGBA color, double strength)
	{
		Bar& bar = *fBars[(size_t) group];
		double maxH = MaxBarHeight(), current = bar.height.Value();
		double peak = std::min(maxH, current + (maxH - 3) * strength);
		Play(bar.height, "bump", {peak, 3}, {0, 0.18, 1}, {kCAEaseOut, kCAEaseInOut}, 1.65);
		bar.active = color;
		bar.tint.Add("tint", 1.65, [](double e) { return 1 - kCAEaseInOut(e / 1.65); });
	}

	double MaxBarHeight() const { return std::max(4.0, (double) fBarsRect.Height - 14); }

	void DrawBars(Graphics& g)
	{
		double slot = fBarsRect.Width / kGroupCount, bw = std::max(4.0, slot * 0.55);
		double bottom = fBarsRect.Y + fBarsRect.Height;
		RGBA rest = TertiaryLabel();
		rest.a *= 0.35;
		for (int i = 0; i < kGroupCount; i++) {
			Bar& bar = *fBars[(size_t) i];
			double h = std::min(MaxBarHeight(), bar.height.Value());
			RGBA color = Mix(rest, bar.active, bar.tint.Value());
			GraphicsPath p;
			RoundRect(p, RectF((REAL) (fBarsRect.X + slot * (i + 0.5) - bw / 2), (REAL) (bottom - 12 - h), (REAL) bw, (REAL) h), 2);
			SolidBrush b(C(color));
			g.FillPath(&b, &p);
			std::wstring label = W(std::string(kGroups[i]).substr(0, 3));
			Text(g, label, RectF((REAL) (fBarsRect.X + slot * i - 6), (REAL) (bottom - 11), (REAL) (slot + 12), 11), 8, FontStyleRegular, TertiaryLabel(), AlignCenter);
		}
	}

	// ---- controls (drawn, so they follow Illustrator's theme)

	void DrawControls(Graphics& g)
	{
		// Copy connection
		const wchar_t* title = fCopied ? L"Copied" : L"Copy connection";
		REAL cw = TextWidth(g, L"Copy connection", 11, FontStyleRegular) + 22;
		fCopyRect = RectF((REAL) (fW - kPad - cw), fCopyRect.Y, cw, 22);
		GraphicsPath b;
		RoundRect(b, RectF(fCopyRect.X + 0.5f, fCopyRect.Y + 0.5f, fCopyRect.Width - 1, fCopyRect.Height - 1), 5);
		RGBA face = gDark ? RGBA{1, 1, 1, fPressed ? 0.22 : fHover ? 0.16 : 0.1} : RGBA{0, 0, 0, fPressed ? 0.14 : fHover ? 0.09 : 0.05};
		SolidBrush fill(C(face));
		g.FillPath(&fill, &b);
		Pen edge(C(TertiaryLabel()), 1);
		g.DrawPath(&edge, &b);
		Text(g, title, fCopyRect, 11, FontStyleRegular, LabelColor(), AlignCenter);
	}

	void CopyConnection()
	{
		std::string info = connectionInfo ? connectionInfo() : "";
		std::wstring text;
		for (wchar_t c : W(info)) { if (c == L'\n') text += L'\r'; text += c; }
		if (OpenClipboard(hwnd)) {
			EmptyClipboard();
			size_t bytes = (text.size() + 1) * sizeof(wchar_t);
			if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
				memcpy(GlobalLock(mem), text.c_str(), bytes);
				GlobalUnlock(mem);
				if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
			}
			CloseClipboard();
		}
		fCopied = true;
		Cancel(fCopiedTimer);
		fCopiedTimer = After(1.2, [this] { fCopiedTimer = 0; fCopied = false; });
	}

	// ---- feed

	void RefreshBusy()
	{
		double now = Now();
		while (!fRecent.empty() && now - fRecent.front() > 3) fRecent.pop_front();
		fSlippy.SetBusy(std::min(1.0, fRecent.size() / 5.0));
	}

	// The newest call slides in on top; the rest move down and the oldest fades.
	void AddRow(const std::wstring& line, const std::wstring& tip, RGBA color, bool ok)
	{
		const double rowH = kRowH;
		bool still = ReduceMotion();
		std::unique_ptr<Row> row(new Row);
		row->line = line;
		row->tip = tip;
		row->color = color;
		row->ok = ok;
		wchar_t time[64] = L"";
		GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, time, 64);
		row->time = time;
		row->x.model = still ? 0 : -24;
		fRows.insert(fRows.begin(), std::move(row));
		if ((int) fRows.size() > kFeedRows) {
			std::unique_ptr<Row> drop = std::move(fRows.back());
			fRows.pop_back();
			drop->dropping = true;
			Play(drop->alpha, "fade", {0}, {0, 1}, {kEaseOut}, 0.3).hold = true;
			fDropping.push_back(std::move(drop));
		}
		for (size_t i = 0; i < fRows.size(); i++) {
			Row& r = *fRows[i];
			double y = i * (rowH + kRowGap), a = 1.0 - 0.08 * i;
			if (still) { r.x.model = 0; r.y.model = y; }
			else {
				Play(r.x, "x", {0}, {0, 1}, {kEaseOut}, 0.3); r.x.model = 0;
				Play(r.y, "y", {y}, {0, 1}, {kEaseOut}, 0.3); r.y.model = y;
			}
			Play(r.alpha, "alpha", {a}, {0, 1}, {kEaseOut}, 0.3); r.alpha.model = a;
		}
	}

	int RowAt(double x, double y)
	{
		if (!Inside(fFeedRect, x, y)) return -1;
		for (size_t i = 0; i < fRows.size(); i++) {
			double top = fFeedRect.Y + fRows[i]->y.Value();
			if (y >= top && y < top + 18) return (int) i;
		}
		return -1;
	}

	void DrawFeed(Graphics& g)
	{
		fDropping.erase(std::remove_if(fDropping.begin(), fDropping.end(), [](const std::unique_ptr<Row>& r) {
			return !r->alpha.Animating() || r->alpha.Value() <= 0.001; }), fDropping.end());
		GraphicsState s = g.Save();
		g.SetClip(fFeedRect);
		for (auto* list : {&fRows, &fDropping}) {
			for (auto& r : *list) DrawRow(g, *r);
		}
		g.Restore(s);
	}

	void DrawRow(Graphics& g, Row& r)
	{
		double a = r.alpha.Value();
		if (a <= 0.001) return;
		REAL x = (REAL) (fFeedRect.X + r.x.Value()), y = (REAL) (fFeedRect.Y + r.y.Value()), w = fFeedRect.Width, timeW = 52;
		SolidBrush dot(C(r.color, a));
		g.FillEllipse(&dot, x, y + 6, 6.0f, 6.0f);
		Text(g, r.line, RectF(x + 12, y + 1, std::max(0.0f, w - 12 - timeW - 4), 16), 11, FontStyleRegular, r.ok ? LabelColor() : ErrorColor(), AlignLeft, a);
		Text(g, r.time, RectF(x + std::max(0.0f, w - timeW), y + 1, timeW, 16), 10, FontStyleRegular, TertiaryLabel(), AlignRight, a);
	}
};

// ------------------------------------------------------------------ window + SDK glue

const wchar_t* kClass = L"SlippyPanelView";
const UINT_PTR kFrameTimer = 1;

AIPanelRef gPanel = nullptr;
PanelCallbacks gCallbacks;
std::unique_ptr<Panel> gView;
std::wstring gStatus = L"Starting…";
bool gListening = false;
bool gPaused = false;
ULONG_PTR gGdiplus = 0;
bool gClassRegistered = false;

HINSTANCE ThisModule()
{
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) &ThisModule, &module);
	return module;
}

LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	Panel* view = gView && gView->hwnd == hwnd ? gView.get() : nullptr;
	switch (msg) {
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		if (view) view->Paint(dc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_TIMER:
		if (wp == kFrameTimer && view) view->Tick();
		return 0;
	case WM_LBUTTONDOWN:
		if (view) view->MouseDown((short) LOWORD(lp), (short) HIWORD(lp));
		return 0;
	case WM_LBUTTONUP:
		if (view) view->MouseUp((short) LOWORD(lp), (short) HIWORD(lp));
		return 0;
	case WM_MOUSEMOVE:
		if (view) view->MouseMove((short) LOWORD(lp), (short) HIWORD(lp));
		return 0;
	case WM_MOUSELEAVE:
		if (view) view->MouseLeave();
		return 0;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;   // clicks work without taking focus from the document
	case WM_SETCURSOR:
		if (view && LOWORD(lp) == HTCLIENT) {
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);
			SetCursor(LoadCursor(nullptr, view->OverFrogPx(pt.x, pt.y) || view->OverHandlePx(pt.x, pt.y) ? IDC_HAND : IDC_ARROW));
			return TRUE;
		}
		break;
	case WM_NOTIFY: {
		NMHDR* h = (NMHDR*) lp;
		if (view && h->code == TTN_GETDISPINFOW) {
			((NMTTDISPINFOW*) lp)->lpszText = const_cast<wchar_t*>(view->TipText());
			return 0;
		}
		break;
	}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// Fills the host with the view.
void Fit()
{
	if (!gView || !gView->hwnd || !gPanel || !sAIPanel) return;
	AIPanelPlatformWindow host = nullptr;
	if (sAIPanel->GetPlatformWindow(gPanel, host) || !host) return;
	if (GetParent(gView->hwnd) != host) SetParent(gView->hwnd, host);
	RECT r;
	GetClientRect(host, &r);
	SetWindowPos(gView->hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SizeChanged(AIPanelRef) { Fit(); }

void Install()
{
	if (!gPanel || !sAIPanel) return;
	if (gView && gView->hwnd) { Fit(); return; }
	AIPanelPlatformWindow host = nullptr;
	if (sAIPanel->GetPlatformWindow(gPanel, host) || !host) return;
	if (!gGdiplus) {
		GdiplusStartupInput input;
		if (GdiplusStartup(&gGdiplus, &input, nullptr) != Ok) { gGdiplus = 0; return; }
	}
	if (!gClassRegistered) {
		INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_WIN95_CLASSES};
		InitCommonControlsEx(&icc);
		WNDCLASSEXW wc = {sizeof wc};
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ViewProc;
		wc.hInstance = ThisModule();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = kClass;
		gClassRegistered = RegisterClassExW(&wc) != 0;
	}
	gView.reset(new Panel);
	gView->onDrawer = [](bool open, double extra) {   // opening the terminal makes the panel taller
		AISize size;
		if (!gPanel || sAIPanel->GetSize(gPanel, size)) return;
		size.height = (AIReal) std::max(260.0, size.height + (open ? extra : -extra));
		sAIPanel->SetSize(gPanel, size);
		Fit();
	};
	{
		std::wstring v = W(kSlippyVersion);   // "0.1.0" -> "0.1"
		if (v.size() > 2 && v.compare(v.size() - 2, 2, L".0") == 0) v.resize(v.size() - 2);
		gView->version = v;
	}
	gView->connectionInfo = [] { return gCallbacks.connectionInfo ? gCallbacks.connectionInfo() : std::string(); };
	RECT r;
	GetClientRect(host, &r);
	gView->hwnd = CreateWindowExW(0, kClass, L"Slippy", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, r.right - r.left, r.bottom - r.top,
		host, nullptr, ThisModule(), nullptr);
	if (!gView->hwnd) { gView.reset(); return; }
	gView->MakeTooltip();
	gView->SetStatus(gStatus, gListening);
	gView->SetPaused(gPaused);
	gView->Start();
	SetTimer(gView->hwnd, kFrameTimer, 16, nullptr);
	sAIPanel->SetSizeChangedNotifyProc(gPanel, SizeChanged);
}

} // namespace

void PanelAttach(AIPanelRef panel, PanelCallbacks callbacks)
{
	gPanel = panel;
	gCallbacks = std::move(callbacks);
	AISize minSize = {240, 300}, pref = {290, 480}, maxSize = {700, 2400};
	sAIPanel->SetSizes(gPanel, minSize, pref, pref, maxSize);
	Install();
}

void PanelDetach()
{
	if (gView) {
		gView->Stop();
		if (gView->hwnd) { KillTimer(gView->hwnd, kFrameTimer); DestroyWindow(gView->hwnd); }
		gView.reset();
	}
	gScheduled.clear();
	ClearFonts();
	if (gClassRegistered) { UnregisterClassW(kClass, ThisModule()); gClassRegistered = false; }
	if (gGdiplus) { GdiplusShutdown(gGdiplus); gGdiplus = 0; }
	gPanel = nullptr;
}

void PanelSetStatus(const std::string& text, bool listening)
{
	gStatus = W(text);
	gListening = listening;
	Install();
	if (gView) gView->SetStatus(gStatus, gListening);
}

void PanelSetPaused(bool paused)
{
	gPaused = paused;
	Install();
	if (gView) gView->SetPaused(paused);
}

void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent)
{
	Install();
	if (gView) gView->Call(method, line, ok, changesDocument, milliseconds, agent);
}

// Windows keeps Slippy's internal "Run Agent Calls" command in the menu:
// Illustrator's Windows menus can't hide one item, and removing it from the
// menu bar under Illustrator's feet isn't worth the risk. Choosing it just
// runs any queued calls, which is harmless.
void HideMenuItemTitled(const std::string&) {}
