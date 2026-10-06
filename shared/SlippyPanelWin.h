#ifndef __SLIPPY_PANEL_WIN_H__
#define __SLIPPY_PANEL_WIN_H__

// The Slippy panel on Windows (GDI+), free of any Adobe SDK: one view, made
// as a child of whatever window hosts it - Illustrator's docked panel or
// Photoshop's floating one. Main thread only.

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace slippy {
namespace winpanel {

struct Options {
	std::function<std::string()> connectionInfo;                    // what "Copy connection" copies
	std::function<void(bool open, double extraHeight)> onDrawer;    // grow / shrink the host for the drawer
	// The host's panel background (0-1 each) and whether its theme is dark;
	// false (or none) keeps the default dark gray.
	std::function<bool(double rgb[3], bool& dark)> theme;
	std::string version;   // "1.2.0", shown as "v.1.2"
};

bool Create(HWND parent, Options options);   // the view, filling 'parent' (once; later calls just Fit)
bool Exists();
void Fit(HWND parent);   // fill 'parent' again (after it's resized)
void Destroy();
// The ten command groups the bars stand for, before Create (a method's group
// is its first word: "layer.set" -> "layer"; the last group takes the rest).
void SetGroups(const std::vector<std::string>& groups);
void SetStatus(const std::string& text, bool listening);
void SetPaused(bool paused);
void Call(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent);

} // namespace winpanel
} // namespace slippy

#endif // __SLIPPY_PANEL_WIN_H__
