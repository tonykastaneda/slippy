#ifndef __SLIPPY_PANEL_VIEW_H__
#define __SLIPPY_PANEL_VIEW_H__

// The panel's Cocoa views, free of the Illustrator SDK so `make preview` can
// show them in a plain window. SlippyPanel.mm puts them in the docked panel.

#import <Cocoa/Cocoa.h>

// Slippy, the mascot. Sleeps (— —, Z's) when nothing's happening, wakes (^ ^,
// a hop) on the first call, works (o o, looking around - faster when busy),
// winces (> <) on an error, and dozes off again after a quiet spell.
@interface SlippyMascotView : NSView
@property (nonatomic) NSTimeInterval sleepAfter;              // quiet seconds before dozing (default 6)
- (void)setAvailable:(BOOL)available paused:(BOOL)paused;     // either NO: stays asleep, dimmed
- (void)callArrived:(NSColor*)color ok:(BOOL)ok;
- (void)setBusy:(double)busy;                                 // 0..1, recent call rate
@end

@interface SlippyPanelView : NSView
@property (nonatomic, copy) NSString* (^connectionInfo)(void);
// The terminal drawer opened (or closed): make the panel taller by extraHeight (or shorter).
@property (nonatomic, copy) void (^onDrawer)(BOOL open, CGFloat extraHeight);
@property (nonatomic, copy) NSString* version;   // shown as "v.0.1"
// The ten command groups the activity bars stand for, before the first panel
// is made (a method's group is its first word: "layer.set" -> "layer").
+ (void)setGroups:(NSArray<NSString*>*)groups;
- (void)setStatus:(NSString*)text listening:(BOOL)listening;
- (void)setPaused:(BOOL)paused;   // from the panel's flyout menu
- (void)toggleDrawer;            // the terminal drawer (also the handle at the bottom)
@property (nonatomic, readonly) BOOL drawerOpen;   // open from the start when a terminal is resumed
+ (CGFloat)drawerHeight;         // how much taller the panel is with the drawer open
// line: what happened in plain English; method + ms go in the row's tooltip.
// agent: who sent it ("Claude", "Codex"...), shown beside the counts.
- (void)call:(NSString*)method line:(NSString*)line ok:(BOOL)ok edit:(BOOL)edit ms:(double)ms agent:(NSString*)agent;
- (void)call:(NSString*)method line:(NSString*)line ok:(BOOL)ok edit:(BOOL)edit ms:(double)ms;
@end

#endif // __SLIPPY_PANEL_VIEW_H__
