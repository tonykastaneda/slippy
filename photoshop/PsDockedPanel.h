#ifndef __SLIPPY_PS_DOCKED_PANEL_H__
#define __SLIPPY_PS_DOCKED_PANEL_H__

// The link to Slippy's docked panel (panel/, a UXP plug-in): Photoshop's C++
// SDK can't dock a panel, so the front end is a small UXP one and this plug-in
// stays the back end. They talk through Photoshop's plug-in messaging
// (PIUXPSuite): this sends {slippyJSON: "<json>"} to the panel - its state and
// each call - and hears its hello and Pause agents. Main thread only.

#include <functional>
#include <string>

namespace slippy {
namespace ps {
namespace docked {

struct Callbacks {
	std::function<std::string()> connection;   // Slippy's URL, for Copy connection
	std::function<void(bool paused)> onPause;  // the panel's Pause agents
	std::function<void()> onHello;             // the panel opened (or reloaded)
};

void Init(Callbacks callbacks);   // false-safe: without the UXP suite it stays quiet
void Shutdown();
bool Connected();   // the panel has said hello since Photoshop started
void SetStatus(const std::string& text, bool listening);
void SetPaused(bool paused);
void Call(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent);

} // namespace docked
} // namespace ps
} // namespace slippy

#endif // __SLIPPY_PS_DOCKED_PANEL_H__
