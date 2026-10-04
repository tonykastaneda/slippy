#ifndef __SLIPPY_PS_PANEL_H__
#define __SLIPPY_PS_PANEL_H__

// Slippy's floating panel in Photoshop: the same SlippyPanelView as
// Illustrator's docked panel, in a floating window (Photoshop's C++ SDK has
// no docked panels; the docked one is panel/, a UXP plug-in - see
// PsDockedPanel.h). It stands aside while the docked panel is open, and is
// there when it isn't. Window > Slippy shows and hides it; it remembers where
// it was. Right-click it for Pause agents. Main thread only.

#include <functional>
#include <string>

struct PanelCallbacks {
	std::function<std::string()> connectionInfo;   // what "Copy connection" copies
	std::function<void(bool paused)> onPause;      // Pause agents, from the panel's menu
	std::function<bool()> docked;                  // the docked panel is open: don't open this one by itself
};

void PanelInit(PanelCallbacks callbacks);   // adds Window > Slippy; opens the panel if it was open last time
void PanelShow();
void PanelHide();   // the docked panel opened
void PanelShutdown();   // Photoshop is quitting: saves the terminal for next time
void PanelSetStatus(const std::string& text, bool listening);
void PanelSetPaused(bool paused);
void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent);

#endif // __SLIPPY_PS_PANEL_H__
