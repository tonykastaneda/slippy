// `make preview`: the KAGE panel in a plain window with made-up agent calls,
// to watch Kage without installing into Illustrator.
//   build/KAGEPreview                 live: bursts of calls, quiet spells (he dozes off)
//   build/KAGEPreview --snapshot DIR  still PNGs of each face (for checking the drawing)

#import "KAGEPanelView.h"
#include "Json.h"
#include "Narrate.h"

// A made-up agent session: method, params, result, error ("" = worked).
struct Fake { const char* method; const char* params; const char* result; const char* error; };
static const Fake kSession[] = {
	{"app.info", "{}", "{}", ""},
	{"document.new", "{\"width\":400,\"height\":400,\"title\":\"Poster\"}", "{}", ""},
	{"shape.ellipse", "{\"x\":75,\"y\":-75,\"width\":250,\"height\":250,\"name\":\"Kage body\"}", "{\"name\":\"Kage body\"}", ""},
	{"path.create", "{\"points\":[[1,2],[3,4]],\"name\":\"left eye\"}", "{\"name\":\"left eye\"}", ""},
	{"text.create", "{\"position\":[30,-95],\"contents\":\"Z\"}", "{}", ""},
	{"art.tree", "{}", "[]", ""},
	{"art.group", "{\"ids\":[\"1\",\"2\",\"3\"],\"name\":\"Kage\"}", "{\"name\":\"Kage\"}", ""},
	{"art.transform", "{\"id\":\"4\",\"rotate\":15}", "[{\"type\":\"group\",\"name\":\"Kage\"}]", ""},
	{"art.set", "{\"id\":\"1\",\"fill\":\"#437BFA\"}", "[{\"type\":\"path\",\"name\":\"Kage body\"}]", ""},
	{"art.get", "{\"id\":\"99\"}", "null", "no art with id 99 in the active document"},
	{"layer.create", "{\"name\":\"Agents\"}", "{\"name\":\"Agents\"}", ""},
	{"shape.rect", "{\"x\":0,\"y\":0,\"width\":120,\"height\":40}", "{}", ""},
	{"history.undo", "{}", "\"undone\"", ""},
	{"document.save", "{\"path\":\"/Users/Shared/Poster.ai\"}", "{}", ""},
};
static const int kSessionCount = sizeof kSession / sizeof kSession[0];

static void Play(KAGEPanelView* panel, const Fake& f)
{
	std::string line = kage::Narrate(f.method, json::Parse(f.params), json::Parse(f.result), f.error);
	NSString* m = [NSString stringWithUTF8String:f.method];
	bool edit = ![@[@"app.info", @"art.tree", @"art.get"] containsObject:m];
	[panel call:m line:[NSString stringWithUTF8String:line.c_str()] ok:!*f.error edit:edit ms:arc4random_uniform(40)];
}

@interface Driver : NSObject
@property (nonatomic, strong) KAGEPanelView* panel;
- (void)burst;
@end

@implementation Driver {
	int _next;
}
// A burst of 3-7 calls from the session, then 2-10 s of quiet (over 6 s and Kage dozes off).
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
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (quiet * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self burst]; });
}
@end

static void Snapshot(NSView* view, NSString* path)
{
	[view layoutSubtreeIfNeeded];
	[view displayIfNeeded];
	NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
	CGContextRef ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep].CGContext;
	CGContextTranslateCTM(ctx, 0, view.bounds.size.height);   // the panel view is flipped
	CGContextScaleCTM(ctx, 1, -1);
	[view.layer renderInContext:ctx];
	[[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}

int main(int argc, const char* argv[])
{
	@autoreleasepool {
		NSApplication* app = NSApplication.sharedApplication;
		app.activationPolicy = NSApplicationActivationPolicyRegular;
		NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 260, 400)
			styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
		win.title = @"KAGE preview";
		win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];   // like Illustrator's dark UI
		win.backgroundColor = [NSColor colorWithWhite:0.22 alpha:1];
		KAGEPanelView* panel = [[KAGEPanelView alloc] initWithFrame:win.contentView.bounds];
		[win.contentView addSubview:panel];
		[panel setStatus:@"Preview - made-up calls" listening:YES];
		panel.connectionInfo = ^NSString* { return @"(preview)"; };

		if (argc == 3 && !strcmp(argv[1], "--snapshot")) {
			// Each face after its blink has finished (a snapshot shows model values, not
			// the animation in flight).
			NSString* dir = [NSString stringWithUTF8String:argv[2]];
			auto wait = [](double s) { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:s]]; };
			wait(0.3);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"1-asleep.png"]);
			for (int i = 0; i < 9; i++) Play(panel, kSession[i]);
			wait(0.3);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"2-waking.png"]);
			wait(0.6);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"3-working.png"]);
			Play(panel, kSession[9]);
			wait(0.3);
			Snapshot(panel, [dir stringByAppendingPathComponent:@"4-ouch.png"]);
			return 0;
		}

		[win makeKeyAndOrderFront:nil];
		[app activateIgnoringOtherApps:YES];
		Driver* driver = [Driver new];
		driver.panel = panel;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [driver burst]; });
		[app run];
	}
	return 0;
}
