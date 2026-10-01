#ifndef __SLIPPY_TERMINAL_H__
#define __SLIPPY_TERMINAL_H__

// The terminal in the Slippy panel's drawer (macOS). SlippyTerminal is the
// shell itself: one per Illustrator session, running in a pseudo-terminal
// whether or not the panel or drawer is showing. It keeps the scrollback,
// saves it with the shell's folder when Illustrator quits, and resumes from
// them on the next launch. Only clear (the drawer's chevron) ends it for good.
// SlippyTerminalView draws it in the drawer with xterm.js
// (Resources/terminal). No Illustrator SDK, so `make preview` runs it too.
// Main thread only.

#import <Cocoa/Cocoa.h>

@protocol SlippyTerminalSink
- (void)terminalOutput:(NSData*)bytes;
@end

@interface SlippyTerminal : NSObject
+ (instancetype)shared;
@property (nonatomic, copy) NSString* stateDirectory;               // where the session is saved between launches
@property (nonatomic, copy) NSDictionary<NSString*, NSString*>* environment;   // extra variables for the shell (SLIPPY_URL)
@property (nonatomic, weak) id<SlippyTerminalSink> sink;            // the view showing it, if any
@property (nonatomic, readonly) BOOL running;
@property (nonatomic, readonly) NSData* scrollback;                 // everything shown so far (capped)
- (BOOL)hasSavedSession;    // a session saved by the last Illustrator to resume
- (void)start;              // the shell, if it isn't running (resuming a saved session first)
- (void)write:(NSData*)input;
- (void)resizeColumns:(int)cols rows:(int)rows;
- (NSString*)busyProcess;   // a program running in the shell (not the shell itself), or nil
- (void)clear;              // end the shell, forget the scrollback and the saved session
- (void)save;               // write the session out for the next launch
- (void)shutdown;           // Illustrator is quitting: save, then end the shell
@end

// The drawer's content: xterm.js in a web view, attached to the shared session.
@interface SlippyTerminalView : NSView <SlippyTerminalSink>
@property (class, nonatomic, copy) NSString* resourceDirectory;     // Resources/terminal (found next to the code if unset)
- (void)attach;   // start / resume the shell and show it
- (void)reset;    // the session was cleared: blank the screen
@end

#endif // __SLIPPY_TERMINAL_H__
