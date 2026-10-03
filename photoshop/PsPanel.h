#ifndef __SLIPPY_PS_PANEL_H__
#define __SLIPPY_PS_PANEL_H__

// Slippy's panel in Photoshop: the same SlippyPanelView as Illustrator's
// docked panel, in a floating window (Photoshop's C++ SDK has no docked
// panels). Window > Slippy shows and hides it; it remembers where it was and
// whether it was open. Right-click it for Pause agents. Main thread only.

#include <functional>
#include <string>

struct PanelCallbacks {
	std::function<std::string()> connectionInfo;   // what "Copy connection" copies
	std::function<void(bool paused)> onPause;      // Pause agents, from the panel's menu
};

void PanelInit(PanelCallbacks callbacks);   // adds Window > Slippy; opens the panel if it was open last time
void PanelShow();
void PanelShutdown();   // Photoshop is quitting: saves the terminal for next time
void PanelSetStatus(const std::string& text, bool listening);
void PanelSetPaused(bool paused);
void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent);

#endif // __SLIPPY_PS_PANEL_H__
