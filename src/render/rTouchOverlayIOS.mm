/*
 * rTouchOverlayIOS.mm — iOS corner-button overlay for game actions.
 *
 * Two transparent buttons sit in the top-left and top-right corners.
 * They are active in all three touch/steering modes.
 * Touches OUTSIDE the buttons are forwarded to SDL normally.
 */

#import <UIKit/UIKit.h>
#import <CoreMotion/CoreMotion.h>
#include "SDL3/SDL.h"
#include "rTouchOverlayIOS.h"

// ============================================================
// C++ bridge (implemented in uInput.cpp / ePlayer.cpp / eCamera.cpp)
// ============================================================
extern "C" {
    int  su_GetEnableTouch();        // returns su_enableTouch
    void su_SetEnableTouch(int m);   // sets su_enableTouch
    // Camera actions — implemented in eCamera.cpp
    void aa_SetGyroCameraInput(float yaw, float pitch); // rad/s look rates
    void aa_SetGlanceForward(bool active);              // hold = look forward
    void aa_SwitchCameraView(void);                     // switch camera mode
    void aa_SetCameraFrozen(bool frozen);               // freeze camera direction
    // Trigger a named global action via the game's binding system
    void aa_TriggerAction(const char* actionName);
    // Trigger a named player action on player 0 (CHAT, SWITCH_VIEW, etc.)
    void aa_TriggerPlayerAction(const char* actionName);
    // Returns true if chat is available (networked game, not standalone)
    bool aa_IsChatAvailable(void);
}

// We access ePlayer via a thin wrapper to avoid pulling in all C++ headers.
extern "C" const char* aa_getInstantChatString(int playerIndex, int chatIndex);
extern "C" int aa_getNumViewports();
extern "C" int aa_getViewportInfo(int vpIdx, float* outLeft, float* outBottom,
                                  float* outWidth, float* outHeight, int* outRotDeg);

// ============================================================
// Constants
// ============================================================
static const CGFloat kButtonW      = 64.0f;
static const CGFloat kButtonH      = 44.0f;
static const CGFloat kSubButtonH   = 28.0f;  // gyro / cam buttons
static const CGFloat kButtonMargin = 4.0f;
static const NSTimeInterval kLongPressThreshold = 0.45;
static const NSTimeInterval kDoubleTapWindow    = 0.30;

static const CGFloat kDropItemH  = 48.0f;
static const CGFloat kDropItemW  = 200.0f;
static const CGFloat kDropCornerR = 12.0f;

// Console insets exposed to rConsoleGraph.cpp
extern float sr_consoleInsetLeft;
extern float sr_consoleInsetRight;

// ============================================================
// SDL event injection helpers
// ============================================================
static void injectKey(SDL_Scancode sc) {
    SDL_Event down, up;
    SDL_zero(down);
    down.type         = SDL_EVENT_KEY_DOWN;
    down.key.scancode = sc;
    down.key.down     = true;
    SDL_PushEvent(&down);

    SDL_zero(up);
    up.type         = SDL_EVENT_KEY_UP;
    up.key.scancode = sc;
    up.key.down     = false;
    SDL_PushEvent(&up);
}

// ============================================================
// CoreMotion gyro state
// ============================================================
static CMMotionManager *s_motionMgr          = nil;
static double           s_gyroBaseX           = NAN;
static double           s_gyroBaseY           = NAN;
// rad/s per normalized gravity unit (gravity is -1..1 on each axis)
static const float      kGyroYawSensitivity   = 3.0f;
static const float      kGyroPitchSensitivity = 1.5f;

static void startGyro(void) {
    if (!s_motionMgr) s_motionMgr = [[CMMotionManager alloc] init];
    if (!s_motionMgr.deviceMotionAvailable) return;

    s_motionMgr.deviceMotionUpdateInterval = 1.0 / 60.0;
    s_gyroBaseX = NAN;
    s_gyroBaseY = NAN;

    [s_motionMgr startDeviceMotionUpdatesToQueue:[NSOperationQueue mainQueue]
                                    withHandler:^(CMDeviceMotion *motion, NSError *error) {
        if (!motion) return;
        if (isnan(s_gyroBaseX)) {
            s_gyroBaseX = motion.gravity.x;
            s_gyroBaseY = motion.gravity.y;
            return;
        }
        // gravity.y: tilt top-right → positive → look right → negative yaw (look-left convention)
        // gravity.x: tilt top-away  → positive → look up    → positive pitch
        float yaw   =  (float)((motion.gravity.y - s_gyroBaseY) * kGyroYawSensitivity);
        float pitch = -(float)((motion.gravity.x - s_gyroBaseX) * kGyroPitchSensitivity);
        aa_SetGyroCameraInput(yaw, pitch);
    }];
}

static void stopGyro(void) {
    if (s_motionMgr) [s_motionMgr stopDeviceMotionUpdates];
    aa_SetGyroCameraInput(0.0f, 0.0f);
}

// ============================================================
// Gyro toggle button — tap to enable/disable gyro camera look
// ============================================================
@interface AAGyroButton : UIView {
    BOOL _enabled;
}
@end

@implementation AAGyroButton

- (instancetype)init {
    self = [super initWithFrame:CGRectZero];
    if (self) {
        _enabled = NO;
        self.backgroundColor      = [UIColor clearColor];
        self.userInteractionEnabled = YES;
    }
    return self;
}

- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGRect inset = CGRectInset(rect, 2, 2);

    UIColor *bg = _enabled
        ? [UIColor colorWithRed:0.0f green:0.6f blue:0.2f alpha:0.35f]
        : [UIColor colorWithWhite:0.0f alpha:0.18f];
    [bg setFill];
    UIBezierPath *pill = [UIBezierPath bezierPathWithRoundedRect:inset cornerRadius:8.0f];
    CGContextAddPath(ctx, pill.CGPath);
    CGContextFillPath(ctx);

    [[UIColor colorWithWhite:1.0f alpha:0.12f] setStroke];
    pill.lineWidth = 0.5f;
    [pill stroke];

    UIColor *color = _enabled
        ? [UIColor colorWithRed:0.4f green:1.0f blue:0.4f alpha:0.90f]
        : [UIColor colorWithWhite:0.90f alpha:0.75f];

    NSString *label = @"L2";
    NSDictionary *attrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:11.0f weight:UIFontWeightSemibold],
        NSForegroundColorAttributeName: color,
    };
    CGSize sz = [label sizeWithAttributes:attrs];
    [label drawAtPoint:CGPointMake((rect.size.width - sz.width) / 2,
                                   (rect.size.height - sz.height) / 2)
        withAttributes:attrs];
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:0.70f];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:1.0f];
    _enabled = !_enabled;
    [self setNeedsDisplay];
    if (_enabled) startGyro(); else stopGyro();
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:1.0f];
}

@end

// ============================================================
// Freeze-camera toggle button (below right)
// ============================================================
@interface AAFreezeButton : UIView {
    BOOL _frozen;
}
@end

@implementation AAFreezeButton

- (instancetype)init {
    self = [super initWithFrame:CGRectZero];
    if (self) {
        self.backgroundColor      = [UIColor clearColor];
        self.userInteractionEnabled = YES;
    }
    return self;
}

- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    [[UIColor colorWithWhite:0.0f alpha:0.18f] setFill];
    UIBezierPath *pill = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(rect, 2, 2)
                                                    cornerRadius:8.0f];
    CGContextAddPath(ctx, pill.CGPath);
    CGContextFillPath(ctx);

    [[UIColor colorWithWhite:1.0f alpha:0.12f] setStroke];
    pill.lineWidth = 0.5f;
    [pill stroke];

    NSString *label = @"R2";
    NSDictionary *attrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:11.0f weight:UIFontWeightSemibold],
        NSForegroundColorAttributeName: [UIColor colorWithWhite:0.90f alpha:0.75f],
    };
    CGSize sz = [label sizeWithAttributes:attrs];
    [label drawAtPoint:CGPointMake((rect.size.width - sz.width) / 2,
                                   (rect.size.height - sz.height) / 2)
        withAttributes:attrs];
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:0.70f];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:1.0f];
    aa_TriggerPlayerAction("SWITCH_VIEW");
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:1.0f];
}

- (void)resetState {
    // Nothing to reset — stateless tap button
}

@end

// ============================================================
// Dropdown overlay — shown during a hold gesture
// ============================================================
@interface AADropdownView : UIView

@property (nonatomic, strong) NSArray<NSString*> *items;
@property (nonatomic, assign) NSInteger highlightedIndex;   // -1 = none
@property (nonatomic, assign) BOOL anchorLeft; // YES → anchored to left button

// Lay out items starting from anchorPt (top of first item in window coords).
- (void)layoutAtAnchor:(CGPoint)anchorPt inView:(UIView*)parent;

// Returns index of item at point (window coords), or -1 if outside.
- (NSInteger)indexForWindowPoint:(CGPoint)pt;

@end

@implementation AADropdownView

- (instancetype)initWithItems:(NSArray<NSString*>*)items {
    CGFloat h = items.count * kDropItemH + 16.0f;
    self = [super initWithFrame:CGRectMake(0, 0, kDropItemW, h)];
    if (self) {
        _items            = items;
        _highlightedIndex = -1;
        self.layer.cornerRadius = kDropCornerR;
        self.layer.masksToBounds = YES;
        self.backgroundColor = [UIColor colorWithWhite:0.08f alpha:0.90f];
        self.layer.borderColor = [UIColor colorWithWhite:1.0f alpha:0.15f].CGColor;
        self.layer.borderWidth = 0.5f;
    }
    return self;
}

- (void)layoutAtAnchor:(CGPoint)anchorPt inView:(UIView*)parent {
    CGFloat totalH = _items.count * kDropItemH + 16.0f;
    CGFloat x = _anchorLeft ? anchorPt.x : anchorPt.x - kDropItemW;
    CGFloat y = anchorPt.y;
    // Clamp to parent bounds
    CGFloat maxX = parent.bounds.size.width  - kDropItemW - 8.0f;
    CGFloat maxY = parent.bounds.size.height - totalH     - 8.0f;
    x = MAX(8.0f, MIN(x, maxX));
    y = MAX(kButtonH + kButtonMargin * 2 + 8.0f, MIN(y, maxY));
    self.frame = CGRectMake(x, y, kDropItemW, totalH);
}

- (void)drawRect:(CGRect)rect {
    [super drawRect:rect];
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGFloat padding = 8.0f;

    for (NSInteger i = 0; i < (NSInteger)_items.count; i++) {
        CGRect itemRect = CGRectMake(padding, padding + i * kDropItemH,
                                     kDropItemW - padding * 2, kDropItemH - 2.0f);

        // Highlight background
        if (i == _highlightedIndex) {
            UIColor *hl = [UIColor colorWithRed:0.20f green:0.50f blue:1.0f alpha:0.80f];
            [hl setFill];
            UIBezierPath *bg = [UIBezierPath bezierPathWithRoundedRect:itemRect
                                                          cornerRadius:8.0f];
            CGContextAddPath(ctx, bg.CGPath);
            CGContextFillPath(ctx);
        }

        // Label
        NSString *label = _items[i];
        if (!label || label.length == 0) label = @"(empty)";

        NSDictionary *attrs = @{
            NSFontAttributeName: [UIFont systemFontOfSize:15.0f weight:UIFontWeightMedium],
            NSForegroundColorAttributeName: (i == _highlightedIndex)
                ? [UIColor whiteColor]
                : [UIColor colorWithWhite:0.88f alpha:1.0f],
        };
        CGRect textRect = CGRectInset(itemRect, 12.0f, 0.0f);
        CGFloat textH = [label boundingRectWithSize:CGSizeMake(textRect.size.width, CGFLOAT_MAX)
                                            options:NSStringDrawingUsesLineFragmentOrigin
                                         attributes:attrs
                                            context:nil].size.height;
        textRect.origin.y += (kDropItemH - textH - 2.0f) / 2.0f;
        [label drawInRect:textRect withAttributes:attrs];
    }
}

- (NSInteger)indexForWindowPoint:(CGPoint)windowPt {
    CGPoint local = [self convertPoint:windowPt fromView:self.window];
    if (!CGRectContainsPoint(self.bounds, local)) return -1;
    CGFloat padding = 8.0f;
    NSInteger idx = (NSInteger)((local.y - padding) / kDropItemH);
    if (idx < 0 || idx >= (NSInteger)_items.count) return -1;
    return idx;
}

- (void)setHighlightedIndex:(NSInteger)highlightedIndex {
    if (_highlightedIndex != highlightedIndex) {
        _highlightedIndex = highlightedIndex;
        [self setNeedsDisplay];
    }
}

@end

// ============================================================
// Corner button view
// ============================================================
typedef NS_ENUM(NSInteger, AAButtonSide) {
    AAButtonSideLeft  = 0,
    AAButtonSideRight = 1,
};

@interface AACornerButton : UIView {
    NSTimeInterval _touchBeganTime;
    UITouch       *_trackingTouch;
    NSTimeInterval _lastTapTime;
    NSUInteger     _tapCount;
    BOOL           _didShowDropdown;
    CADisplayLink *_holdTimer;
    NSTimer       *_singleTapTimer;  // delays single-tap to allow double-tap detection
    NSInteger      _activeMode;  // 0=none, 1=single (score/chat), 2=double (menu/console)
}

@property (nonatomic, assign) AAButtonSide side;
@property (nonatomic, weak)   AADropdownView *dropdownView;  // weak: owned by parent
@property (nonatomic, weak)   UIView         *overlayParent; // the full-screen overlay
@property (nonatomic, copy)   void (^onMenuOpened)(void);   // called when double-tap fires on left

- (void)resetState;  // clears activeMode without injecting keys (called on game-state change)

@end

@implementation AACornerButton

- (instancetype)initWithSide:(AAButtonSide)side {
    self = [super initWithFrame:CGRectZero];
    if (self) {
        _side = side;
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = YES;
        self.multipleTouchEnabled   = NO;
        [self setupGestures];
    }
    return self;
}

// ---- Visual ----

- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();

    // Semi-transparent pill background — green when a mode is active
    UIColor *bg = (_activeMode != 0)
        ? [UIColor colorWithRed:0.0f green:0.55f blue:0.20f alpha:0.55f]
        : [UIColor colorWithWhite:0.0f alpha:0.18f];
    [bg setFill];
    UIBezierPath *pill = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(rect, 2, 2)
                                                    cornerRadius:10.0f];
    CGContextAddPath(ctx, pill.CGPath);
    CGContextFillPath(ctx);

    // Border — barely visible
    [[UIColor colorWithWhite:1.0f alpha:0.12f] setStroke];
    pill.lineWidth = 0.5f;
    [pill stroke];

    NSString *label = (_side == AAButtonSideLeft) ? @"L1" : @"R1";
    NSDictionary *attrs = @{
        NSFontAttributeName: [UIFont systemFontOfSize:13.0f weight:UIFontWeightSemibold],
        NSForegroundColorAttributeName: [UIColor colorWithWhite:0.95f alpha:0.85f],
    };
    CGSize sz = [label sizeWithAttributes:attrs];
    [label drawAtPoint:CGPointMake((rect.size.width - sz.width) / 2,
                                   (rect.size.height - sz.height) / 2)
        withAttributes:attrs];
}

// ---- Dropdown helpers ----

- (NSArray<NSString*>*)dropdownItems {
    if (_side == AAButtonSideLeft) {
        return @[ @"Touch Mode 1  (steer L/R)",
                  @"Touch Mode 2  (swipe)",
                  @"Touch Mode 3  (buttons)" ];
    } else {
        // Right: first 8 instant-chat strings for player 0
        NSMutableArray *arr = [NSMutableArray arrayWithCapacity:8];
        for (int i = 0; i < 8; i++) {
            const char *s = aa_getInstantChatString(0, i);
            NSString *str = s ? @(s) : @"";
            [arr addObject:str];
        }
        return arr;
    }
}

- (void)showDropdown {
    if (_dropdownView) return;   // already visible
    _didShowDropdown = YES;

    AADropdownView *dd = [[AADropdownView alloc] initWithItems:[self dropdownItems]];
    dd.anchorLeft = (_side == AAButtonSideLeft);

    // Anchor just below this button
    CGPoint anchor = [self convertPoint:CGPointMake(_side == AAButtonSideLeft ? 0.0f
                                                                              : self.bounds.size.width,
                                                    self.bounds.size.height + 4.0f)
                                 toView:_overlayParent];
    [dd layoutAtAnchor:anchor inView:_overlayParent];
    [_overlayParent addSubview:dd];
    _dropdownView = dd;
}

- (void)updateDropdownForTouch:(UITouch*)touch {
    if (!_dropdownView) return;
    CGPoint pt = [touch locationInView:self.window];
    NSInteger idx = [_dropdownView indexForWindowPoint:pt];
    _dropdownView.highlightedIndex = idx;
}

- (void)commitDropdownWithTouch:(UITouch*)touch {
    if (!_dropdownView) return;

    CGPoint pt  = [touch locationInView:self.window];
    NSInteger idx = [_dropdownView indexForWindowPoint:pt];

    [_dropdownView removeFromSuperview];
    // _dropdownView becomes nil (weak ref)

    if (idx < 0) return;  // released outside → cancel

    if (_side == AAButtonSideLeft) {
        // Switch touch mode (1-based in the list)
        su_SetEnableTouch((int)(idx + 1));
    } else {
        // Trigger instant chat via F-key injection
        SDL_Scancode fkeys[] = {
            SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
            SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8
        };
        if (idx < 8) injectKey(fkeys[idx]);
    }
}

- (void)updateDropdownForPoint:(CGPoint)pt {
    if (!_dropdownView) return;
    NSInteger idx = [_dropdownView indexForWindowPoint:pt];
    _dropdownView.highlightedIndex = idx;
}

- (void)commitDropdownAtPoint:(CGPoint)pt {
    if (!_dropdownView) return;
    NSInteger idx = [_dropdownView indexForWindowPoint:pt];
    [_dropdownView removeFromSuperview];
    if (idx < 0) return;
    if (_side == AAButtonSideLeft) {
        su_SetEnableTouch((int)(idx + 1));
    } else {
        SDL_Scancode fkeys[] = {
            SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
            SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8
        };
        if (idx < 8) injectKey(fkeys[idx]);
    }
}

- (void)dismissDropdown {
    [_dropdownView removeFromSuperview];
    // _dropdownView becomes nil (weak)
}

// Hold timer removed — UILongPressGestureRecognizer handles long-press
- (void)holdTimerFired:(id)unused {
}

// ---- UIResponder touch handling ----

- (void)setupGestures {
    UITapGestureRecognizer *singleTap = [[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(handleSingleTap:)];
    singleTap.numberOfTapsRequired = 1;

    UITapGestureRecognizer *doubleTap = [[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(handleDoubleTap:)];
    doubleTap.numberOfTapsRequired = 2;

    UILongPressGestureRecognizer *longPress = [[UILongPressGestureRecognizer alloc]
        initWithTarget:self action:@selector(handleLongPress:)];
    longPress.minimumPressDuration = kLongPressThreshold;

    // Single tap waits for double-tap to fail
    [singleTap requireGestureRecognizerToFail:doubleTap];
    [singleTap requireGestureRecognizerToFail:longPress];
    [doubleTap requireGestureRecognizerToFail:longPress];

    [self addGestureRecognizer:singleTap];
    [self addGestureRecognizer:doubleTap];
    [self addGestureRecognizer:longPress];
}

- (void)handleSingleTap:(UITapGestureRecognizer*)gesture {
    [self fireSingleTap];
}

- (void)handleDoubleTap:(UITapGestureRecognizer*)gesture {
    [self fireDoubleTap];
}

- (void)handleLongPress:(UILongPressGestureRecognizer*)gesture {
    if (gesture.state == UIGestureRecognizerStateBegan) {
        [self showDropdown];
    } else if (gesture.state == UIGestureRecognizerStateChanged) {
        [self updateDropdownForPoint:[gesture locationInView:self.window]];
    } else if (gesture.state == UIGestureRecognizerStateEnded) {
        [self commitDropdownAtPoint:[gesture locationInView:self.window]];
    } else if (gesture.state == UIGestureRecognizerStateCancelled) {
        [self dismissDropdown];
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:0.70f];
}

// Touch visual feedback (gesture recognizers handle the logic)
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:1.0f];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self setAlpha:1.0f];
}

// ---- Actions ----

// Close whatever is currently active without toggling back
- (void)closeActive {
    if (_activeMode == 1 && _side == AAButtonSideRight) {
        aa_TriggerPlayerAction("CHAT");  // toggle chat off
        SDL_StopTextInput(SDL_GetKeyboardFocus());
    } else if (_activeMode == 2 && _side == AAButtonSideRight) {
        aa_TriggerAction("CONSOLE_INPUT");  // toggle console off
        SDL_StopTextInput(SDL_GetKeyboardFocus());
    }
    _activeMode = 0;
    [self setNeedsDisplay];
}

// Reset state silently (no key injection) — used when returning from menu
- (void)resetState {
    _activeMode = 0;
    [self setNeedsDisplay];
}

- (void)fireSingleTap {
    if (_side == AAButtonSideLeft) {
        aa_TriggerAction("SCORE");  // uses existing action binding system
    } else {
        // Chat is only available in networked games
        if (!aa_IsChatAvailable()) return;

        // Toggle chat via the game's CHAT action (same as keyboard binding)
        aa_TriggerPlayerAction("CHAT");
        if (_activeMode == 1) {
            _activeMode = 0;
            SDL_StopTextInput(SDL_GetKeyboardFocus());
        } else {
            _activeMode = 1;
            SDL_StartTextInput(SDL_GetKeyboardFocus());
        }
    }
    [self setNeedsDisplay];
}

- (void)fireDoubleTap {
    if (_side == AAButtonSideLeft) {
        // Open in-game menu via existing action binding system.
        // Buttons stay visible — the menu has its own "Return to Game" option.
        aa_TriggerAction("INGAME_MENU");
        _activeMode = 0;
        [self setNeedsDisplay];
    } else {
        // Double-tap = console input (works in all modes, unlike chat).
        // First tap may have triggered chat — close it first.
        if (_activeMode == 1) {
            aa_TriggerPlayerAction("CHAT");
        }
        aa_TriggerAction("CONSOLE_INPUT");
        if (_activeMode == 2) {
            _activeMode = 0;
            SDL_StopTextInput(SDL_GetKeyboardFocus());
        } else {
            _activeMode = 2;
            SDL_StartTextInput(SDL_GetKeyboardFocus());
        }
        [self setNeedsDisplay];
    }
}

@end

// ============================================================
// Full-screen transparent overlay
// Pass touches through to SDL except on the corner buttons.
// ============================================================
@interface AATouchOverlayView : UIView {
    BOOL _menuMode;
    UIView *_resumeButton;
    int  _lastViewportConf;   // conf we last laid out buttons for (-1 = needs update)
}
@property (nonatomic, strong) AACornerButton *leftBtn;
@property (nonatomic, strong) AACornerButton *rightBtn;
@property (nonatomic, strong) AAGyroButton   *gyroBtn;
@property (nonatomic, strong) AAFreezeButton *freezeBtn;

- (void)enterMenuMode;
- (void)exitMenuMode;
// Reposition buttons for the current viewport configuration (call from timer).
- (void)updateForViewports;

@end

@implementation AATouchOverlayView

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor            = [UIColor clearColor];
        self.userInteractionEnabled     = YES;
        self.multipleTouchEnabled       = YES;
        self.autoresizingMask           = UIViewAutoresizingFlexibleWidth |
                                          UIViewAutoresizingFlexibleHeight;
        _lastViewportConf = -1;

        _leftBtn  = [[AACornerButton alloc] initWithSide:AAButtonSideLeft];
        _rightBtn = [[AACornerButton alloc] initWithSide:AAButtonSideRight];
        _leftBtn.overlayParent  = self;
        _rightBtn.overlayParent = self;

        __weak typeof(self) weakSelf = self;
        _leftBtn.onMenuOpened = ^{ [weakSelf enterMenuMode]; };

        _gyroBtn   = [[AAGyroButton alloc] init];
        _freezeBtn = [[AAFreezeButton alloc] init];

        // Resume button — shown only in menu mode
        _resumeButton = [[UIView alloc] init];
        _resumeButton.backgroundColor   = [UIColor colorWithRed:0.10f green:0.45f blue:0.90f alpha:0.75f];
        _resumeButton.layer.cornerRadius = 10.0f;
        _resumeButton.hidden            = YES;
        _resumeButton.userInteractionEnabled = YES;

        UILabel *lbl = [[UILabel alloc] init];
        lbl.text          = @"RETURN TO GAME";
        lbl.textColor     = [UIColor whiteColor];
        lbl.font          = [UIFont systemFontOfSize:13.0f weight:UIFontWeightSemibold];
        lbl.textAlignment = NSTextAlignmentCenter;
        lbl.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [_resumeButton addSubview:lbl];

        UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc]
            initWithTarget:self action:@selector(resumeTapped)];
        [_resumeButton addGestureRecognizer:tap];

        [self addSubview:_leftBtn];
        [self addSubview:_rightBtn];
        [self addSubview:_gyroBtn];
        [self addSubview:_freezeBtn];
        [self addSubview:_resumeButton];
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    _lastViewportConf = -1; // force re-layout on next timer tick
    [self updateForViewports];
}

- (void)enterMenuMode {
    _menuMode = YES;
    _leftBtn.hidden   = YES;
    _rightBtn.hidden  = YES;
    _gyroBtn.hidden   = YES;
    _freezeBtn.hidden = YES;
    _resumeButton.hidden = NO;
}

- (void)exitMenuMode {
    _menuMode = NO;
    _resumeButton.hidden = YES;
    _leftBtn.hidden   = NO;
    _rightBtn.hidden  = NO;
    _gyroBtn.hidden   = NO;
    _freezeBtn.hidden = NO;
    // Reset left button state (menu is now closed)
    [_leftBtn resetState];
}

- (void)resumeTapped {
    // Close the in-game menu and return to game
    injectKey(SDL_SCANCODE_ESCAPE);
    [self exitMenuMode];
}

// Reposition and rotate the four button views to match the current viewport layout.
// Called from the 0.2 s timer. No-ops if the configuration hasn't changed.
- (void)updateForViewports {
    int numVp = aa_getNumViewports();

    CGFloat W = self.bounds.size.width;
    CGFloat H = self.bounds.size.height;
    if (W < 1.0f || H < 1.0f) return; // bounds not ready yet — retry on next tick

    // Check viewport 0's rotation to detect config changes (e.g. top-bottom vs left-right
    // both have 2 viewports but different rotations). Combine with count for cache key.
    int rot0 = 0;
    if (numVp > 0) {
        float dummy;
        aa_getViewportInfo(0, &dummy, &dummy, &dummy, &dummy, &rot0);
    }
    int cacheKey = numVp * 1000 + rot0;
    if (cacheKey == _lastViewportConf) return;
    _lastViewportConf = cacheKey;

    // We always show buttons for viewport 0 (primary player) using the existing
    // _leftBtn / _rightBtn / _gyroBtn / _freezeBtn views.
    // For multi-viewport we pick viewport 0 for "main" buttons and, for the
    // second viewport (if any), reuse those same views repositioned to player 1's
    // edge — a single player can't be in two viewports at once, so one set suffices.
    //
    // For a proper per-player setup (4 players) a future refactor would create
    // a button group per viewport. For now we show controls for viewport 0 only.
    // (Player in viewport 0 uses them; other players can rely on touch-mode 2.)

    // --- resolve viewport 0 info ---
    float vL = 0, vB = 0, vW = 1, vH = 1; int rotDeg = 0;
    aa_getViewportInfo(0, &vL, &vB, &vW, &vH, &rotDeg);

    // Convert OpenGL viewport (bottom-origin) to UIKit (top-origin)
    CGFloat vpLeft   = vL * W;
    CGFloat vpWidth  = vW * W;
    CGFloat vpHeight = vH * H;
    CGFloat vpTop    = (1.0f - vB - vH) * H;  // UIKit y: top = 1 - (bottom + height)

    // Determine which edge buttons live at (the edge nearest to the sitting player).
    // rotDeg 0→top-corners (single player natural hold), 90→left, 180→top, 270→right
    CGRect leftFrame, rightFrame, gyroFrame, freezeFrame;
    CGAffineTransform btnRot = CGAffineTransformMakeRotation((CGFloat)rotDeg * M_PI / 180.0);

    if (rotDeg == 0) {
        // Single player / bottom player: buttons at physical TOP corners of viewport.
        // (Natural tablet hold has thumbs at top-left and top-right.)
        CGFloat bY = vpTop + kButtonMargin;
        CGFloat sY = bY + kButtonH + kButtonMargin;
        leftFrame   = CGRectMake(vpLeft + kButtonMargin,            bY, kButtonW, kButtonH);
        rightFrame  = CGRectMake(vpLeft + vpWidth - kButtonW - kButtonMargin, bY, kButtonW, kButtonH);
        gyroFrame   = CGRectMake(vpLeft + kButtonMargin,            sY, kButtonW, kSubButtonH);
        freezeFrame = CGRectMake(vpLeft + vpWidth - kButtonW - kButtonMargin, sY, kButtonW, kSubButtonH);
    } else if (rotDeg == 180) {
        // Player sits at top → buttons at physical top of viewport
        CGFloat bY = vpTop + kButtonMargin;
        CGFloat sY = bY + kButtonH + kButtonMargin;
        leftFrame   = CGRectMake(vpLeft + kButtonMargin,            bY, kButtonW, kButtonH);
        rightFrame  = CGRectMake(vpLeft + vpWidth - kButtonW - kButtonMargin, bY, kButtonW, kButtonH);
        gyroFrame   = CGRectMake(vpLeft + kButtonMargin,            sY, kButtonW, kSubButtonH);
        freezeFrame = CGRectMake(vpLeft + vpWidth - kButtonW - kButtonMargin, sY, kButtonW, kSubButtonH);
    } else if (rotDeg == 90) {
        // Player sits at left → buttons at physical left of viewport, stacked vertically
        CGFloat bX = vpLeft + kButtonMargin;
        CGFloat sX = bX + kButtonW + kButtonMargin;
        leftFrame   = CGRectMake(bX, vpTop + kButtonMargin,            kButtonW, kButtonH);
        rightFrame  = CGRectMake(bX, vpTop + vpHeight - kButtonH - kButtonMargin, kButtonW, kButtonH);
        gyroFrame   = CGRectMake(sX, vpTop + kButtonMargin,            kSubButtonH, kButtonH);
        freezeFrame = CGRectMake(sX, vpTop + vpHeight - kButtonH - kButtonMargin, kSubButtonH, kButtonH);
    } else { // 270
        // Player sits at right → buttons at physical right of viewport, stacked vertically
        CGFloat bX = vpLeft + vpWidth - kButtonW - kButtonMargin;
        CGFloat sX = bX - kSubButtonH - kButtonMargin;
        leftFrame   = CGRectMake(bX, vpTop + kButtonMargin,            kButtonW, kButtonH);
        rightFrame  = CGRectMake(bX, vpTop + vpHeight - kButtonH - kButtonMargin, kButtonW, kButtonH);
        gyroFrame   = CGRectMake(sX, vpTop + kButtonMargin,            kSubButtonH, kButtonH);
        freezeFrame = CGRectMake(sX, vpTop + vpHeight - kButtonH - kButtonMargin, kSubButtonH, kButtonH);
    }

    // Apply frames and text-rotation transforms so labels are readable for the player.
    _leftBtn.frame   = leftFrame;   _leftBtn.transform   = btnRot;
    _rightBtn.frame  = rightFrame;  _rightBtn.transform  = btnRot;
    _gyroBtn.frame   = gyroFrame;   _gyroBtn.transform   = btnRot;
    _freezeBtn.frame = freezeFrame; _freezeBtn.transform = btnRot;

    // Resume button stays centered (used in menu mode, player 0 always at bottom in single-vp)
    CGFloat rw = 200.0f, rh = 44.0f;
    _resumeButton.frame = CGRectMake((W - rw) * 0.5f, kButtonMargin, rw, rh);
    for (UIView *v in _resumeButton.subviews) v.frame = _resumeButton.bounds;
}

// Capture touches on game buttons, resume button, and any visible dropdown.
// Uses subview coordinate conversion so rotated (transformed) buttons are hit-tested correctly.
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent*)event {
    if (_menuMode) {
        CGPoint local = [_resumeButton convertPoint:point fromView:self];
        return [_resumeButton pointInside:local withEvent:event];
    }
    NSArray<UIView*> *btns = @[_leftBtn, _rightBtn, _gyroBtn, _freezeBtn];
    for (UIView *btn in btns) {
        if (btn.hidden) continue;
        CGPoint local = [btn convertPoint:point fromView:self];
        if ([btn pointInside:local withEvent:event]) return YES;
    }
    for (UIView *sub in self.subviews) {
        if ([sub isKindOfClass:[AADropdownView class]] && !sub.hidden) {
            CGPoint local = [sub convertPoint:point fromView:self];
            if ([sub pointInside:local withEvent:event]) return YES;
        }
    }
    return NO;
}

@end

// C++ viewport bridge — in rTouchBridgeIOS.mm
extern "C" int aa_getNumViewports();
extern "C" int aa_getViewportInfo(int vpIdx, float* outLeft, float* outBottom,
                                  float* outWidth, float* outHeight, int* outRotDeg);

// aa_getInstantChatString is defined in rTouchBridgeIOS.mm (non-ARC, C++ headers)
// sg_GameRunning() is in gGame.cpp — declared here with C++ linkage.
extern bool sg_GameRunning();

// Shared overlay instance — kept alive for the app lifetime.
static AATouchOverlayView *s_overlay = nil;

// Called every 0.2 s from an NSTimer: show overlay iff a game is running,
// and update console insets accordingly.
static void updateOverlayVisibility() {
    if (!s_overlay) return;

    bool inGame = sg_GameRunning();
    BOOL shouldShow = inGame ? YES : NO;

    // If the game ended while menu mode was active (user left via menu), reset
    if (!inGame) {
        [s_overlay exitMenuMode];
        [s_overlay.freezeBtn resetState];
    }

    if (s_overlay.hidden == shouldShow) {
        s_overlay.hidden = !shouldShow;

        if (inGame) {
            // Compute inset in OpenGL [−1,+1] coords.
            CGFloat screenW = s_overlay.bounds.size.width;
            if (screenW > 0) {
                float buttonFraction = (float)((kButtonW + kButtonMargin * 2 + 4.0) / screenW);
                float inset = buttonFraction * 2.0f;
                sr_consoleInsetLeft  = inset;
                sr_consoleInsetRight = inset;
            }
        } else {
            sr_consoleInsetLeft  = 0.0f;
            sr_consoleInsetRight = 0.0f;
        }
    }

    // Update button positions whenever viewport config may have changed.
    if (inGame) [s_overlay updateForViewports];
}

// ============================================================
// Installation
// ============================================================
extern "C" void aa_installTouchOverlay(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            UIWindow *window = nil;
            for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
                if (![scene isKindOfClass:[UIWindowScene class]]) continue;
                UIWindowScene *ws = (UIWindowScene*)scene;
                window = ws.windows.firstObject;
                if (window) break;
            }
            if (!window) return;

            CGRect bounds = window.bounds;
            s_overlay = [[AATouchOverlayView alloc] initWithFrame:bounds];
            s_overlay.hidden = YES;  // hidden until a game is running
            s_overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                         UIViewAutoresizingFlexibleHeight;
            [window addSubview:s_overlay];
            [window bringSubviewToFront:s_overlay];

            // Poll game state every 200 ms.
            [NSTimer scheduledTimerWithTimeInterval:0.2
                                            repeats:YES
                                              block:^(NSTimer*){ updateOverlayVisibility(); }];
        }
    });
}
