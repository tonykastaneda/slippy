#import "KAGEPanelView.h"
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

// ------------------------------------------------------------------ look

namespace {

// Command groups, one bar each, in the order they're drawn.
const char* kGroups[] = {"document", "layer", "art", "shape", "path", "text", "menu", "action", "history", "app"};
const int kGroupCount = sizeof kGroups / sizeof kGroups[0];
const int kFeedRows = 8;
const CGFloat kPad = 12;

NSColor* ReadColor() { return NSColor.systemTealColor; }
NSColor* EditColor() { return NSColor.systemOrangeColor; }
NSColor* ErrorColor() { return NSColor.systemRedColor; }
NSColor* KageBlue() { return [NSColor colorWithSRGBRed:0x43 / 255.0 green:0x7B / 255.0 blue:0xFA / 255.0 alpha:1]; }   // from the mascot art

bool ReduceMotion() { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion; }

double Random(double lo, double hi) { return lo + (hi - lo) * (arc4random_uniform(10001) / 10000.0); }

int GroupOf(const std::string& method)
{
	std::string g = method.substr(0, method.find('.'));
	for (int i = 0; i < kGroupCount; i++) if (g == kGroups[i]) return i;
	return kGroupCount - 1;
}

NSTextField* Label(CGFloat size, NSFontWeight weight, NSColor* color)
{
	NSTextField* t = [NSTextField labelWithString:@""];
	t.font = [NSFont systemFontOfSize:size weight:weight];
	t.textColor = color;
	t.lineBreakMode = NSLineBreakByTruncatingTail;
	return t;
}

} // namespace

// ------------------------------------------------------------------ Kage
// Geometry is in units of the body's diameter D, measured off the mascot art;
// layer coordinates, so y grows upward.

namespace {

enum class Face { Sleep, Happy, Awake, Ouch };
enum class Mood { Asleep, Waking, Working, Dozing };

// Where the eye pair sits for each face (offset of the eyes from the body's center).
CGFloat EyeY(Face f) { return f == Face::Sleep ? -0.17 : f == Face::Happy ? 0.10 : 0.0; }
const CGFloat kEyeX = 0.21;

// One eye's stroke, centered on (0, 0). right: the right eye (Ouch mirrors).
CGPathRef EyePath(Face f, bool right, CGFloat D)
{
	CGMutablePathRef p = CGPathCreateMutable();
	switch (f) {
	case Face::Sleep:   // —
		CGPathMoveToPoint(p, nullptr, -0.105 * D, 0);
		CGPathAddLineToPoint(p, nullptr, 0.105 * D, 0);
		break;
	case Face::Happy:   // ^
		CGPathMoveToPoint(p, nullptr, -0.075 * D, -0.06 * D);
		CGPathAddLineToPoint(p, nullptr, 0, 0.07 * D);
		CGPathAddLineToPoint(p, nullptr, 0.075 * D, -0.06 * D);
		break;
	case Face::Awake:   // o
		CGPathAddEllipseInRect(p, nullptr, CGRectMake(-0.062 * D, -0.07 * D, 0.124 * D, 0.14 * D));
		break;
	case Face::Ouch: {  // > <
		CGFloat s = right ? -1 : 1;
		CGPathMoveToPoint(p, nullptr, -0.06 * D * s, 0.065 * D);
		CGPathAddLineToPoint(p, nullptr, 0.06 * D * s, 0);
		CGPathAddLineToPoint(p, nullptr, -0.06 * D * s, -0.065 * D);
		break;
	}
	}
	return p;
}

CGFloat EyeWidth(Face f, CGFloat D) { return (f == Face::Sleep ? 0.045 : f == Face::Awake ? 0.05 : 0.055) * D; }

// The three Z's of the art: offset from the body's center, font size.
struct ZSpot { CGFloat x, y, size; };
const ZSpot kZs[] = {{-0.33, 0.08, 0.13}, {-0.44, 0.25, 0.16}, {-0.52, 0.47, 0.21}};

} // namespace

@implementation KAGEMascotView {
	CALayer* _face;          // body + eyes: hops, breathes, shakes
	CAShapeLayer* _body;
	CALayer* _eyes;          // the pair: looks around
	CAShapeLayer* _eye[2];
	NSMutableArray<CATextLayer*>* _stillZs;   // Reduce Motion: the art's Z's, still
	NSMutableArray<CATextLayer*>* _sleepZs;   // Three staggered, continuously drifting Z's
	CGFloat _D;
	CGPoint _center;
	CGPoint _look;           // eye-pair offset, in D
	Face _shown;
	Mood _mood;
	int _blink;              // generation: a newer blink cancels an older one's second half
	double _busy;
	BOOL _available, _paused;
	NSTimer* _lookTimer;
	NSTimer* _sleepTimer;
}

- (instancetype)initWithFrame:(NSRect)frame
{
	if ((self = [super initWithFrame:frame])) {
		self.wantsLayer = YES;
		self.sleepAfter = 6;
		_face = [CALayer layer];
		_body = [CAShapeLayer layer];
		_eyes = [CALayer layer];
		[_face addSublayer:_body];
		[_face addSublayer:_eyes];
		for (int i = 0; i < 2; i++) {
			_eye[i] = [CAShapeLayer layer];
			_eye[i].fillColor = nil;
			_eye[i].strokeColor = NSColor.blackColor.CGColor;
			_eye[i].lineCap = kCALineCapButt;
			_eye[i].lineJoin = kCALineJoinMiter;
			[_eyes addSublayer:_eye[i]];
		}
		[self.layer addSublayer:_face];
		_stillZs = [NSMutableArray array];
		_sleepZs = [NSMutableArray array];
		for (int i = 0; i < 3; i++) {
			CATextLayer* z = [self makeZ];
			z.hidden = YES;
			[self.layer addSublayer:z];
			[_stillZs addObject:z];
			z = [self makeZ];
			z.hidden = YES;
			[self.layer addSublayer:z];
			[_sleepZs addObject:z];
		}
		_shown = Face::Sleep;
		_mood = Mood::Asleep;
		_available = YES;
		[self paintBody:KageBlue()];
	}
	return self;
}

- (void)dealloc
{
	[_lookTimer invalidate];
	[_sleepTimer invalidate];
}

- (void)viewDidMoveToWindow
{
	[super viewDidMoveToWindow];
	if (!self.window) {   // off screen: no timers running for nobody
		[_lookTimer invalidate]; _lookTimer = nil;
		return;
	}
	CGFloat scale = self.window.backingScaleFactor;
	for (CATextLayer* z in _stillZs) z.contentsScale = scale;
	for (CATextLayer* z in _sleepZs) z.contentsScale = scale;
	[self enterMood:_mood];
}

- (CATextLayer*)makeZ
{
	CATextLayer* z = [CATextLayer layer];
	z.string = @"Z";
	z.font = (__bridge CFTypeRef) [NSFont systemFontOfSize:12 weight:NSFontWeightHeavy];
	z.foregroundColor = NSColor.blackColor.CGColor;
	z.alignmentMode = kCAAlignmentCenter;
	z.contentsScale = self.window ? self.window.backingScaleFactor : 2;
	return z;
}

// ---- geometry

- (void)layout
{
	[super layout];
	NSSize s = self.bounds.size;
	CGFloat oldD = _D;
	_D = std::floor(std::min(s.width, s.height) * 0.64);
	// Low and to the right, leaving the upper left for the Z's.
	_center = CGPointMake(s.width - _D / 2 - s.width * 0.06, _D / 2 + s.height * 0.05);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	_face.bounds = CGRectMake(0, 0, _D, _D);
	_face.position = _center;
	_body.frame = _face.bounds;
	CGPathRef circle = CGPathCreateWithEllipseInRect(_face.bounds, nullptr);
	_body.path = circle;
	CGPathRelease(circle);
	_eyes.bounds = _face.bounds;
	[self placeEyes];
	[self drawFace];
	for (int i = 0; i < 3; i++) {
		CATextLayer* z = _stillZs[i];
		CGFloat size = kZs[i].size * _D;
		z.fontSize = size * 1.25;
		z.bounds = CGRectMake(0, 0, size * 1.4, size * 1.6);
		z.position = CGPointMake(_center.x + kZs[i].x * _D, _center.y + kZs[i].y * _D);
		z = _sleepZs[i];
		z.fontSize = kZs[2].size * _D * 1.25;
		z.bounds = CGRectMake(0, 0, kZs[2].size * _D * 1.4, kZs[2].size * _D * 1.6);
	}
	[CATransaction commit];
	if (_D != oldD && _mood == Mood::Asleep && _available && !_paused && self.window)
		[self startSnoring];
}

- (void)placeEyes
{
	_eyes.position = CGPointMake(_D / 2 + _look.x * _D, _D / 2 + _look.y * _D);
}

// Shape + spot of both eyes for _shown (no animation).
- (void)drawFace
{
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < 2; i++) {
		CGPathRef p = EyePath(_shown, i == 1, _D);
		_eye[i].path = p;
		CGPathRelease(p);
		_eye[i].lineWidth = EyeWidth(_shown, _D);
		_eye[i].bounds = CGRectZero;
		_eye[i].position = CGPointMake(_D / 2 + (i ? kEyeX : -kEyeX) * _D, _D / 2 + EyeY(_shown) * _D);
	}
	[CATransaction commit];
}

- (void)paintBody:(NSColor*)color
{
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{
		self->_body.fillColor = color.CGColor;
	}];
}

// ---- faces

// Eyes squeeze shut, change, open - how every face change reads as alive.
- (void)showFace:(Face)face
{
	if (face == _shown) return;
	_shown = face;
	int gen = ++_blink;
	if (ReduceMotion() || !self.window) { [self drawFace]; return; }
	[CATransaction begin];
	[CATransaction setAnimationDuration:0.07];
	[CATransaction setCompletionBlock:^{
		if (gen != self->_blink) return;
		[self drawFace];
		[CATransaction begin];
		[CATransaction setAnimationDuration:0.11];
		for (int i = 0; i < 2; i++) self->_eye[i].transform = CATransform3DIdentity;
		[CATransaction commit];
	}];
	for (int i = 0; i < 2; i++) _eye[i].transform = CATransform3DMakeScale(1, 0.08, 1);
	[CATransaction commit];
}

- (void)blink
{
	if (ReduceMotion() || _shown != Face::Awake) return;
	int gen = ++_blink;
	[CATransaction begin];
	[CATransaction setAnimationDuration:0.06];
	[CATransaction setCompletionBlock:^{
		if (gen != self->_blink) return;
		[CATransaction begin];
		[CATransaction setAnimationDuration:0.09];
		for (int i = 0; i < 2; i++) self->_eye[i].transform = CATransform3DIdentity;
		[CATransaction commit];
	}];
	for (int i = 0; i < 2; i++) _eye[i].transform = CATransform3DMakeScale(1, 0.08, 1);
	[CATransaction commit];
}

- (void)lookAt:(CGPoint)look duration:(double)seconds
{
	_look = look;
	[CATransaction begin];
	[CATransaction setAnimationDuration:ReduceMotion() ? 0 : seconds];
	[CATransaction setAnimationTimingFunction:[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut]];
	[self placeEyes];
	[CATransaction commit];
}

// ---- moods

- (void)after:(double)seconds do:(void (^)(KAGEMascotView* me))work
{
	__weak KAGEMascotView* weakSelf = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		if (KAGEMascotView* me = weakSelf) work(me);
	});
}

- (void)enterMood:(Mood)mood
{
	_mood = mood;
	[_lookTimer invalidate]; _lookTimer = nil;
	bool asleep = mood == Mood::Asleep;
	bool snoring = asleep && _available && !_paused;
	for (CATextLayer* z in _stillZs) z.hidden = !(snoring && ReduceMotion());
	for (CATextLayer* z in _sleepZs) { [z removeAnimationForKey:@"float"]; z.hidden = YES; }
	[self breathe:asleep];
	if (!self.window) return;
	if (snoring && !ReduceMotion() && _D > 0) [self startSnoring];
	if (mood == Mood::Working) [self scheduleLook];
}

// Asleep: slow deep breaths. Awake: a light bob, quicker when busy.
- (void)breathe:(bool)asleep
{
	[_face removeAnimationForKey:@"breath"];
	if (ReduceMotion()) return;
	CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
	if (asleep) { a.fromValue = @0.965; a.toValue = @1.03; a.duration = 1.7; }
	else { a.fromValue = @0.99; a.toValue = @1.015; a.duration = 1.15; }
	a.autoreverses = YES;
	a.repeatCount = HUGE_VALF;
	a.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
	[_face addAnimation:a forKey:@"breath"];
}

// Three Z's follow the same gentle arc, offset in time so the trail never jumps.
- (void)startSnoring
{
	CGPoint from = CGPointMake(_center.x + kZs[0].x * _D, _center.y + kZs[0].y * _D);
	CGPoint mid = CGPointMake(_center.x + kZs[1].x * _D, _center.y + kZs[1].y * _D);
	CGPoint to = CGPointMake(_center.x + kZs[2].x * _D, _center.y + (kZs[2].y + 0.12) * _D);
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathMoveToPoint(path, nullptr, from.x, from.y);
	CGPathAddQuadCurveToPoint(path, nullptr, mid.x + 0.08 * _D, mid.y, to.x, to.y);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < 3; i++) {
		CATextLayer* z = _sleepZs[i];
		[z removeAnimationForKey:@"float"];
		z.position = from;
		z.opacity = 0;
		z.hidden = NO;
		CAKeyframeAnimation* move = [CAKeyframeAnimation animationWithKeyPath:@"position"];
		move.path = path;
		move.calculationMode = kCAAnimationPaced;
		CABasicAnimation* grow = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
		grow.fromValue = @(kZs[0].size / kZs[2].size);
		grow.toValue = @1.05;
		CAKeyframeAnimation* tilt = [CAKeyframeAnimation animationWithKeyPath:@"transform.rotation.z"];
		tilt.values = @[@0.08, @-0.08, @0.04];
		CAKeyframeAnimation* fade = [CAKeyframeAnimation animationWithKeyPath:@"opacity"];
		fade.values = @[@0, @0.9, @0.9, @0];
		fade.keyTimes = @[@0, @0.2, @0.72, @1];
		CAAnimationGroup* g = [CAAnimationGroup animation];
		g.animations = @[move, grow, tilt, fade];
		g.duration = 2.7;
		g.repeatCount = HUGE_VALF;
		g.beginTime = CACurrentMediaTime() - i * 0.9;
		[z addAnimation:g forKey:@"float"];
	}
	[CATransaction commit];
	CGPathRelease(path);
}

// Working: the eyes dart somewhere new every so often, sooner when busy,
// with the odd blink.
- (void)scheduleLook
{
	[_lookTimer invalidate];
	double wait = Random(0.5, 1.5) * (1.0 - 0.6 * _busy);
	__weak KAGEMascotView* weakSelf = self;
	_lookTimer = [NSTimer scheduledTimerWithTimeInterval:wait repeats:NO block:^(NSTimer*) {
		KAGEMascotView* me = weakSelf;
		if (!me || me->_mood != Mood::Working) return;
		if (Random(0, 1) < 0.15) [me blink];
		else [me lookAt:CGPointMake(Random(-0.12, 0.12), Random(-0.07, 0.08)) duration:0.16];
		[me scheduleLook];
	}];
}

- (void)hop
{
	if (ReduceMotion()) return;
	CAKeyframeAnimation* a = [CAKeyframeAnimation animationWithKeyPath:@"position.y"];
	CGFloat y = _center.y;
	a.values = @[@(y), @(y + 0.14 * _D), @(y), @(y + 0.04 * _D), @(y)];
	a.keyTimes = @[@0, @0.35, @0.65, @0.82, @1];
	a.duration = 0.5;
	a.additive = NO;
	[_face addAnimation:a forKey:@"hop"];
}

- (void)wince
{
	[self showFace:Face::Ouch];
	CABasicAnimation* flash = [CABasicAnimation animationWithKeyPath:@"fillColor"];
	__block CGColorRef red = nullptr;
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{ red = ErrorColor().CGColor; }];
	flash.fromValue = (__bridge id) red;
	flash.toValue = (__bridge id) _body.fillColor;
	flash.duration = 0.8;
	[_body addAnimation:flash forKey:@"flash"];
	if (!ReduceMotion()) {
		CAKeyframeAnimation* shake = [CAKeyframeAnimation animationWithKeyPath:@"position.x"];
		CGFloat x = _center.x;
		shake.values = @[@(x), @(x - 5), @(x + 5), @(x - 3), @(x + 3), @(x)];
		shake.duration = 0.35;
		[_face addAnimation:shake forKey:@"shake"];
	}
	[self after:0.75 do:^(KAGEMascotView* me) {
		if (me->_mood == Mood::Working && me->_shown == Face::Ouch) [me showFace:Face::Awake];
	}];
}

- (void)restartSleepTimer
{
	[_sleepTimer invalidate];
	__weak KAGEMascotView* weakSelf = self;
	_sleepTimer = [NSTimer scheduledTimerWithTimeInterval:self.sleepAfter repeats:NO block:^(NSTimer*) { [weakSelf doze]; }];
}

// Quiet for a while: eyes back to center, a content ^ ^, then off to sleep.
- (void)doze
{
	if (_mood != Mood::Working) return;
	[self enterMood:Mood::Dozing];
	[self lookAt:CGPointZero duration:0.3];
	[self showFace:Face::Happy];
	[self after:0.9 do:^(KAGEMascotView* me) {
		if (me->_mood != Mood::Dozing) return;
		[me showFace:Face::Sleep];
		[me enterMood:Mood::Asleep];
	}];
}

// ---- public

- (void)setAvailable:(BOOL)available paused:(BOOL)paused
{
	if (available == _available && paused == _paused) return;
	_available = available;
	_paused = paused;
	self.alphaValue = available && !paused ? 1.0 : 0.55;
	if (!available || paused) {
		[_sleepTimer invalidate]; _sleepTimer = nil;
		[self lookAt:CGPointZero duration:0.3];
		[self showFace:Face::Sleep];
	}
	[self enterMood:(!available || paused) ? Mood::Asleep : _mood];
}

- (void)setBusy:(double)busy
{
	_busy = busy;
}

- (void)callArrived:(NSColor*)color ok:(BOOL)ok
{
	[self ripple:color];
	[self restartSleepTimer];
	if (_mood == Mood::Asleep || _mood == Mood::Dozing) {
		// Waking up: ^ ^ with a hop, then eyes open.
		[self enterMood:Mood::Waking];
		[self showFace:Face::Happy];
		[self hop];
		[self after:0.55 do:^(KAGEMascotView* me) {
			if (me->_mood != Mood::Waking) return;
			[me enterMood:Mood::Working];
			[me showFace:Face::Awake];
			if (!ok) [me wince];
		}];
		return;
	}
	if (_mood != Mood::Working) return;
	if (!ok) { [self wince]; return; }
	// Notices the call: a quick glance.
	[self lookAt:CGPointMake(Random(-0.12, 0.12), Random(-0.05, 0.08)) duration:0.1];
}

- (void)ripple:(NSColor*)color
{
	if (ReduceMotion() || _D < 1) return;
	CAShapeLayer* ring = [CAShapeLayer layer];
	ring.bounds = CGRectMake(0, 0, _D, _D);
	ring.position = _center;
	CGPathRef path = CGPathCreateWithEllipseInRect(CGRectInset(ring.bounds, 1, 1), nullptr);
	ring.path = path;
	CGPathRelease(path);
	ring.fillColor = nil;
	ring.lineWidth = 2;
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{ ring.strokeColor = color.CGColor; }];
	ring.opacity = 0;
	[self.layer insertSublayer:ring below:_face];

	[CATransaction begin];
	[CATransaction setCompletionBlock:^{ [ring removeFromSuperlayer]; }];
	CABasicAnimation* grow = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
	grow.fromValue = @1.0;
	grow.toValue = @1.8;
	CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
	fade.fromValue = @0.9;
	fade.toValue = @0.0;
	CAAnimationGroup* g = [CAAnimationGroup animation];
	g.animations = @[grow, fade];
	g.duration = 0.7;
	g.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
	[ring addAnimation:g forKey:@"ripple"];
	[CATransaction commit];
}
@end

// ------------------------------------------------------------------ bars
// One bar per command group. A call kicks its group's bar up (a spring, higher
// for slower calls); the bars sink back while nothing happens.

@interface KAGEBarsView : NSView
- (void)bump:(int)group color:(NSColor*)color strength:(double)strength;
@end

@implementation KAGEBarsView {
	NSMutableArray<CALayer*>* _bars;
	NSMutableArray<NSTextField*>* _labels;
}

- (instancetype)initWithFrame:(NSRect)frame
{
	if ((self = [super initWithFrame:frame])) {
		self.wantsLayer = YES;
		_bars = [NSMutableArray array];
		_labels = [NSMutableArray array];
		for (int i = 0; i < kGroupCount; i++) {
			CALayer* bar = [CALayer layer];
			bar.cornerRadius = 2;
			bar.anchorPoint = CGPointMake(0.5, 0);
			[self.layer addSublayer:bar];
			[_bars addObject:bar];
			NSTextField* l = Label(8, NSFontWeightMedium, NSColor.tertiaryLabelColor);
			l.alignment = NSTextAlignmentCenter;
			l.stringValue = [[NSString stringWithUTF8String:kGroups[i]] substringToIndex:3];
			[self addSubview:l];
			[_labels addObject:l];
		}
		[self tint];
	}
	return self;
}

- (void)restColor:(CALayer*)bar
{
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{
		bar.backgroundColor = [NSColor.tertiaryLabelColor colorWithAlphaComponent:0.35].CGColor;
	}];
}

- (void)tint { for (CALayer* bar in _bars) [self restColor:bar]; }

- (void)viewDidChangeEffectiveAppearance { [self tint]; }

- (CGFloat)slot { return self.bounds.size.width / kGroupCount; }
- (CGFloat)maxHeight { return MAX(4, self.bounds.size.height - 14); }
- (CGFloat)heightFor:(double)level { return 3 + (self.maxHeight - 3) * level; }

- (void)layout
{
	[super layout];
	CGFloat slot = self.slot, w = MAX(4, slot * 0.55);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < kGroupCount; i++) {
		CALayer* bar = _bars[i];
		bar.bounds = CGRectMake(0, 0, w, 3);
		bar.position = CGPointMake(slot * (i + 0.5), 12);
		_labels[i].frame = NSMakeRect(slot * i, 0, slot, 11);
	}
	[CATransaction commit];
}

- (void)bump:(int)group color:(NSColor*)color strength:(double)strength
{
	CALayer* bar = _bars[group];
	if (ReduceMotion()) return;
	CGFloat current = ((CALayer*)bar.presentationLayer ?: bar).bounds.size.height;
	CGFloat peak = std::min(self.maxHeight, current + (self.maxHeight - 3) * strength);
	CAKeyframeAnimation* height = [CAKeyframeAnimation animationWithKeyPath:@"bounds.size.height"];
	height.values = @[@(current), @(peak), @3];
	height.keyTimes = @[@0, @0.18, @1];
	height.timingFunctions = @[
		[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut],
		[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut]];
	height.duration = 1.65;
	[bar addAnimation:height forKey:@"bump"];
	__block CGColorRef active = nullptr;
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{ active = color.CGColor; }];
	CABasicAnimation* tint = [CABasicAnimation animationWithKeyPath:@"backgroundColor"];
	tint.fromValue = (__bridge id)active;
	tint.toValue = (__bridge id)bar.backgroundColor;
	tint.duration = 1.65;
	tint.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
	[bar addAnimation:tint forKey:@"tint"];
}
@end

// ------------------------------------------------------------------ panel

@interface KAGEFeedView : NSView   // top-down, newest row at y = 0
@end
@implementation KAGEFeedView
- (BOOL)isFlipped { return YES; }
@end

// One feed line: colored dot, what happened, time of day. Lays itself out
// from its own width, so a row made before the panel had a size still fits.
@interface KAGEFeedRow : NSView
- (instancetype)initWithLine:(NSString*)line tip:(NSString*)tip color:(NSColor*)color ok:(BOOL)ok;
@end

@implementation KAGEFeedRow {
	NSView* _dot;
	NSTextField* _text;
	NSTextField* _time;
}

- (instancetype)initWithLine:(NSString*)line tip:(NSString*)tip color:(NSColor*)color ok:(BOOL)ok
{
	if ((self = [super initWithFrame:NSMakeRect(0, 0, 200, 18)])) {
		_dot = [[NSView alloc] initWithFrame:NSMakeRect(0, 6, 6, 6)];
		_dot.wantsLayer = YES;
		_dot.layer.cornerRadius = 3;
		[self.effectiveAppearance performAsCurrentDrawingAppearance:^{ self->_dot.layer.backgroundColor = color.CGColor; }];
		_text = Label(11, NSFontWeightRegular, ok ? NSColor.labelColor : ErrorColor());
		_text.stringValue = line;
		_time = Label(10, NSFontWeightRegular, NSColor.tertiaryLabelColor);
		_time.font = [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
		_time.alignment = NSTextAlignmentRight;
		NSDateFormatter* f = [NSDateFormatter new];
		f.timeStyle = NSDateFormatterShortStyle;
		f.dateStyle = NSDateFormatterNoStyle;
		_time.stringValue = [f stringFromDate:NSDate.date];
		self.toolTip = [NSString stringWithFormat:@"%@\n%@", line, tip];
		for (NSView* v in @[_dot, _text, _time]) [self addSubview:v];
	}
	return self;
}

- (void)layout
{
	[super layout];
	CGFloat w = self.bounds.size.width, timeW = 52;
	_dot.frame = NSMakeRect(0, 6, 6, 6);
	_time.frame = NSMakeRect(MAX(0, w - timeW), 1, timeW, 16);
	_text.frame = NSMakeRect(12, 1, MAX(0, w - 12 - timeW - 4), 16);
}
@end

@implementation KAGEPanelView {
	KAGEMascotView* _kage;
	KAGEBarsView* _bars;
	NSTextField* _title;
	NSTextField* _status;
	NSTextField* _counts;
	NSButton* _pause;
	NSButton* _copy;
	NSView* _feed;
	NSMutableArray<NSView*>* _rows;
	NSTimer* _tick;
	std::deque<double> _recent;   // call times, for how busy Kage looks
	long _calls, _errors;
	BOOL _listening, _paused;
}

- (BOOL)isFlipped { return YES; }

- (instancetype)initWithFrame:(NSRect)frame
{
	if ((self = [super initWithFrame:frame])) {
		self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		_kage = [[KAGEMascotView alloc] initWithFrame:NSZeroRect];
		_bars = [[KAGEBarsView alloc] initWithFrame:NSZeroRect];
		_title = Label(15, NSFontWeightBold, NSColor.labelColor);
		_title.stringValue = @"KAGE";
		_status = Label(11, NSFontWeightRegular, NSColor.secondaryLabelColor);
		_counts = Label(10, NSFontWeightRegular, NSColor.tertiaryLabelColor);
		_counts.font = [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
		_pause = [NSButton checkboxWithTitle:@"Pause agents" target:self action:@selector(togglePause:)];
		_pause.controlSize = NSControlSizeSmall;
		_pause.font = [NSFont systemFontOfSize:11];
		_copy = [NSButton buttonWithTitle:@"Copy connection" target:self action:@selector(copyConnection:)];
		_copy.controlSize = NSControlSizeSmall;
		_copy.bezelStyle = NSBezelStyleRounded;
		_copy.font = [NSFont systemFontOfSize:11];
		_feed = [[KAGEFeedView alloc] initWithFrame:NSZeroRect];
		_feed.wantsLayer = YES;
		_feed.layer.masksToBounds = YES;
		_rows = [NSMutableArray array];
		for (NSView* v in @[_kage, _bars, _title, _status, _counts, _pause, _copy, _feed]) [self addSubview:v];
		[self setStatus:@"Starting…" listening:NO];
		[self updateCounts];
	}
	return self;
}

- (void)layout
{
	[super layout];
	CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
	CGFloat kage = 124, text = kPad + kage + 2;
	_kage.frame = NSMakeRect(kPad - 6, kPad - 6, kage, kage);
	_title.frame = NSMakeRect(text, kPad + 34, w - text - kPad, 20);
	_status.frame = NSMakeRect(text, kPad + 56, w - text - kPad, 16);
	_counts.frame = NSMakeRect(text, kPad + 74, w - text - kPad, 14);
	CGFloat y = kPad + kage;
	_bars.frame = NSMakeRect(kPad, y, w - 2 * kPad, 54);
	y += 62;
	[_pause sizeToFit];
	[_copy sizeToFit];
	_pause.frame = NSMakeRect(kPad, y + 2, _pause.frame.size.width, 18);
	_copy.frame = NSMakeRect(w - kPad - _copy.frame.size.width, y, _copy.frame.size.width, 22);
	y += 30;
	_feed.frame = NSMakeRect(kPad, y, w - 2 * kPad, MAX(0, h - y - kPad));
	for (NSView* row in _rows) row.frame = NSMakeRect(0, row.frame.origin.y, _feed.bounds.size.width, 18);   // rows lay out their own contents
}

- (void)setStatus:(NSString*)text listening:(BOOL)listening
{
	_status.stringValue = text;
	_listening = listening;
	[_kage setAvailable:_listening paused:_paused];
}

- (void)updateCounts
{
	_counts.stringValue = [NSString stringWithFormat:@"%ld call%@ · %ld error%@", _calls, _calls == 1 ? @"" : @"s", _errors, _errors == 1 ? @"" : @"s"];
}

// Busy: five or more calls in the last 3 s is flat out.
- (void)refreshBusy
{
	double now = CACurrentMediaTime();
	while (!_recent.empty() && now - _recent.front() > 3) _recent.pop_front();
	[_kage setBusy:std::min(1.0, _recent.size() / 5.0)];
}

- (void)startTicking
{
	if (_tick) return;
	__weak KAGEPanelView* weakSelf = self;
	_tick = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer* t) {
		KAGEPanelView* s = weakSelf;
		if (!s) { [t invalidate]; return; }
		[s refreshBusy];
		if (s->_recent.empty()) { [t invalidate]; s->_tick = nil; }
	}];
}

- (void)call:(NSString*)method line:(NSString*)line ok:(BOOL)ok edit:(BOOL)edit ms:(double)ms
{
	_calls++;
	if (!ok) _errors++;
	_recent.push_back(CACurrentMediaTime());
	[self updateCounts];
	NSColor* color = !ok ? ErrorColor() : edit ? EditColor() : ReadColor();
	[self refreshBusy];
	[_kage callArrived:color ok:ok];
	[_bars bump:GroupOf(method.UTF8String) color:color strength:0.35 + std::min(0.5, ms / 400.0)];
	// "shown line\nmore detail": the detail joins the tooltip.
	NSRange nl = [line rangeOfString:@"\n"];
	NSString* shown = nl.location == NSNotFound ? line : [line substringToIndex:nl.location];
	NSString* detail = nl.location == NSNotFound ? @"" : [[line substringFromIndex:nl.location + 1] stringByAppendingString:@"\n"];
	NSString* tip = [NSString stringWithFormat:@"%@%@ · %@", detail, method, ms < 1 ? @"under 1 ms" : [NSString stringWithFormat:@"%.0f ms", ms]];
	[self addRow:[[KAGEFeedRow alloc] initWithLine:shown tip:tip color:color ok:ok]];
	[self startTicking];
}

// The newest call slides in on top; the rest move down and the oldest fades.
- (void)addRow:(NSView*)row
{
	CGFloat width = _feed.bounds.size.width, rowH = 18;
	bool still = ReduceMotion();
	row.frame = NSMakeRect(still ? 0 : -24, 0, width, rowH);
	row.alphaValue = still ? 1 : 0;
	[_feed addSubview:row];
	[_rows insertObject:row atIndex:0];

	NSView* drop = _rows.count > kFeedRows ? _rows.lastObject : nil;
	if (drop) [_rows removeLastObject];
	[NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
		ctx.duration = still ? 0 : 0.28;
		ctx.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
		for (NSUInteger i = 0; i < self->_rows.count; i++) {
			NSView* r = self->_rows[i];
			NSRect f = NSMakeRect(0, i * (rowH + 4), width, rowH);
			(still ? r : r.animator).frame = f;
			(still ? r : r.animator).alphaValue = 1.0 - 0.08 * i;
		}
		if (drop) (still ? drop : drop.animator).alphaValue = 0;
	} completionHandler:^{ [drop removeFromSuperview]; }];
}

- (void)togglePause:(NSButton*)sender
{
	_paused = sender.state == NSControlStateValueOn;
	if (self.onPause) self.onPause(_paused);
	[_kage setAvailable:_listening paused:_paused];
}

- (void)copyConnection:(id)sender
{
	NSString* info = self.connectionInfo ? self.connectionInfo() : @"";
	[NSPasteboard.generalPasteboard clearContents];
	[NSPasteboard.generalPasteboard setString:info forType:NSPasteboardTypeString];
	_copy.title = @"Copied";
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (1.2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		self->_copy.title = @"Copy connection";
	});
}

- (void)dealloc { [_tick invalidate]; }
@end
