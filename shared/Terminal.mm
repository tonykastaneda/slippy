#import "Terminal.h"
#import <WebKit/WebKit.h>

#include <fcntl.h>
#include <libproc.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

#include <algorithm>
#include <string>
#include <vector>

extern char** environ;

namespace {
const NSUInteger kScrollbackCap = 2 << 20;   // in memory
const NSUInteger kSavedCap = 1 << 20;        // on disk, for the next launch
const char* kResumed = "\r\n\x1b[2m- resumed from your last Illustrator session -\x1b[0m\r\n";
const char* kEnded = "\r\n\x1b[2m[the shell ended - type anything for a new one]\x1b[0m\r\n";
}

// ------------------------------------------------------------------ the shell

@implementation SlippyTerminal {
	int _master;
	pid_t _pid;
	dispatch_source_t _read, _exitWatch, _autosave;
	NSMutableData* _buffer;
	int _cols, _rows;
	BOOL _dirty;
}

+ (instancetype)shared
{
	static SlippyTerminal* shared = [SlippyTerminal new];
	return shared;
}

- (instancetype)init
{
	if ((self = [super init])) {
		_master = -1;
		_buffer = [NSMutableData data];
		_cols = 80;
		_rows = 24;
	}
	return self;
}

- (BOOL)running { return _pid > 0; }
- (NSData*)scrollback { return _buffer; }

- (NSString*)savedScrollbackPath { return [self.stateDirectory stringByAppendingPathComponent:@"terminal-scrollback.bin"]; }
- (NSString*)savedInfoPath { return [self.stateDirectory stringByAppendingPathComponent:@"terminal.json"]; }

- (BOOL)hasSavedSession
{
	return self.stateDirectory && [NSFileManager.defaultManager fileExistsAtPath:[self savedInfoPath]];
}

- (void)forgetSaved
{
	if (!self.stateDirectory) return;
	[NSFileManager.defaultManager removeItemAtPath:[self savedScrollbackPath] error:nil];
	[NSFileManager.defaultManager removeItemAtPath:[self savedInfoPath] error:nil];
}

- (void)append:(NSData*)bytes
{
	[_buffer appendData:bytes];
	if (_buffer.length > kScrollbackCap) [_buffer replaceBytesInRange:NSMakeRange(0, _buffer.length - kScrollbackCap * 3 / 4) withBytes:nullptr length:0];
	_dirty = YES;
	[self.sink terminalOutput:bytes];
}

- (void)appendText:(const char*)text { [self append:[NSData dataWithBytes:text length:strlen(text)]]; }

// The shell's current folder, so a resumed session starts where it was.
- (NSString*)currentDirectory
{
	if (_pid <= 0) return nil;
	struct proc_vnodepathinfo info;
	if (proc_pidinfo(_pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof info) != sizeof info) return nil;
	return [NSString stringWithUTF8String:info.pvi_cdir.vip_path];
}

- (void)start
{
	if (_pid > 0) return;
	NSString* cwd = NSHomeDirectory();
	if (_buffer.length == 0 && [self hasSavedSession]) {
		NSData* saved = [NSData dataWithContentsOfFile:[self savedScrollbackPath]];
		NSDictionary* info = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:[self savedInfoPath]] options:0 error:nil];
		BOOL isDir = NO;
		NSString* was = [info isKindOfClass:NSDictionary.class] ? info[@"cwd"] : nil;
		if ([was isKindOfClass:NSString.class] && [NSFileManager.defaultManager fileExistsAtPath:was isDirectory:&isDir] && isDir) cwd = was;
		if (saved.length) {
			[_buffer appendData:saved];
			[_buffer appendBytes:kResumed length:strlen(kResumed)];
			[self.sink terminalOutput:_buffer];
		}
		[self forgetSaved];   // live now; saved again on quit
	}

	// Everything the child needs is made before fork: only exec-safe calls after it.
	NSMutableDictionary<NSString*, NSString*>* env = [NSProcessInfo.processInfo.environment mutableCopy];
	env[@"TERM"] = @"xterm-256color";
	env[@"COLORTERM"] = @"truecolor";
	env[@"TERM_PROGRAM"] = @"Slippy";
	if (!env[@"LANG"]) env[@"LANG"] = @"en_US.UTF-8";
	[env addEntriesFromDictionary:self.environment ?: @{}];
	std::vector<std::string> pairs;
	for (NSString* k in env) pairs.push_back(std::string(k.UTF8String) + "=" + env[k].UTF8String);
	std::vector<char*> envp;
	for (auto& p : pairs) envp.push_back(&p[0]);
	envp.push_back(nullptr);
	std::string shell = env[@"SHELL"].length ? env[@"SHELL"].UTF8String : "/bin/zsh";
	std::string login = "-" + shell.substr(shell.find_last_of('/') + 1);   // a login shell, like Terminal's
	std::string dir = cwd.fileSystemRepresentation;
	char* argv[] = {&login[0], nullptr};

	struct winsize ws = {(unsigned short) _rows, (unsigned short) _cols, 0, 0};
	int maxfd = std::min(getdtablesize(), 65536);
	int master = -1;
	pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
	if (pid < 0) { [self appendText:"\r\n[couldn't start a shell]\r\n"]; return; }
	if (pid == 0) {
		// Only the terminal: not Illustrator's files, or Slippy's listening socket
		// (a program left running from here would keep the port after Illustrator quits).
		for (int fd = 3; fd < maxfd; fd++) close(fd);
		chdir(dir.c_str());
		execve(shell.c_str(), argv, envp.data());
		_exit(127);
	}
	_pid = pid;
	_master = master;
	fcntl(_master, F_SETFL, fcntl(_master, F_GETFL) | O_NONBLOCK);

	__weak SlippyTerminal* weakSelf = self;
	_read = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t) _master, 0, dispatch_get_main_queue());
	dispatch_source_set_event_handler(_read, ^{ [weakSelf readAvailable]; });
	dispatch_resume(_read);
	_exitWatch = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, (uintptr_t) _pid, DISPATCH_PROC_EXIT, dispatch_get_main_queue());
	dispatch_source_set_event_handler(_exitWatch, ^{ [weakSelf shellExited]; });
	dispatch_resume(_exitWatch);
	if (!_autosave) {   // a crash loses a few seconds at most
		_autosave = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
		dispatch_source_set_timer(_autosave, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), 20 * NSEC_PER_SEC, 2 * NSEC_PER_SEC);
		dispatch_source_set_event_handler(_autosave, ^{ SlippyTerminal* s = weakSelf; if (s && s->_dirty) [s save]; });
		dispatch_resume(_autosave);
	}
}

- (void)readAvailable
{
	char chunk[65536];
	for (;;) {
		ssize_t n = read(_master, chunk, sizeof chunk);
		if (n > 0) { [self append:[NSData dataWithBytes:chunk length:(NSUInteger) n]]; continue; }
		break;   // EAGAIN, or the shell went away (the exit source reports that)
	}
}

// Stops watching the shell; doesn't signal it.
- (void)detach
{
	if (_read) { dispatch_source_cancel(_read); _read = nil; }
	if (_exitWatch) { dispatch_source_cancel(_exitWatch); _exitWatch = nil; }
	if (_master >= 0) { close(_master); _master = -1; }
}

- (void)shellExited
{
	[self readAvailable];
	int status = 0;
	waitpid(_pid, &status, WNOHANG);
	[self detach];
	_pid = 0;
	[self appendText:kEnded];
}

// Hang up on the shell (and everything it runs), then make sure it's gone.
- (void)endShell
{
	if (_pid <= 0) return;
	pid_t pid = _pid;
	[self detach];
	_pid = 0;
	kill(-pid, SIGHUP);
	kill(pid, SIGHUP);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		if (waitpid(pid, nullptr, WNOHANG) == 0) { kill(-pid, SIGKILL); kill(pid, SIGKILL); waitpid(pid, nullptr, 0); }
	});
}

- (void)write:(NSData*)input
{
	if (_pid <= 0) { [self start]; return; }   // the shell ended: any key starts a new one
	const char* p = (const char*) input.bytes;
	size_t left = input.length;
	for (int tries = 0; left > 0 && tries < 100; tries++) {
		ssize_t n = ::write(_master, p, left);
		if (n > 0) { p += n; left -= (size_t) n; }
		else if (n < 0 && errno != EAGAIN && errno != EINTR) break;
		else usleep(1000);
	}
}

- (void)resizeColumns:(int)cols rows:(int)rows
{
	if (cols < 2 || rows < 1) return;
	_cols = cols;
	_rows = rows;
	if (_master < 0) return;
	struct winsize ws = {(unsigned short) rows, (unsigned short) cols, 0, 0};
	ioctl(_master, TIOCSWINSZ, &ws);
}

- (NSString*)busyProcess
{
	if (_pid <= 0 || _master < 0) return nil;
	pid_t group = tcgetpgrp(_master);
	if (group <= 0 || group == _pid) return nil;
	char name[PROC_PIDPATHINFO_MAXSIZE] = {0};
	if (proc_name(group, name, sizeof name) <= 0) return @"a program";
	return [NSString stringWithUTF8String:name];
}

- (void)clear
{
	[self endShell];
	_buffer = [NSMutableData data];
	_dirty = NO;
	[self forgetSaved];
}

- (void)save
{
	_dirty = NO;
	if (!self.stateDirectory) return;
	if (_buffer.length == 0 && _pid <= 0) { [self forgetSaved]; return; }
	[NSFileManager.defaultManager createDirectoryAtPath:self.stateDirectory withIntermediateDirectories:YES attributes:nil error:nil];
	NSMutableData* tail = [NSMutableData data];
	NSUInteger from = _buffer.length > kSavedCap ? _buffer.length - kSavedCap : 0;
	[tail appendData:[_buffer subdataWithRange:NSMakeRange(from, _buffer.length - from)]];
	// Replayed later into a fresh screen: leave any full-screen program's
	// alternate screen and reset colors, so the prompt that follows is plain.
	const char* settle = "\x1b[0m\x1b[?1049l\x1b[?25h";
	[tail appendBytes:settle length:strlen(settle)];
	[tail writeToFile:[self savedScrollbackPath] atomically:YES];
	NSDictionary* info = @{@"cwd": [self currentDirectory] ?: NSHomeDirectory()};
	[[NSJSONSerialization dataWithJSONObject:info options:0 error:nil] writeToFile:[self savedInfoPath] atomically:YES];
	chmod([self savedScrollbackPath].fileSystemRepresentation, 0600);
}

- (void)shutdown
{
	if (_buffer.length || _pid > 0) [self save];
	[self endShell];
	if (_autosave) { dispatch_source_cancel(_autosave); _autosave = nil; }
}
@end

// ------------------------------------------------------------------ the view

// WKWebView keeps its script handler alive; this breaks the cycle.
@interface SlippyScriptProxy : NSObject <WKScriptMessageHandler>
@property (nonatomic, weak) id<WKScriptMessageHandler> target;
@end

@implementation SlippyScriptProxy
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m
{
	[self.target userContentController:c didReceiveScriptMessage:m];
}
@end

// Illustrator would take ⌘C / ⌘V / ⌘A for the artwork; while the terminal
// has the keyboard, they're the terminal's.
@interface SlippyTerminalWebView : WKWebView
@end

@implementation SlippyTerminalWebView
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (BOOL)performKeyEquivalent:(NSEvent*)event
{
	NSResponder* first = self.window.firstResponder;
	bool focused = first == self || ([first isKindOfClass:NSView.class] && [(NSView*) first isDescendantOf:self]);
	NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
	if (!focused || mods != NSEventModifierFlagCommand) return [super performKeyEquivalent:event];
	NSString* key = event.charactersIgnoringModifiers.lowercaseString;
	if ([key isEqualToString:@"c"]) {
		[self evaluateJavaScript:@"slippySelection()" completionHandler:^(id text, NSError*) {
			if (![text isKindOfClass:NSString.class] || ![text length]) return;
			[NSPasteboard.generalPasteboard clearContents];
			[NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];
		}];
		return YES;
	}
	if ([key isEqualToString:@"v"]) {
		NSString* text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
		if (!text.length) return YES;
		NSData* json = [NSJSONSerialization dataWithJSONObject:@[text] options:0 error:nil];
		NSString* arg = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
		[self evaluateJavaScript:[NSString stringWithFormat:@"slippyPaste(%@[0])", arg] completionHandler:nil];
		return YES;
	}
	if ([key isEqualToString:@"a"]) { [self evaluateJavaScript:@"slippySelectAll()" completionHandler:nil]; return YES; }
	return [super performKeyEquivalent:event];
}
@end

@interface SlippyTerminalView () <WKScriptMessageHandler>
@end

@implementation SlippyTerminalView {
	SlippyTerminalWebView* _web;
	BOOL _ready, _flushing;
	NSMutableData* _pending;
}

static NSString* gResourceDirectory;

+ (NSString*)resourceDirectory
{
	if (gResourceDirectory) return gResourceDirectory;
	NSFileManager* fm = NSFileManager.defaultManager;
	NSString* bundled = [[[NSBundle bundleForClass:self] resourcePath] stringByAppendingPathComponent:@"terminal"];
	if ([fm fileExistsAtPath:[bundled stringByAppendingPathComponent:@"terminal.html"]]) return bundled;
	// `make preview`: build/SlippyPreview, next to the repo's Resources.
	NSString* exe = NSProcessInfo.processInfo.arguments.firstObject.stringByResolvingSymlinksInPath;
	return [[[exe stringByDeletingLastPathComponent] stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Resources/terminal"];
}

+ (void)setResourceDirectory:(NSString*)dir { gResourceDirectory = [dir copy]; }

- (instancetype)initWithFrame:(NSRect)frame
{
	if ((self = [super initWithFrame:frame])) {
		self.wantsLayer = YES;
		self.layer.cornerRadius = 10;
		self.layer.masksToBounds = YES;
		self.layer.backgroundColor = [NSColor colorWithWhite:0.07 alpha:0.55].CGColor;
		_pending = [NSMutableData data];
	}
	return self;
}

- (void)load
{
	if (_web) return;
	WKWebViewConfiguration* config = [WKWebViewConfiguration new];
	SlippyScriptProxy* proxy = [SlippyScriptProxy new];
	proxy.target = self;
	[config.userContentController addScriptMessageHandler:proxy name:@"slippy"];
	_web = [[SlippyTerminalWebView alloc] initWithFrame:self.bounds configuration:config];
	_web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	[_web setValue:@NO forKey:@"drawsBackground"];   // the rounded well shows through
	[self addSubview:_web];
	NSString* dir = [SlippyTerminalView resourceDirectory];
	NSURL* page = [NSURL fileURLWithPath:[dir stringByAppendingPathComponent:@"terminal.html"]];
	[_web loadFileURL:page allowingReadAccessToURL:[NSURL fileURLWithPath:dir isDirectory:YES]];
}

- (void)attach
{
	SlippyTerminal.shared.sink = self;
	[self load];
	if (_ready) {
		[SlippyTerminal.shared start];
		[self.window makeFirstResponder:_web];
	}
}

- (void)reset
{
	[_pending setLength:0];
	if (_ready) [_web evaluateJavaScript:@"slippyReset()" completionHandler:nil];
}

- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m
{
	NSDictionary* msg = [m.body isKindOfClass:NSDictionary.class] ? m.body : nil;
	NSString* type = msg[@"type"];
	SlippyTerminal* t = SlippyTerminal.shared;
	if ([type isEqualToString:@"input"]) {
		NSString* data = msg[@"data"];
		if ([data isKindOfClass:NSString.class]) [t write:[data dataUsingEncoding:NSUTF8StringEncoding]];
	}
	else if ([type isEqualToString:@"resize"]) [t resizeColumns:[msg[@"cols"] intValue] rows:[msg[@"rows"] intValue]];
	else if ([type isEqualToString:@"ready"]) {
		// The page is up: size the shell, then show what it has said so far (or
		// what the last session said), then go live.
		_ready = YES;
		[t resizeColumns:[msg[@"cols"] intValue] rows:[msg[@"rows"] intValue]];
		t.sink = self;
		[_pending setLength:0];
		if (t.scrollback.length) [self terminalOutput:t.scrollback];
		[t start];   // resumes a saved session first, which arrives through terminalOutput
		[self.window makeFirstResponder:_web];
	}
}

// Output arrives in bursts; send it to the page once per turn of the run loop.
- (void)terminalOutput:(NSData*)bytes
{
	if (!_ready) return;   // the page sends 'ready', then gets the whole scrollback
	[_pending appendData:bytes];
	if (_flushing) return;
	_flushing = YES;
	dispatch_async(dispatch_get_main_queue(), ^{
		self->_flushing = NO;
		if (!self->_pending.length) return;
		NSString* b64 = [self->_pending base64EncodedStringWithOptions:0];
		[self->_pending setLength:0];
		[self->_web evaluateJavaScript:[NSString stringWithFormat:@"slippyWrite('%@')", b64] completionHandler:nil];
	});
}
@end
