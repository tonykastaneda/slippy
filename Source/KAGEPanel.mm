#include "IllustratorSDK.h"
#include "KAGEPanel.h"
#include "KAGESuites.h"

#import "KAGEPanelView.h"

// Hosts KAGEPanelView (KAGEPanelView.mm) in Illustrator's docked panel.

// ------------------------------------------------------------------ C++ side

namespace {
AIPanelRef gPanel = nullptr;
KAGEPanelView* gView = nil;
PanelCallbacks gCallbacks;
NSString* gStatus = @"Starting…";
BOOL gListening = NO;

NSString* NS(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

void Install()
{
	if (!gPanel || !sAIPanel) return;
	__autoreleasing AIPanelPlatformWindow host = nil;
	if (sAIPanel->GetPlatformWindow(gPanel, host) || !host) return;
	if (gView && gView.superview == host) return;
	if (!gView) {
		gView = [[KAGEPanelView alloc] initWithFrame:host.bounds];
		gView.onPause = ^(BOOL paused) { if (gCallbacks.setPaused) gCallbacks.setPaused(paused); };
		gView.connectionInfo = ^NSString* { return gCallbacks.connectionInfo ? NS(gCallbacks.connectionInfo()) : @""; };
		[gView setStatus:gStatus listening:gListening];
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
		AISize minSize = {220, 260}, pref = {260, 380}, maxSize = {600, 2000};
		sAIPanel->SetSizes(gPanel, minSize, pref, pref, maxSize);
		Install();
	}
}

void PanelDetach()
{
	@autoreleasepool {
		[gView removeFromSuperview];
		gView = nil;
		gPanel = nullptr;
	}
}

void PanelSetStatus(const std::string& text, bool listening)
{
	@autoreleasepool {
		gStatus = NS(text);
		gListening = listening;
		Install();
		[gView setStatus:gStatus listening:gListening];
	}
}

void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line)
{
	@autoreleasepool {
		Install();
		[gView call:NS(method) line:NS(line) ok:ok edit:changesDocument ms:milliseconds];
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
