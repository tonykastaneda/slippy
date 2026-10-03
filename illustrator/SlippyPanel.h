#ifndef __SLIPPY_PANEL_H__
#define __SLIPPY_PANEL_H__

// The docked Slippy panel (Window > Slippy), starring Slippy the mascot: asleep
// (— —, Z's) while idle, wakes (^ ^) on the first call, works (o o, looking
// around) while agents call, winces (> <) on errors. Calls also ripple (teal =
// read, amber = edit, red = error), bump their group's bar and slide into the feed.
// Reduce Motion keeps the faces, drops the movement. Main thread only.

#include "AIPanel.h"

#include <functional>
#include <string>

struct PanelCallbacks {
	std::function<std::string()> connectionInfo;   // what "Copy connection" copies
};

void PanelAttach(AIPanelRef panel, PanelCallbacks callbacks);
void PanelDetach();
void PanelSetStatus(const std::string& text, bool listening);
void PanelSetPaused(bool paused);   // Pause agents, from the flyout menu
// Hides a menu item by its title (Slippy's own internal command).
void HideMenuItemTitled(const std::string& title);

// agent: who sent the call ("Claude", "Codex"...; "" if unknown).
void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent);

#endif // __SLIPPY_PANEL_H__
