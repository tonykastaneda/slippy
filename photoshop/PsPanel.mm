#include "PsPanel.h"
#include "Platform.h"
#include "Version.h"

#import "SlippyPanelView.h"
#import "Terminal.h"

#include <algorithm>

// Hosts SlippyPanelView (Source/SlippyPanelView.mm) in a floating window.

namespace {
PanelCallbacks gCallbacks;
SlippyPanelView* gView = nil;
NSPanel* gWindow = nil;
NSString* gStatus = @"Starting…";
BOOL gListening = NO;
BOOL gPaused = NO;
NSString* const kOpenKey = @"SlippyPanelOpen";

NSString* NS(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
} // namespace

@interface SlippyPsPanelController : NSObject <NSWindowDelegate, NSMenuItemValidation>
@end

static SlippyPsPanelController* gController = nil;

@implementation SlippyPsPanelController
- (void)toggle:(id)sender
{
	if (gWindow.visible) [gWindow orderOut:nil];
	else PanelShow();
	[NSUserDefaults.standardUserDefaults setBool:gWindow.visible forKey:kOpenKey];
}
- (void)togglePause:(id)sender
{
	gPaused = !gPaused;
	PanelSetPaused(gPaused);
	if (gCallbacks.onPause) gCallbacks.onPause(gPaused);
}
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
	if (item.action == @selector(toggle:)) item.state = gWindow.visible ? NSControlStateValueOn : NSControlStateValueOff;
	if (item.action == @selector(togglePause:)) item.state = gPaused ? NSControlStateValueOn : NSControlStateValueOff;
	return YES;
}
- (BOOL)windowShouldClose:(NSWindow*)sender
{
	[sender orderOut:nil];   // keep it (and the terminal) alive; Window > Slippy brings it back
	[NSUserDefaults.standardUserDefaults setBool:NO forKey:kOpenKey];
	return NO;
}
@end

namespace {

void Build()
{
	if (gWindow) return;
	// The terminal's session is saved here between launches (before the view, which may resume it).
	SlippyTerminal.shared.stateDirectory = NS(slippy::platform::SupportDir());
	NSRect frame = NSMakeRect(0, 0, 290, 480);
	gWindow = [[NSPanel alloc] initWithContentRect:frame
		styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskUtilityWindow
		backing:NSBackingStoreBuffered defer:YES];
	gWindow.title = @"Slippy";
	gWindow.floatingPanel = YES;
	gWindow.hidesOnDeactivate = YES;   // like Photoshop's own panels
	gWindow.releasedWhenClosed = NO;
	gWindow.contentMinSize = NSMakeSize(240, 300);
	gWindow.delegate = gController;
	gWindow.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorMoveToActiveSpace;
	if (![gWindow setFrameUsingName:@"SlippyPhotoshopPanel"]) {
		// The first time: inside Photoshop's window, near its top-right corner.
		NSWindow* main = nil;
		for (NSWindow* w in NSApp.windows)
			if (w.visible && w != gWindow && (!main || w.frame.size.width * w.frame.size.height > main.frame.size.width * main.frame.size.height)) main = w;
		NSRect host = main ? main.frame : NSScreen.mainScreen.visibleFrame;
		[gWindow setFrameOrigin:NSMakePoint(NSMaxX(host) - frame.size.width - 340, NSMaxY(host) - frame.size.height - 140)];
	}
	gWindow.frameAutosaveName = @"SlippyPhotoshopPanel";

	[SlippyPanelView setGroups:@[@"document", @"layer", @"select", @"edit", @"filter", @"text", @"ps", @"history", @"commands", @"app"]];
	gView = [[SlippyPanelView alloc] initWithFrame:gWindow.contentView.bounds];
	gView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	gView.connectionInfo = ^NSString* { return gCallbacks.connectionInfo ? NS(gCallbacks.connectionInfo()) : @""; };
	NSString* version = NS(kSlippyVersion);   // "0.1.0" -> "0.1"
	if ([version hasSuffix:@".0"]) version = [version substringToIndex:version.length - 2];
	gView.version = version;
	// Opening the terminal drawer makes the window taller (from its top edge down).
	gView.onDrawer = ^(BOOL open, CGFloat extra) {
		NSRect f = gWindow.frame;
		CGFloat height = std::max<CGFloat>(300, f.size.height + (open ? extra : -extra));
		f.origin.y += f.size.height - height;
		f.size.height = height;
		[gWindow setFrame:f display:YES animate:NO];
	};
	NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Slippy"];
	[menu addItemWithTitle:@"Pause agents" action:@selector(togglePause:) keyEquivalent:@""].target = gController;
	gView.menu = menu;
	[gView setStatus:gStatus listening:gListening];
	[gView setPaused:gPaused];
	gWindow.contentView = gView;
}

// Window > Slippy. Photoshop can rebuild its menus (workspaces, languages),
// so this checks every few seconds that the item is still there.
void InstallMenuItem()
{
	NSMenu* main = NSApp.mainMenu;
	NSMenu* window = nil;
	// Photoshop's top-level items have no titles of their own; their submenus do.
	for (NSMenuItem* item in main.itemArray)
		if ([item.submenu.title isEqualToString:@"Window"] || [item.title isEqualToString:@"Window"]) { window = item.submenu; break; }
	if (!window) window = NSApp.windowsMenu;
	if (!window) return;
	for (NSMenuItem* item in window.itemArray) if (item.action == @selector(toggle:) && item.target == gController) return;
	NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"Slippy" action:@selector(toggle:) keyEquivalent:@""];
	item.target = gController;
	// After Photoshop's own panels' "Properties"/"Paths"... or at the end.
	[window addItem:[NSMenuItem separatorItem]];
	[window addItem:item];
}

void WatchMenu()
{
	InstallMenuItem();
	slippy::platform::MainThreadAfter(5, [] { if (gController) WatchMenu(); });
}

} // namespace

void PanelInit(PanelCallbacks callbacks)
{
	@autoreleasepool {
		gCallbacks = std::move(callbacks);
		gController = [SlippyPsPanelController new];
		[NSUserDefaults.standardUserDefaults registerDefaults:@{kOpenKey: @YES}];
		WatchMenu();
		// Open it if it was open when Photoshop last quit (and on the very first
		// launch) - unless the docked panel turns up first.
		if ([NSUserDefaults.standardUserDefaults boolForKey:kOpenKey])
			slippy::platform::MainThreadAfter(6, [] { if (!gCallbacks.docked || !gCallbacks.docked()) PanelShow(); });
	}
}

void PanelShow()
{
	@autoreleasepool {
		Build();
		[gWindow orderFront:nil];
	}
}

void PanelHide()
{
	@autoreleasepool {
		[gWindow orderOut:nil];   // its "open" setting stays, for when the docked panel isn't there
	}
}

void PanelShutdown()
{
	@autoreleasepool {
		[SlippyTerminal.shared shutdown];
		if (gWindow) [NSUserDefaults.standardUserDefaults setBool:gWindow.visible forKey:kOpenKey];
		[gWindow orderOut:nil];
		gWindow.contentView = nil;
		gView = nil;
		gWindow = nil;
		gController = nil;
	}
}

void PanelSetStatus(const std::string& text, bool listening)
{
	@autoreleasepool {
		// Agents started in the terminal find Slippy here.
		if (listening && gCallbacks.connectionInfo) SlippyTerminal.shared.environment = @{@"SLIPPY_URL": NS(gCallbacks.connectionInfo())};
		gStatus = NS(text);
		gListening = listening;
		[gView setStatus:gStatus listening:gListening];
	}
}

void PanelSetPaused(bool paused)
{
	@autoreleasepool {
		gPaused = paused;
		[gView setPaused:paused];
		if (!paused) [gView setStatus:gStatus listening:gListening];
	}
}

void PanelCall(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent)
{
	@autoreleasepool {
		[gView call:NS(method) line:NS(line) ok:ok edit:changesDocument ms:milliseconds agent:NS(agent)];
	}
}
