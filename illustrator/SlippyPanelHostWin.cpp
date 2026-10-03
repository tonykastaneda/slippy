// Puts the Windows panel (shared/SlippyPanelWin.cpp) into Illustrator's
// docked panel, in Illustrator's theme colors.

#include "IllustratorSDK.h"
#include "SlippyPanel.h"
#include "SlippySuites.h"
#include "SlippyID.h"
#include "AIUITheme.h"
#include "SlippyPanelWin.h"

#include <algorithm>

extern "C" SPBasicSuite* sSPBasic;

namespace {

AIPanelRef gPanel = nullptr;
PanelCallbacks gCallbacks;
std::string gStatus = "Starting…";
bool gListening = false;
bool gPaused = false;

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

HWND Host()
{
	AIPanelPlatformWindow host = nullptr;
	if (!gPanel || !sAIPanel || sAIPanel->GetPlatformWindow(gPanel, host)) return nullptr;
	return host;
}

void SizeChanged(AIPanelRef) { slippy::winpanel::Fit(Host()); }

void Install()
{
	HWND host = Host();
	if (!host) return;
	if (slippy::winpanel::Exists()) { slippy::winpanel::Fit(host); return; }
	slippy::winpanel::Options o;
	o.connectionInfo = [] { return gCallbacks.connectionInfo ? gCallbacks.connectionInfo() : std::string(); };
	o.onDrawer = [](bool open, double extra) {   // opening the terminal makes the panel taller
		AISize size;
		if (!gPanel || sAIPanel->GetSize(gPanel, size)) return;
		size.height = (AIReal) std::max(260.0, size.height + (open ? extra : -extra));
		sAIPanel->SetSize(gPanel, size);
		slippy::winpanel::Fit(Host());
	};
	o.theme = [](double rgb[3], bool& dark) {
		AIUIThemeSuite* t = Theme();
		AIUIThemeColor c;
		if (!t || t->GetUIThemeColor(kAIUIThemeSelectorPanel, kAIUIComponentColorBackground, c)) return false;
		rgb[0] = c.red;
		rgb[1] = c.green;
		rgb[2] = c.blue;
		dark = t->IsUIThemeDark();
		return true;
	};
	o.version = kSlippyVersion;
	if (!slippy::winpanel::Create(host, o)) return;
	slippy::winpanel::SetStatus(gStatus, gListening);
	slippy::winpanel::SetPaused(gPaused);
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
	slippy::winpanel::Destroy();
	gPanel = nullptr;
}

void PanelSetStatus(const std::string& text, bool listening)
{
	gStatus = text;
	gListening = listening;
	Install();
	slippy::winpanel::SetStatus(text, listening);
}

void PanelSetPaused(bool paused)
{
	gPaused = paused;
	Install();
	slippy::winpanel::SetPaused(paused);
}

void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent)
{
	Install();
	slippy::winpanel::Call(method, ok, changesDocument, milliseconds, line, agent);
}

// Windows keeps Slippy's internal "Run Agent Calls" command in the menu:
// Illustrator's Windows menus can't hide one item, and removing it from the
// menu bar under Illustrator's feet isn't worth the risk. Choosing it just
// runs any queued calls, which is harmless.
void HideMenuItemTitled(const std::string&) {}
