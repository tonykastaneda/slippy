#import "SlippyPanelView.h"
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
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
NSColor* SRGB(int rgb) { return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0 blue:(rgb & 0xFF) / 255.0 alpha:1]; }
NSColor* SlippyGreen() { return SRGB(0x00AB45); }   // Slippy's skin

// macOS's Reduce Motion, unless SLIPPY_FULL_MOTION=1 asks for the full bounce anyway.
bool ReduceMotion()
{
	static const bool full = getenv("SLIPPY_FULL_MOTION") && !strcmp(getenv("SLIPPY_FULL_MOTION"), "1");
	return !full && NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

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

// ------------------------------------------------------------------ Slippy
// Geometry is in units of the body's diameter D, measured off the mascot art;
// layer coordinates, so y grows upward.
//
// Motion is layered so nothing fights: _root hops, shakes and tilts (pivoting
// on the ground under Slippy), _squash squashes and stretches, _breath breathes
// on a loop nobody else touches. Every move starts from where Slippy is on
// screen right now, so a new move never pops.

namespace {

enum class Face { Sleep, Happy, Awake, Ouch };
enum class Mood { Asleep, Waking, Working, Dozing };

// Slippy is a frog's face: a green head with two eye bumps on top, and the
// eyes sitting in the bumps. No mouth, no body - the eyes do the talking.
// Units of D, from the box's bottom-left.
const CGFloat kEyeX = 0.22, kEyeY = 0.70;   // eye (bump) centers: 0.5 +/- kEyeX, kEyeY
const CGFloat kBumpR = 0.2;

// Where the eye pair sits for each face: sleepy eyes droop, happy ones lift.
CGFloat EyeLift(Face f) { return f == Face::Sleep ? -0.03 : f == Face::Happy ? 0.02 : 0.0; }

CGPathRef HeadPath(CGFloat D) { return CGPathCreateWithEllipseInRect(CGRectMake(0, 0.06 * D, D, 0.62 * D), nullptr); }

// The two eye bumps. They ride along with the eyes, so a look moves the whole eye.
CGPathRef BumpsPath(CGFloat D)
{
	CGMutablePathRef p = CGPathCreateMutable();
	for (CGFloat side : {-1.0, 1.0})
		CGPathAddEllipseInRect(p, nullptr, CGRectMake((0.5 + side * kEyeX - kBumpR) * D, (kEyeY - kBumpR) * D, 2 * kBumpR * D, 2 * kBumpR * D));
	return p;
}

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
const ZSpot kZs[] = {{-0.56, 0.3, 0.13}, {-0.64, 0.5, 0.16}, {-0.68, 0.72, 0.21}};   // up and away from the left eye

// How far Slippy moves: all the way, or - with Reduce Motion on - a gentle
// fraction. Slippy still breathes, blinks and eases; it just doesn't bounce.
CGFloat Motion() { return ReduceMotion() ? 0.3 : 1.0; }

CAMediaTimingFunction* Curve(float a, float b, float c, float d) { return [CAMediaTimingFunction functionWithControlPoints:a :b :c :d]; }
CAMediaTimingFunction* EaseOut() { return Curve(0.22, 1, 0.36, 1); }      // quick start, soft landing
CAMediaTimingFunction* EaseIn() { return Curve(0.55, 0, 1, 0.45); }       // gravity
CAMediaTimingFunction* EaseInOut() { return Curve(0.65, 0, 0.35, 1); }
CAMediaTimingFunction* Overshoot() { return Curve(0.34, 1.56, 0.64, 1); } // a little past, then back

// The value on screen right now, mid-animation included.
id Current(CALayer* layer, NSString* keyPath)
{
	CALayer* shown = layer.presentationLayer;
	return [(shown ?: layer) valueForKeyPath:keyPath];
}

// Keyframes from wherever the layer is now through 'values' (the last should
// be the model value). times has one more entry than values; curves one fewer.
CAKeyframeAnimation* Play(CALayer* layer, NSString* key, NSString* keyPath, NSArray* values, NSArray<NSNumber*>* times,
	NSArray<CAMediaTimingFunction*>* curves, double duration)
{
	CAKeyframeAnimation* a = [CAKeyframeAnimation animationWithKeyPath:keyPath];
	NSMutableArray* v = [NSMutableArray arrayWithObject:Current(layer, keyPath)];
	[v addObjectsFromArray:values];
	a.values = v;
	a.keyTimes = times;
	a.timingFunctions = curves;
	a.duration = duration;
	[layer addAnimation:a forKey:key];
	return a;
}

// A spring from where the layer is now to 'to', which becomes the model value.
void Spring(CALayer* layer, NSString* keyPath, id to, CGFloat stiffness, CGFloat damping)
{
	id from = Current(layer, keyPath);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	[layer setValue:to forKeyPath:keyPath];
	[CATransaction commit];
	CASpringAnimation* s = [CASpringAnimation animationWithKeyPath:keyPath];
	s.fromValue = from;
	s.toValue = to;
	s.mass = 1;
	s.stiffness = stiffness;
	s.damping = damping;
	s.duration = s.settlingDuration;
	[layer addAnimation:s forKey:keyPath];
}

// Timers keep running while Illustrator tracks the mouse (common modes).
NSTimer* After(double seconds, void (^block)(NSTimer*))
{
	NSTimer* t = [NSTimer timerWithTimeInterval:seconds repeats:NO block:block];
	[NSRunLoop.mainRunLoop addTimer:t forMode:NSRunLoopCommonModes];
	return t;
}

} // namespace

@implementation SlippyMascotView {
	CALayer* _root;          // hops, shakes, tilts; anchored on the ground under Slippy
	CALayer* _squash;        // squash & stretch
	CALayer* _breath;        // the breathing loop
	CAShapeLayer* _body;     // skin: the head
	CAShapeLayer* _bumps;    // skin: the eye bumps, inside _eyes so they move with the eyes
	CALayer* _eyes;          // the pair: looks around
	CALayer* _eye[2];        // one eye: white + mark; blinks squeeze the whole eye
	CAShapeLayer* _mark[2];  // the eye's stroke: o, —, ^, > <
	NSMutableArray<CATextLayer*>* _zs;
	BOOL _snoring;
	CGFloat _D;
	CGPoint _center;
	CGPoint _look;           // eye-pair offset, in D
	Face _shown;
	Mood _mood;
	int _blink;              // generation: a newer blink or face change cancels an older one's second half
	int _moodGen;            // generation: a mood change cancels scheduled steps of the old one
	double _busy;
	BOOL _available, _paused;
	NSTimer* _idleTimer;     // looks, blinks, tilts, snuffles
	NSTimer* _sleepTimer;
}

- (instancetype)initWithFrame:(NSRect)frame
{
	if ((self = [super initWithFrame:frame])) {
		self.wantsLayer = YES;
		self.sleepAfter = 6;
		_root = [CALayer layer];
		_squash = [CALayer layer];
		_breath = [CALayer layer];
		for (CALayer* l in @[_root, _squash, _breath]) l.anchorPoint = CGPointMake(0.5, 0);   // pivot on the ground
		_body = [CAShapeLayer layer];
		_bumps = [CAShapeLayer layer];
		_eyes = [CALayer layer];
		[_root addSublayer:_squash];
		[_squash addSublayer:_breath];
		[_breath addSublayer:_body];
		[_breath addSublayer:_eyes];
		[_eyes addSublayer:_bumps];
		for (int i = 0; i < 2; i++) {
			_eye[i] = [CALayer layer];
			_mark[i] = [CAShapeLayer layer];
			_mark[i].fillColor = nil;
			_mark[i].strokeColor = NSColor.blackColor.CGColor;
			_mark[i].lineCap = kCALineCapButt;
			_mark[i].lineJoin = kCALineJoinMiter;
			[_eye[i] addSublayer:_mark[i]];
			[_eyes addSublayer:_eye[i]];   // over the bumps
		}
		[self.layer addSublayer:_root];
		_zs = [NSMutableArray array];
		for (int i = 0; i < 3; i++) {
			CATextLayer* z = [self makeZ];
			z.opacity = 0;
			[self.layer addSublayer:z];
			[_zs addObject:z];
		}
		_shown = Face::Sleep;
		_mood = Mood::Asleep;
		_available = YES;
		[self paintBody:SlippyGreen()];
	}
	return self;
}

- (void)dealloc
{
	[_idleTimer invalidate];
	[_sleepTimer invalidate];
}

- (void)viewDidMoveToWindow
{
	[super viewDidMoveToWindow];
	if (!self.window) {   // off screen: no timers running for nobody
		[_idleTimer invalidate]; _idleTimer = nil;
		return;
	}
	for (CATextLayer* z in _zs) z.contentsScale = self.window.backingScaleFactor;
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
	CGRect box = CGRectMake(0, 0, _D, _D);
	_root.bounds = box;
	_root.position = CGPointMake(_center.x, _center.y - _D / 2);
	for (CALayer* l in @[_squash, _breath]) {
		l.bounds = box;
		l.position = CGPointMake(_D / 2, 0);
	}
	CGPathRef frog = HeadPath(_D);
	_body.path = frog;
	CGPathRelease(frog);
	_body.frame = box;
	CGPathRef bumps = BumpsPath(_D);
	_bumps.frame = box;
	_bumps.path = bumps;
	CGPathRelease(bumps);
	for (int i = 0; i < 2; i++) _eye[i].bounds = CGRectZero;
	_eyes.bounds = box;
	_eyes.position = [self eyeSpot];
	[self drawFace];
	for (CATextLayer* z in _zs) {
		CGFloat size = kZs[2].size * _D;
		z.fontSize = size * 1.25;
		z.bounds = CGRectMake(0, 0, size * 1.4, size * 1.6);
	}
	[CATransaction commit];
	// The Z's follow Slippy's size; and the first layout is when they can start.
	if (_D != oldD) { _snoring = NO; [self snore:[self wantsSnore]]; }
}

// The eye pair moves together when Slippy looks around.
- (CGPoint)eyeSpot { return CGPointMake(_D / 2 + _look.x * _D, _D / 2 + _look.y * _D); }

// Shape + spot of both eyes for _shown (no animation).
- (void)drawFace
{
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < 2; i++) {
		CGPathRef p = EyePath(_shown, i == 1, _D);
		_mark[i].path = p;
		CGPathRelease(p);
		_mark[i].lineWidth = EyeWidth(_shown, _D);
		_mark[i].bounds = CGRectZero;
		_eye[i].position = CGPointMake((0.5 + (i ? kEyeX : -kEyeX)) * _D, (kEyeY + EyeLift(_shown)) * _D);
	}
	[CATransaction commit];
}

- (void)paintBody:(NSColor*)color
{
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{
		self->_body.fillColor = color.CGColor;
		self->_bumps.fillColor = color.CGColor;
	}];
}

// ---- eyes

// Lids close fast, then (after 'change', if any) open with a little pop.
- (void)closeEyes:(double)close hold:(double)hold then:(void (^)(void))change open:(double)open
{
	int gen = ++_blink;
	[CATransaction begin];
	[CATransaction setCompletionBlock:^{
		if (gen != self->_blink) return;
		if (change) change();
		for (int i = 0; i < 2; i++) {
			CAKeyframeAnimation* a = Play(self->_eye[i], @"lid", @"transform.scale.y", @[@0.08, @1], @[@0, @(hold / (hold + open)), @1],
				@[EaseInOut(), Overshoot()], hold + open);
			a.removedOnCompletion = YES;
		}
	}];
	for (int i = 0; i < 2; i++) {
		CAKeyframeAnimation* a = Play(_eye[i], @"lid", @"transform.scale.y", @[@0.08], @[@0, @1], @[EaseIn()], close);
		a.fillMode = kCAFillModeForwards;   // stay shut until the open half takes over
		a.removedOnCompletion = NO;
	}
	[CATransaction commit];
}

// Every face change reads as alive: eyes squeeze shut, change, pop open.
- (void)showFace:(Face)face
{
	if (face == _shown) return;
	_shown = face;
	if (!self.window) { [self drawFace]; return; }
	[self closeEyes:0.07 hold:0.02 then:^{ [self drawFace]; } open:0.16];
}

- (void)blinkTwice:(BOOL)twice
{
	if (_shown != Face::Awake) return;
	[self closeEyes:0.06 hold:0.03 then:nil open:0.12];
	if (twice) {
		int mood = _moodGen;
		[self after:0.28 do:^(SlippyMascotView* me) { if (me->_moodGen == mood) [me closeEyes:0.06 hold:0.03 then:nil open:0.12]; }];
	}
}

- (void)lookAt:(CGPoint)look
{
	_look = look;
	Spring(_eyes, @"position", [NSValue valueWithPoint:[self eyeSpot]], 300, 20);   // darts, overshoots a hair, settles
}

// ---- body

// Hop: crouch, stretch on the way up, fall with gravity, squash on landing, jiggle.
- (void)hop:(CGFloat)height squash:(CGFloat)amount duration:(double)duration
{
	CGFloat m = Motion(), y = _root.position.y, top = y + height * _D * m, k = amount * m;
	Play(_root, @"hop", @"position.y", @[@(y), @(top), @(y), @(y)], @[@0, @0.2, @0.5, @0.76, @1],
		@[EaseInOut(), EaseOut(), EaseIn(), EaseOut()], duration);
	NSArray* times = @[@0, @0.2, @0.32, @0.5, @0.8, @1];
	NSArray* curves = @[EaseOut(), EaseOut(), EaseInOut(), EaseIn(), Overshoot()];
	Play(_squash, @"squash.x", @"transform.scale.x", @[@(1 + k), @(1 - 0.6 * k), @1, @(1 + 1.1 * k), @1], times, curves, duration);
	Play(_squash, @"squash.y", @"transform.scale.y", @[@(1 - k), @(1 + 0.8 * k), @1, @(1 - 1.1 * k), @1], times, curves, duration);
}

// A slow stretch, like a yawn, before waking up properly.
- (void)stretch
{
	CGFloat k = 0.1 * Motion();
	NSArray* times = @[@0, @0.25, @0.75, @1];
	NSArray* curves = @[EaseInOut(), EaseInOut(), Overshoot()];
	Play(_squash, @"squash.x", @"transform.scale.x", @[@(1 + 0.5 * k), @(1 - 0.5 * k), @1], times, curves, 0.55);
	Play(_squash, @"squash.y", @"transform.scale.y", @[@(1 - 0.5 * k), @(1 + k), @1], times, curves, 0.55);
}

// Asleep, now and then: a small wriggle, getting comfy.
- (void)snuffle
{
	CGFloat k = 0.035 * Motion() + 0.01;
	NSArray* times = @[@0, @0.3, @0.6, @1];
	NSArray* curves = @[EaseInOut(), EaseInOut(), EaseOut()];
	Play(_squash, @"squash.x", @"transform.scale.x", @[@(1 + k), @(1 - 0.4 * k), @1], times, curves, 0.7);
	Play(_squash, @"squash.y", @"transform.scale.y", @[@(1 - k), @(1 + 0.4 * k), @1], times, curves, 0.7);
}

// Working, now and then: head on one side, curious, then back.
- (void)tilt
{
	CGFloat angle = (Random(0, 1) < 0.5 ? -1 : 1) * 0.08 * Motion();
	Spring(_root, @"transform.rotation.z", @(angle), 140, 11);
	int mood = _moodGen;
	[self after:Random(0.7, 1.2) do:^(SlippyMascotView* me) {
		if (me->_moodGen == mood) Spring(me->_root, @"transform.rotation.z", @0, 140, 11);
	}];
}

// Breathing: deep and slow asleep (a little slumped), light awake. Settles
// from the current breath into the new rhythm instead of jumping.
- (void)breathe
{
	bool asleep = _mood == Mood::Asleep || _mood == Mood::Dozing;
	double inhale = asleep ? 1.5 : 0.55, exhale = asleep ? 2.0 : 0.75, settle = 0.45;
	struct { NSString* axis; CGFloat base, depth; } axes[] = {
		{@"x", asleep ? 1.01 : 1.0, asleep ? 0.03 : 0.01},
		{@"y", asleep ? 0.975 : 1.0, asleep ? 0.045 : 0.018},
	};
	for (auto& ax : axes) {
		NSString* keyPath = [@"transform.scale." stringByAppendingString:ax.axis];
		CABasicAnimation* into = [CABasicAnimation animationWithKeyPath:keyPath];
		into.fromValue = Current(_breath, keyPath);
		into.toValue = @(ax.base);
		into.duration = settle;
		into.timingFunction = EaseInOut();
		CAKeyframeAnimation* loop = [CAKeyframeAnimation animationWithKeyPath:keyPath];
		loop.values = @[@(ax.base), @(ax.base + ax.depth), @(ax.base)];
		loop.keyTimes = @[@0, @(inhale / (inhale + exhale)), @1];
		loop.timingFunctions = @[EaseInOut(), EaseInOut()];
		loop.duration = inhale + exhale;
		loop.beginTime = settle;
		loop.repeatCount = HUGE_VALF;
		CAAnimationGroup* g = [CAAnimationGroup animation];
		g.animations = @[into, loop];
		g.duration = HUGE_VAL;
		[_breath addAnimation:g forKey:[@"breath." stringByAppendingString:ax.axis]];
	}
}

// ---- Z's

- (BOOL)wantsSnore { return _mood == Mood::Asleep && _available && !_paused && self.window && _D >= 1; }

- (void)snore:(BOOL)on
{
	// Already snoring and still animating: nothing to do. (AppKit drops a layer's
	// animations when Illustrator docks or re-shows the panel - then restart.)
	if (on == _snoring && (!on || [_zs[0] animationForKey:@"float"])) return;
	_snoring = on;
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (CATextLayer* z in _zs) {
		// Freeze each Z where it is, then fade it out (or restart the drift).
		CATextLayer* shown = z.presentationLayer;
		if (shown) { z.position = shown.position; z.transform = shown.transform; }
		CGFloat opacity = shown ? shown.opacity : z.opacity;
		[z removeAnimationForKey:@"float"];
		z.opacity = 0;
		if (!on && opacity > 0) {
			CABasicAnimation* out = [CABasicAnimation animationWithKeyPath:@"opacity"];
			out.fromValue = @(opacity);
			out.toValue = @0;
			out.duration = 0.35;
			out.timingFunction = EaseOut();
			[z addAnimation:out forKey:@"out"];
		}
	}
	[CATransaction commit];
	if (!on) return;
	if (ReduceMotion()) [self snoreInPlace];
	else [self snoreDrifting];
}

// Each Z rises from beside Slippy along a gentle arc, growing, swaying and
// fading; three of them, a third of a cycle apart, so the trail never jumps.
- (void)snoreDrifting
{
	CGPoint from = CGPointMake(_center.x + kZs[0].x * _D, _center.y + kZs[0].y * _D);
	CGPoint mid = CGPointMake(_center.x + kZs[1].x * _D, _center.y + kZs[1].y * _D);
	CGPoint to = CGPointMake(_center.x + kZs[2].x * _D, _center.y + (kZs[2].y + 0.14) * _D);
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathMoveToPoint(path, nullptr, from.x, from.y);
	CGPathAddQuadCurveToPoint(path, nullptr, mid.x + 0.1 * _D, mid.y, to.x, to.y);
	double cycle = 3.0;
	for (int i = 0; i < 3; i++) {
		CATextLayer* z = _zs[i];
		CAKeyframeAnimation* move = [CAKeyframeAnimation animationWithKeyPath:@"position"];
		move.path = path;
		move.calculationMode = kCAAnimationPaced;
		move.timingFunction = Curve(0.3, 0.1, 0.6, 1);   // lifts off slowly, drifts away
		CABasicAnimation* grow = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
		grow.fromValue = @(kZs[0].size / kZs[2].size);
		grow.toValue = @1.08;
		grow.timingFunction = EaseOut();
		CAKeyframeAnimation* sway = [CAKeyframeAnimation animationWithKeyPath:@"transform.rotation.z"];
		sway.values = @[@0.12, @-0.1, @0.08, @-0.04];
		sway.timingFunctions = @[EaseInOut(), EaseInOut(), EaseInOut()];
		CAKeyframeAnimation* fade = [CAKeyframeAnimation animationWithKeyPath:@"opacity"];
		fade.values = @[@0, @0.9, @0.85, @0];
		fade.keyTimes = @[@0, @0.18, @0.7, @1];
		fade.timingFunctions = @[EaseOut(), EaseInOut(), EaseIn()];
		CAAnimationGroup* g = [CAAnimationGroup animation];
		g.animations = @[move, grow, sway, fade];
		g.duration = cycle;
		g.repeatCount = HUGE_VALF;
		g.beginTime = CACurrentMediaTime() - i * cycle / 3;
		[z addAnimation:g forKey:@"float"];
	}
	CGPathRelease(path);
}

// Reduce Motion: the art's three Z's stay put and light up one after
// another, then fade together - still asleep, still alive, no travel.
- (void)snoreInPlace
{
	double cycle = 3.2;
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < 3; i++) {
		CATextLayer* z = _zs[i];
		z.position = CGPointMake(_center.x + kZs[i].x * _D, _center.y + kZs[i].y * _D);
		z.transform = CATransform3DMakeScale(kZs[i].size / kZs[2].size, kZs[i].size / kZs[2].size, 1);
		double on = 0.1 + i * 0.18;
		CAKeyframeAnimation* fade = [CAKeyframeAnimation animationWithKeyPath:@"opacity"];
		fade.values = @[@0, @0, @0.85, @0.85, @0];
		fade.keyTimes = @[@0, @(on), @(on + 0.15), @0.78, @0.95];
		fade.timingFunctions = @[EaseInOut(), EaseOut(), EaseInOut(), EaseIn()];
		fade.duration = cycle;
		fade.repeatCount = HUGE_VALF;
		[z addAnimation:fade forKey:@"float"];
	}
	[CATransaction commit];
}

// ---- moods

- (void)after:(double)seconds do:(void (^)(SlippyMascotView* me))work
{
	__weak SlippyMascotView* weakSelf = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		if (SlippyMascotView* me = weakSelf) work(me);
	});
}

- (void)enterMood:(Mood)mood
{
	bool changed = mood != _mood;
	_mood = mood;
	_moodGen++;
	if (changed || ![_breath animationForKey:@"breath.y"]) [self breathe];
	[self snore:[self wantsSnore]];
	if (mood != Mood::Working && [_root animationForKey:@"transform.rotation.z"]) Spring(_root, @"transform.rotation.z", @0, 140, 14);
	[self scheduleIdle];
}

// Little things Slippy does on its own: working - looks around (sooner when
// busy), blinks, tilts its head; asleep - the odd snuffle.
- (void)scheduleIdle
{
	[_idleTimer invalidate];
	_idleTimer = nil;
	if (!self.window) return;
	double wait;
	if (_mood == Mood::Working) wait = Random(0.6, 1.8) * (1.0 - 0.6 * _busy);
	else if (_mood == Mood::Asleep && _available && !_paused) wait = Random(7, 16);
	else return;
	__weak SlippyMascotView* weakSelf = self;
	_idleTimer = After(wait, ^(NSTimer*) { [weakSelf idle]; });
}

- (void)idle
{
	if (_mood == Mood::Working) {
		double r = Random(0, 1);
		if (r < 0.2) [self blinkTwice:Random(0, 1) < 0.25];
		else if (r < 0.28 && _busy < 0.5) [self tilt];
		else [self lookAt:CGPointMake(Random(-0.12, 0.12), Random(-0.07, 0.08))];
	}
	else if (_mood == Mood::Asleep) [self snuffle];
	[self scheduleIdle];
}

- (void)wince
{
	[self showFace:Face::Ouch];
	CABasicAnimation* flash = [CABasicAnimation animationWithKeyPath:@"fillColor"];
	__block CGColorRef red = nullptr;
	[self.effectiveAppearance performAsCurrentDrawingAppearance:^{ red = ErrorColor().CGColor; }];
	flash.fromValue = (__bridge id) red;
	flash.toValue = (__bridge id) _body.fillColor;
	flash.duration = 0.9;
	flash.timingFunction = EaseIn();
	[_body addAnimation:flash forKey:@"flash"];
	[_bumps addAnimation:flash forKey:@"flash"];
	// A shake that dies away, and a flinch.
	CGFloat x = _root.position.x, s = 0.05 * _D * Motion();
	Play(_root, @"shake", @"position.x", @[@(x - s), @(x + 0.8 * s), @(x - 0.5 * s), @(x + 0.25 * s), @(x)],
		@[@0, @0.12, @0.32, @0.52, @0.74, @1], @[EaseOut(), EaseInOut(), EaseInOut(), EaseInOut(), EaseOut()], 0.5);
	CGFloat k = 0.1 * Motion();
	NSArray* times = @[@0, @0.2, @0.55, @1];
	NSArray* curves = @[EaseOut(), EaseInOut(), Overshoot()];
	Play(_squash, @"squash.x", @"transform.scale.x", @[@(1 + k), @(1 - 0.3 * k), @1], times, curves, 0.5);
	Play(_squash, @"squash.y", @"transform.scale.y", @[@(1 - k), @(1 + 0.3 * k), @1], times, curves, 0.5);
	int mood = _moodGen;
	[self after:0.8 do:^(SlippyMascotView* me) {
		if (me->_moodGen == mood && me->_shown == Face::Ouch) [me showFace:Face::Awake];
	}];
}

- (void)restartSleepTimer
{
	[_sleepTimer invalidate];
	__weak SlippyMascotView* weakSelf = self;
	_sleepTimer = After(self.sleepAfter, ^(NSTimer*) { [weakSelf doze]; });
}

// Quiet for a while: eyes back to center, a content ^ ^, nods off - catches
// itself - and drifts to sleep.
- (void)doze
{
	if (_mood != Mood::Working) return;
	[self enterMood:Mood::Dozing];
	int mood = _moodGen;
	[self lookAt:CGPointZero];
	[self showFace:Face::Happy];
	CGFloat y = _root.position.y, dip = 0.035 * _D * Motion();
	[self after:0.6 do:^(SlippyMascotView* me) {
		if (me->_moodGen != mood) return;
		Play(me->_root, @"nod", @"position.y", @[@(y - dip), @(y - dip), @(y)], @[@0, @0.6, @0.7, @1],
			@[EaseInOut(), EaseInOut(), Overshoot()], 1.1);
		Play(me->_root, @"nodTilt", @"transform.rotation.z", @[@(-0.05 * Motion()), @(-0.05 * Motion()), @0], @[@0, @0.6, @0.7, @1],
			@[EaseInOut(), EaseInOut(), Overshoot()], 1.1);
	}];
	[self after:1.9 do:^(SlippyMascotView* me) {
		if (me->_moodGen != mood) return;
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
	[NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
		ctx.duration = 0.35;
		self.animator.alphaValue = available && !paused ? 1.0 : 0.55;
	}];
	if (!available || paused) {
		[_sleepTimer invalidate]; _sleepTimer = nil;
		[self lookAt:CGPointZero];
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
		// Waking up: a stretch with a content ^ ^, a hop, then eyes open.
		[self enterMood:Mood::Waking];
		int mood = _moodGen;
		[self showFace:Face::Happy];
		[self stretch];
		[self after:0.45 do:^(SlippyMascotView* me) {
			if (me->_moodGen == mood) [me hop:0.16 squash:0.12 duration:0.62];
		}];
		[self after:1.0 do:^(SlippyMascotView* me) {
			if (me->_moodGen != mood) return;
			[me enterMood:Mood::Working];
			[me showFace:Face::Awake];
			if (!ok) [me wince];
		}];
		return;
	}
	if (_mood != Mood::Working) return;
	if (!ok) { [self wince]; return; }
	// Notices the call: a glance, and a little bounce when it changed something.
	[self lookAt:CGPointMake(Random(-0.12, 0.12), Random(-0.05, 0.08))];
	if ([color isEqual:EditColor()] && ![_root animationForKey:@"hop"]) [self hop:0.05 squash:0.06 duration:0.4];
	[self scheduleIdle];   // it just looked; the next idle glance can wait
}

- (void)ripple:(NSColor*)color
{
	if (_D < 1) return;
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
	[self.layer insertSublayer:ring below:_root];

	[CATransaction begin];
	[CATransaction setCompletionBlock:^{ [ring removeFromSuperlayer]; }];
	CABasicAnimation* grow = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
	grow.fromValue = @1.0;
	grow.toValue = @(1 + 0.8 * Motion());
	CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
	fade.fromValue = @0.9;
	fade.toValue = @0.0;
	CAAnimationGroup* g = [CAAnimationGroup animation];
	g.animations = @[grow, fade];
	g.duration = 0.75;
	g.timingFunction = EaseOut();
	[ring addAnimation:g forKey:@"ripple"];
	[CATransaction commit];
}
@end

// ------------------------------------------------------------------ bars
// One bar per command group. A call kicks its group's bar up (a spring, higher
// for slower calls); the bars sink back while nothing happens.

@interface SlippyBarsView : NSView
- (void)bump:(int)group color:(NSColor*)color strength:(double)strength;
@end

@implementation SlippyBarsView {
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
	CALayer* bar = _bars[group];   // grows in place, so it stays on with Reduce Motion
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

@interface SlippyFeedView : NSView   // top-down, newest row at y = 0
@end
@implementation SlippyFeedView
- (BOOL)isFlipped { return YES; }
@end

// One feed line: colored dot, what happened, time of day. Lays itself out
// from its own width, so a row made before the panel had a size still fits.
@interface SlippyFeedRow : NSView
- (instancetype)initWithLine:(NSString*)line tip:(NSString*)tip color:(NSColor*)color ok:(BOOL)ok;
@end

@implementation SlippyFeedRow {
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

@implementation SlippyPanelView {
	SlippyMascotView* _slippy;
	SlippyBarsView* _bars;
	NSTextField* _title;
	NSTextField* _status;
	NSTextField* _counts;
	NSButton* _pause;
	NSButton* _copy;
	NSView* _feed;
	NSMutableArray<NSView*>* _rows;
	NSTimer* _tick;
	std::deque<double> _recent;   // call times, for how busy Slippy looks
	long _calls, _errors;
	BOOL _listening, _paused;
}

- (BOOL)isFlipped { return YES; }

- (instancetype)initWithFrame:(NSRect)frame
{
	if ((self = [super initWithFrame:frame])) {
		self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		_slippy = [[SlippyMascotView alloc] initWithFrame:NSZeroRect];
		_bars = [[SlippyBarsView alloc] initWithFrame:NSZeroRect];
		_title = Label(15, NSFontWeightBold, NSColor.labelColor);
		_title.stringValue = @"Slippy";
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
		_feed = [[SlippyFeedView alloc] initWithFrame:NSZeroRect];
		_feed.wantsLayer = YES;
		_feed.layer.masksToBounds = YES;
		_rows = [NSMutableArray array];
		for (NSView* v in @[_slippy, _bars, _title, _status, _counts, _pause, _copy, _feed]) [self addSubview:v];
		[self setStatus:@"Starting…" listening:NO];
		[self updateCounts];
	}
	return self;
}

- (void)layout
{
	[super layout];
	CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
	CGFloat slippy = 124, text = kPad + slippy + 2;
	_slippy.frame = NSMakeRect(kPad - 6, kPad - 6, slippy, slippy);
	_title.frame = NSMakeRect(text, kPad + 34, w - text - kPad, 20);
	_status.frame = NSMakeRect(text, kPad + 56, w - text - kPad, 16);
	_counts.frame = NSMakeRect(text, kPad + 74, w - text - kPad, 14);
	CGFloat y = kPad + slippy;
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
	[_slippy setAvailable:_listening paused:_paused];
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
	[_slippy setBusy:std::min(1.0, _recent.size() / 5.0)];
}

- (void)startTicking
{
	if (_tick) return;
	__weak SlippyPanelView* weakSelf = self;
	_tick = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer* t) {
		SlippyPanelView* s = weakSelf;
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
	[_slippy callArrived:color ok:ok];
	[_bars bump:GroupOf(method.UTF8String) color:color strength:0.35 + std::min(0.5, ms / 400.0)];
	// "shown line\nmore detail": the detail joins the tooltip.
	NSRange nl = [line rangeOfString:@"\n"];
	NSString* shown = nl.location == NSNotFound ? line : [line substringToIndex:nl.location];
	NSString* detail = nl.location == NSNotFound ? @"" : [[line substringFromIndex:nl.location + 1] stringByAppendingString:@"\n"];
	NSString* tip = [NSString stringWithFormat:@"%@%@ · %@", detail, method, ms < 1 ? @"under 1 ms" : [NSString stringWithFormat:@"%.0f ms", ms]];
	[self addRow:[[SlippyFeedRow alloc] initWithLine:shown tip:tip color:color ok:ok]];
	[self startTicking];
}

// The newest call slides in on top; the rest move down and the oldest fades.
- (void)addRow:(NSView*)row
{
	CGFloat width = _feed.bounds.size.width, rowH = 18;
	bool still = ReduceMotion();
	row.frame = NSMakeRect(still ? 0 : -24, 0, width, rowH);   // Reduce Motion: fades in, no slide
	row.alphaValue = 0;
	[_feed addSubview:row];
	[_rows insertObject:row atIndex:0];

	NSView* drop = _rows.count > kFeedRows ? _rows.lastObject : nil;
	if (drop) [_rows removeLastObject];
	[NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
		ctx.duration = 0.3;
		ctx.timingFunction = [CAMediaTimingFunction functionWithControlPoints:0.22 :1 :0.36 :1];
		for (NSUInteger i = 0; i < self->_rows.count; i++) {
			NSView* r = self->_rows[i];
			NSRect f = NSMakeRect(0, i * (rowH + 4), width, rowH);
			if (still) r.frame = f;
			else r.animator.frame = f;
			r.animator.alphaValue = 1.0 - 0.08 * i;
		}
		if (drop) drop.animator.alphaValue = 0;
	} completionHandler:^{ [drop removeFromSuperview]; }];
}

- (void)togglePause:(NSButton*)sender
{
	_paused = sender.state == NSControlStateValueOn;
	if (self.onPause) self.onPause(_paused);
	[_slippy setAvailable:_listening paused:_paused];
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
