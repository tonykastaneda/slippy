#include "IllustratorSDK.h"
#include "SlippyPanel.h"
#include "SlippySuites.h"
#include "SlippyID.h"

#import "SlippyPanelView.h"
#import "Terminal.h"
#include "Platform.h"

#include <algorithm>

// Hosts SlippyPanelView (SlippyPanelView.mm) in Illustrator's docked panel.

// ------------------------------------------------------------------ C++ side

namespace {
AIPanelRef gPanel = nullptr;
SlippyPanelView* gView = nil;
PanelCallbacks gCallbacks;
NSString* gStatus = @"Starting…";
BOOL gListening = NO;
BOOL gPaused = NO;

NSString* NS(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

void Install()
{
	if (!gPanel || !sAIPanel) return;
	__autoreleasing AIPanelPlatformWindow host = nil;
	if (sAIPanel->GetPlatformWindow(gPanel, host) || !host) return;
	if (gView && gView.superview == host) return;
	if (!gView) {
		// The terminal's session is saved here between launches (before the view, which may resume it).
		SlippyTerminal.shared.stateDirectory = NS(slippy::platform::SupportDir());
		gView = [[SlippyPanelView alloc] initWithFrame:host.bounds];
		gView.connectionInfo = ^NSString* { return gCallbacks.connectionInfo ? NS(gCallbacks.connectionInfo()) : @""; };
		NSString* version = NS(kSlippyVersion);   // "0.1.0" -> "0.1"
		if ([version hasSuffix:@".0"]) version = [version substringToIndex:version.length - 2];
		gView.version = version;
		// Opening the terminal drawer makes the panel taller (when Illustrator lets it).
		gView.onDrawer = ^(BOOL open, CGFloat extra) {
			AISize size;
			if (!gPanel || sAIPanel->GetSize(gPanel, size)) return;
			size.height = (AIReal) std::max(260.0, size.height + (open ? extra : -extra));
			sAIPanel->SetSize(gPanel, size);
		};
		[gView setStatus:gStatus listening:gListening];
		[gView setPaused:gPaused];
	}
	[gView removeFromSuperview];
	gView.frame = host.bounds;
	[host addSubview:gView];
}
} // namespace

void PanelAttach(AIPanelRef panel, PanelCallbacks callbacks)
{
	@autoreleasepool {
		gPanel = panel;
		gCallbacks = std::move(callbacks);
		AISize minSize = {240, 300}, pref = {290, 480}, maxSize = {700, 2400};
		sAIPanel->SetSizes(gPanel, minSize, pref, pref, maxSize);
		Install();
	}
}

void PanelDetach()
{
	@autoreleasepool {
		[SlippyTerminal.shared shutdown];   // Illustrator is quitting: save the terminal for next time
		[gView removeFromSuperview];
		gView = nil;
		gPanel = nullptr;
	}
}

void PanelSetStatus(const std::string& text, bool listening)
{
	@autoreleasepool {
		// Agents started in the terminal find Slippy here.
		if (listening && gCallbacks.connectionInfo) SlippyTerminal.shared.environment = @{@"SLIPPY_URL": NS(gCallbacks.connectionInfo())};
		gStatus = NS(text);
		gListening = listening;
		Install();
		[gView setStatus:gStatus listening:gListening];
	}
}

void PanelSetPaused(bool paused)
{
	@autoreleasepool {
		gPaused = paused;
		Install();
		[gView setPaused:paused];
		if (!paused) [gView setStatus:gStatus listening:gListening];
	}
}

void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent)
{
	@autoreleasepool {
		Install();
		[gView call:NS(method) line:NS(line) ok:ok edit:changesDocument ms:milliseconds agent:NS(agent)];
	}
}

static bool HideIn(NSMenu* menu, NSString* title)
{
	bool hid = false;
	for (NSMenuItem* item in menu.itemArray) {
		if ([item.title isEqualToString:title]) { item.hidden = YES; hid = true; }
		if (item.submenu && HideIn(item.submenu, title)) hid = true;
	}
	return hid;
}

void HideMenuItemTitled(const std::string& title)
{
	@autoreleasepool {
		HideIn(NSApp.mainMenu, NS(title));
	}
}
