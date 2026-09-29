#ifndef __KAGE_PANEL_VIEW_H__
#define __KAGE_PANEL_VIEW_H__

// The panel's Cocoa views, free of the Illustrator SDK so `make preview` can
// show them in a plain window. KAGEPanel.mm puts them in the docked panel.

#import <Cocoa/Cocoa.h>

// Kage, the mascot. Sleeps (— —, Z's) when nothing's happening, wakes (^ ^,
// a hop) on the first call, works (o o, looking around - faster when busy),
// winces (> <) on an error, and dozes off again after a quiet spell.
@interface KAGEMascotView : NSView
@property (nonatomic) NSTimeInterval sleepAfter;              // quiet seconds before dozing (default 6)
- (void)setAvailable:(BOOL)available paused:(BOOL)paused;     // either NO: stays asleep, dimmed
- (void)callArrived:(NSColor*)color ok:(BOOL)ok;
- (void)setBusy:(double)busy;                                 // 0..1, recent call rate
@end

@interface KAGEPanelView : NSView
@property (nonatomic, copy) void (^onPause)(BOOL paused);
@property (nonatomic, copy) NSString* (^connectionInfo)(void);
- (void)setStatus:(NSString*)text listening:(BOOL)listening;
// line: what happened in plain English; method + ms go in the row's tooltip.
- (void)call:(NSString*)method line:(NSString*)line ok:(BOOL)ok edit:(BOOL)edit ms:(double)ms;
@end

#endif // __KAGE_PANEL_VIEW_H__
