// `make preview`: the Slippy panel in a plain window with made-up agent calls,
// to watch Slippy without installing into Illustrator.
//   build/SlippyPreview                 live: bursts of calls, quiet spells (he dozes off)
//   build/SlippyPreview --once          one burst, then quiet
//   build/SlippyPreview --quiet         no calls at all (click Slippy to wake him)
//   build/SlippyPreview --snapshot DIR  still PNGs of each face (for checking the drawing)

#import "SlippyPanelView.h"
#include "Json.h"
#include "Narrate.h"

// A made-up agent session: method, params, result, error ("" = worked).
struct Fake { const char* method; const char* params; const char* result; const char* error; };
static const Fake kSession[] = {
	{"app.info", "{}", "{}", ""},
	{"document.new", "{\"width\":400,\"height\":400,\"title\":\"Poster\"}", "{}", ""},
	{"shape.ellipse", "{\"x\":75,\"y\":-75,\"width\":250,\"height\":250,\"name\":\"Slippy body\"}", "{\"name\":\"Slippy body\"}", ""},
	{"path.create", "{\"points\":[[1,2],[3,4]],\"name\":\"left eye\"}", "{\"name\":\"left eye\"}", ""},
	{"text.create", "{\"position\":[30,-95],\"contents\":\"Z\"}", "{}", ""},
	{"art.tree", "{}", "[]", ""},
	{"art.group", "{\"ids\":[\"1\",\"2\",\"3\"],\"name\":\"Slippy\"}", "{\"name\":\"Slippy\"}", ""},
	{"art.transform", "{\"id\":\"4\",\"rotate\":15}", "[{\"type\":\"group\",\"name\":\"Slippy\"}]", ""},
	{"art.set", "{\"id\":\"1\",\"fill\":\"#437BFA\"}", "[{\"type\":\"path\",\"name\":\"Slippy body\"}]", ""},
	{"art.get", "{\"id\":\"99\"}", "null", "no art with id 99 in the active document"},
	{"layer.create", "{\"name\":\"Agents\"}", "{\"name\":\"Agents\"}", ""},
	{"shape.rect", "{\"x\":0,\"y\":0,\"width\":120,\"height\":40}", "{}", ""},
	{"history.undo", "{}", "\"undone\"", ""},
	{"document.save", "{\"path\":\"/Users/Shared/Poster.ai\"}", "{}", ""},
};
static const int kSessionCount = sizeof kSession / sizeof kSession[0];

static void Play(SlippyPanelView* panel, const Fake& f)
{
	std::string line = slippy::Narrate(f.method, json::Parse(f.params), json::Parse(f.result), f.error);
	NSString* m = [NSString stringWithUTF8String:f.method];
	bool edit = ![@[@"app.info", @"art.tree", @"art.get"] containsObject:m];
	static NSArray* agents = @[@"Claude", @"Claude", @"Codex", @"Gemini"];   // made-up callers
	static int turn = 0;
	[panel call:m line:[NSString stringWithUTF8String:line.c_str()] ok:!*f.error edit:edit ms:arc4random_uniform(40) agent:agents[turn++ / 5 % agents.count]];
}

@interface Driver : NSObject
@property (nonatomic, strong) SlippyPanelView* panel;
@property (nonatomic) BOOL once;
- (void)burst;
@end

@implementation Driver {
	int _next;
}
// A burst of 3-7 calls from the session, then 2-10 s of quiet (over 6 s and Slippy dozes off).
- (void)burst
{
	int n = 3 + (int) arc4random_uniform(5);
	for (int i = 0; i < n; i++) {
		const Fake& f = kSession[_next++ % kSessionCount];
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (i * (0.25 + arc4random_uniform(40) / 100.0) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
			Play(self.panel, f);
		});
	}
	double quiet = n * 0.5 + 2 + arc4random_uniform(8);
	if (self.once) return;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (quiet * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self burst]; });
}
@end

static void Snapshot(NSView* view, NSString* path)
{
	// A real screenshot of the window (screencapture), so it looks exactly as on screen.
	[view layoutSubtreeIfNeeded];
	[view displayIfNeeded];
	[[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
	NSString* cmd = [NSString stringWithFormat:@"/usr/sbin/screencapture -x -o -l%ld '%@'", (long) view.window.windowNumber, path];
	system(cmd.UTF8String);
}

int main(int argc, const char* argv[])
{
	@autoreleasepool {
		NSApplication* app = NSApplication.sharedApplication;
		app.activationPolicy = NSApplicationActivationPolicyRegular;
		NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 290, 480)
			styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
		win.title = @"Slippy preview";
		win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];   // like Illustrator's dark UI
		win.backgroundColor = [NSColor colorWithWhite:0.22 alpha:1];
		SlippyPanelView* panel = [[SlippyPanelView alloc] initWithFrame:win.contentView.bounds];
		[win.contentView addSubview:panel];
		[panel setStatus:@"Listening on 127.0.0.1:7331" listening:YES];
		panel.connectionInfo = ^NSString* { return @"(preview)"; };
		panel.onDrawer = ^(BOOL open, CGFloat extra) {   // the window grows like the panel would
			NSRect f = win.frame;
			f.size.height += open ? extra : -extra;
			f.origin.y -= open ? extra : -extra;
			[win setFrame:f display:YES animate:YES];
		};

		if (argc == 3 && !strcmp(argv[1], "--snapshot")) {
			[win makeKeyAndOrderFront:nil];
			[app activateIgnoringOtherApps:YES];
			// Each face after its blink has finished (a snapshot shows model values, not
			// the animation in flight).
			NSString* dir = [NSString stringWithUTF8String:argv[2]];
			auto wait = [](double s) { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:s]]; };
			wait(0.3);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"1-asleep.png"]);
			for (int i = 0; i < 9; i++) Play(panel, kSession[i]);
			wait(0.3);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"2-waking.png"]);
			wait(1.2);   // the stretch and hop, then eyes open
			Snapshot(panel, [dir stringByAppendingPathComponent:@"3-working.png"]);
			Play(panel, kSession[9]);
			wait(0.3);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"4-ouch.png"]);
			// The terminal drawer open (the window grows like the panel would).
			panel.onDrawer = ^(BOOL open, CGFloat extra) {
				NSRect f = win.frame;
				f.size.height += open ? extra : -extra;
				f.origin.y -= open ? extra : -extra;
				[win setFrame:f display:NO];
				panel.frame = win.contentView.bounds;
			};
			[panel toggleDrawer];
			wait(0.4);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"5-drawer.png"]);
			return 0;
		}

		[win makeKeyAndOrderFront:nil];
		[app activateIgnoringOtherApps:YES];
		Driver* driver = [Driver new];
		driver.panel = panel;
		driver.once = argc == 2 && !strcmp(argv[1], "--once");   // one burst, then quiet: watch Slippy doze off
		if (argc == 2 && !strcmp(argv[1], "--quiet")) { [app run]; return 0; }   // no calls: click Slippy
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [driver burst]; });
		[app run];
	}
	return 0;
}
