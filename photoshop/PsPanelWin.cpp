// Slippy's panel in Photoshop on Windows: the shared Windows view
// (shared/SlippyPanelWin.cpp) in a floating tool window that stays above
// Photoshop. File > Automate > Slippy shows it (Photoshop's Windows menus
// aren't plain Win32 ones to add to); it remembers where it was and whether
// it was open. Right-click it for Pause agents. Main thread only.

#include "PsPanel.h"
#include "Platform.h"
#include "SlippyPanelWin.h"
#include "Version.h"

#include <algorithm>
#include <sstream>

namespace {

const wchar_t* kFrameClass = L"SlippyPhotoshopPanel";
const UINT kPauseItem = 1;

PanelCallbacks gCallbacks;
HWND gFrame = nullptr;
bool gRegistered = false;
std::string gStatus = "Starting…";
bool gListening = false;
bool gPaused = false;

HINSTANCE ThisModule()
{
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) &ThisModule, &module);
	return module;
}

// Photoshop's main window: this process's largest visible top-level window.
HWND PhotoshopWindow()
{
	struct Find { HWND best = nullptr; LONG area = 0; } find;
	EnumWindows([](HWND w, LPARAM lp) -> BOOL {
		DWORD pid = 0;
		GetWindowThreadProcessId(w, &pid);
		if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
		RECT r;
		GetWindowRect(w, &r);
		Find* f = (Find*) lp;
		LONG area = (r.right - r.left) * (r.bottom - r.top);
		if (area > f->area) { f->area = area; f->best = w; }
		return TRUE;
	}, (LPARAM) &find);
	return find.best;
}

// The window's place and whether it's open, between launches.
std::string StatePath() { return slippy::platform::JoinPath(slippy::platform::SupportDir(), "panel-window.txt"); }

void SaveState()
{
	if (!gFrame) return;
	RECT r;
	GetWindowRect(gFrame, &r);
	std::ostringstream out;
	out << r.left << " " << r.top << " " << (r.right - r.left) << " " << (r.bottom - r.top) << " " << (IsWindowVisible(gFrame) ? 1 : 0);
	slippy::platform::WritePrivateFile(StatePath(), out.str());
}

bool LoadState(RECT& r, bool& open)
{
	std::istringstream in(slippy::platform::ReadFile(StatePath()));
	int x, y, w, h, o;
	if (!(in >> x >> y >> w >> h >> o)) return false;
	r = {x, y, x + std::max(240, w), y + std::max(300, h)};
	open = o != 0;
	return true;
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_SIZE:
		slippy::winpanel::Fit(hwnd);
		return 0;
	case WM_GETMINMAXINFO: {
		MINMAXINFO* m = (MINMAXINFO*) lp;
		m->ptMinTrackSize = {240, 300};
		return 0;
	}
	case WM_CLOSE:   // hide, keeping the panel; File > Automate > Slippy brings it back
		ShowWindow(hwnd, SW_HIDE);
		SaveState();
		return 0;
	case WM_EXITSIZEMOVE:
		SaveState();
		return 0;
	case WM_CONTEXTMENU: {
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING | (gPaused ? MF_CHECKED : 0), kPauseItem, L"Pause agents");
		POINT pt = {(short) LOWORD(lp), (short) HIWORD(lp)};
		if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
		UINT chose = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
		DestroyMenu(menu);
		if (chose == kPauseItem) {
			gPaused = !gPaused;
			PanelSetPaused(gPaused);
			if (gCallbacks.onPause) gCallbacks.onPause(gPaused);
		}
		return 0;
	}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

void Build()
{
	if (gFrame) return;
	if (!gRegistered) {
		WNDCLASSEXW wc = {sizeof wc};
		wc.lpfnWndProc = FrameProc;
		wc.hInstance = ThisModule();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = CreateSolidBrush(RGB(0x32, 0x32, 0x32));
		wc.lpszClassName = kFrameClass;
		gRegistered = RegisterClassExW(&wc) != 0;
	}
	RECT r;
	bool open = true;
	if (!LoadState(r, open)) {   // the first time: inside Photoshop's window, near its top-right corner
		RECT host = {0, 0, 1280, 800};
		if (HWND ps = PhotoshopWindow()) GetWindowRect(ps, &host);
		r = {host.right - 290 - 340, host.top + 140, host.right - 340, host.top + 140 + 500};
	}
	gFrame = CreateWindowExW(WS_EX_TOOLWINDOW, kFrameClass, L"Slippy", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
		r.left, r.top, r.right - r.left, r.bottom - r.top, PhotoshopWindow(), nullptr, ThisModule(), nullptr);
	if (!gFrame) return;
	slippy::winpanel::SetGroups({"document", "layer", "select", "edit", "filter", "text", "ps", "history", "commands", "app"});
	slippy::winpanel::Options o;
	o.connectionInfo = [] { return gCallbacks.connectionInfo ? gCallbacks.connectionInfo() : std::string(); };
	o.onDrawer = [](bool openDrawer, double extra) {
		RECT f;
		GetWindowRect(gFrame, &f);
		int height = std::max(300, (int) (f.bottom - f.top + (openDrawer ? extra : -extra)));
		SetWindowPos(gFrame, nullptr, 0, 0, f.right - f.left, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	};
	o.version = kSlippyVersion;
	slippy::winpanel::Create(gFrame, o);
	slippy::winpanel::SetStatus(gStatus, gListening);
	slippy::winpanel::SetPaused(gPaused);
}

} // namespace

void PanelInit(PanelCallbacks callbacks)
{
	gCallbacks = std::move(callbacks);
	RECT r;
	bool open = true;
	if (!LoadState(r, open) || open)   // unless the docked panel turns up first
		slippy::platform::MainThreadAfter(6, [] { if (!gCallbacks.docked || !gCallbacks.docked()) PanelShow(); });
}

void PanelShow()
{
	Build();
	if (!gFrame) return;
	ShowWindow(gFrame, SW_SHOWNOACTIVATE);
	SaveState();
}

void PanelHide()
{
	if (gFrame) ShowWindow(gFrame, SW_HIDE);   // the saved "open" stays, for when the docked panel isn't there
}

void PanelShutdown()
{
	SaveState();
	slippy::winpanel::Destroy();
	if (gFrame) { DestroyWindow(gFrame); gFrame = nullptr; }
	if (gRegistered) { UnregisterClassW(kFrameClass, ThisModule()); gRegistered = false; }
}

void PanelSetStatus(const std::string& text, bool listening)
{
	gStatus = text;
	gListening = listening;
	slippy::winpanel::SetStatus(text, listening);
}

void PanelSetPaused(bool paused)
{
	gPaused = paused;
	slippy::winpanel::SetPaused(paused);
	if (!paused) slippy::winpanel::SetStatus(gStatus, gListening);
}

void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent)
{
	slippy::winpanel::Call(method, ok, changesDocument, milliseconds, line, agent);
}
