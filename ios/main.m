#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

#include <math.h>
#include "../progress.h"
#include "../render.h"
#include "../vendor/celeste.h"

static NSString *const kProgressKey = @"CelesteProgressV1";
static NSString *const kPlaySecondsKey = @"CelestePlaySeconds";
static NSString *const kTotalDashesKey = @"CelesteTotalDashes";

extern uint64_t celeste_ios_dashes;

enum {
    CelesteLeft = 0,
    CelesteRight = 1,
    CelesteUp = 2,
    CelesteDown = 3,
    CelesteJump = 4,
    CelesteDash = 5,
};

@interface GameViewController : UIViewController
@end

@implementation GameViewController {
    UIImageView *_screen;
    CADisplayLink *_displayLink;
    Progress _progress;
    BOOL _paused;
    BOOL _foreground;
    NSTimeInterval _lastTick;
    NSTimeInterval _unsavedPlaySeconds;
    uint64_t _totalPlaySeconds;
    uint64_t _totalDashes;
    uint64_t _lastCoreDashes;
    UILabel *_pauseLabel;
    NSMutableSet<UIButton *> *_activeButtons;
}

static const uint8_t kPico8Palette[16][3] = {
    {0, 0, 0},       {29, 43, 83},    {126, 37, 83},   {0, 135, 81},
    {171, 82, 54},   {95, 87, 79},    {194, 195, 199}, {255, 241, 232},
    {255, 0, 77},    {255, 163, 0},   {255, 236, 39},  {0, 228, 54},
    {41, 173, 255},  {131, 118, 156}, {255, 119, 168}, {255, 204, 170},
};

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.045 green:0.055 blue:0.09 alpha:1];
    _foreground = YES;
    _activeButtons = [NSMutableSet set];

    [self loadProgress];
    render_init();
    Celeste_P8_set_call_func(render_callback);
    Celeste_P8_set_rndseed((unsigned)NSDate.date.timeIntervalSince1970);
    Celeste_P8_init();
    Celeste_Flipper_resume((int)_progress.room, _progress.fruit,
                           (int)_progress.deaths, (int)_progress.double_dash);
    Celeste_Flipper_set_completed((int)_progress.completed);

    _screen = [[UIImageView alloc] init];
    _screen.translatesAutoresizingMaskIntoConstraints = NO;
    _screen.contentMode = UIViewContentModeScaleAspectFit;
    _screen.layer.magnificationFilter = kCAFilterNearest;
    _screen.layer.minificationFilter = kCAFilterNearest;
    _screen.backgroundColor = UIColor.blackColor;
    _screen.accessibilityLabel = @"Celeste game screen";
    [self.view addSubview:_screen];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_screen.topAnchor constraintEqualToAnchor:safe.topAnchor constant:4],
        [_screen.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_screen.widthAnchor constraintLessThanOrEqualToAnchor:safe.widthAnchor constant:-12],
        [_screen.heightAnchor constraintEqualToAnchor:_screen.widthAnchor],
    ]];
    NSLayoutConstraint *squareWidth = [_screen.widthAnchor constraintEqualToAnchor:safe.widthAnchor constant:-12];
    squareWidth.priority = UILayoutPriorityDefaultHigh;
    squareWidth.active = YES;

    [self buildControlsBelow:_screen safeArea:safe];
    [self installLifecycleObservers];

    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    if (@available(iOS 15.0, *)) {
        _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30, 30, 30);
    } else {
        _displayLink.preferredFramesPerSecond = 30;
    }
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (UIButton *)gameButton:(NSString *)title mask:(uint8_t)mask {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.tag = mask;
    button.titleLabel.font = [UIFont boldSystemFontOfSize:18];
    button.backgroundColor = [UIColor colorWithWhite:1 alpha:0.13];
    button.tintColor = UIColor.whiteColor;
    button.layer.cornerRadius = 14;
    button.layer.borderWidth = 1;
    button.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.2].CGColor;
    [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:self action:@selector(buttonDown:) forControlEvents:(UIControlEventTouchDown | UIControlEventTouchDragEnter)];
    [button addTarget:self action:@selector(buttonUp:) forControlEvents:(UIControlEventTouchUpInside |
                                                                          UIControlEventTouchUpOutside |
                                                                          UIControlEventTouchCancel |
                                                                          UIControlEventTouchDragExit)];
    return button;
}

- (void)menuHaptic:(id)sender {
    (void)sender;
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
}

- (UIButton *)menuButton:(NSString *)title action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    button.tintColor = UIColor.whiteColor;
    button.backgroundColor = [UIColor colorWithWhite:1 alpha:0.09];
    button.layer.cornerRadius = 10;
    [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:self action:@selector(menuHaptic:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (void)buildControlsBelow:(UIView *)screen safeArea:(UILayoutGuide *)safe {
    UIView *controls = [[UIView alloc] init];
    controls.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:controls];
    [NSLayoutConstraint activateConstraints:@[
        [controls.topAnchor constraintEqualToAnchor:screen.bottomAnchor constant:8],
        [controls.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:10],
        [controls.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-10],
        [controls.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-6],
    ]];

    uint8_t leftMask = 1u << CelesteLeft, rightMask = 1u << CelesteRight;
    uint8_t upMask = 1u << CelesteUp, downMask = 1u << CelesteDown;
    UIButton *upLeft = [self gameButton:@"↖" mask:upMask | leftMask];
    UIButton *up = [self gameButton:@"▲" mask:upMask];
    UIButton *upRight = [self gameButton:@"↗" mask:upMask | rightMask];
    UIButton *left = [self gameButton:@"◀" mask:leftMask];
    UIButton *right = [self gameButton:@"▶" mask:rightMask];
    UIButton *downLeft = [self gameButton:@"↙" mask:downMask | leftMask];
    UIButton *down = [self gameButton:@"▼" mask:downMask];
    UIButton *downRight = [self gameButton:@"↘" mask:downMask | rightMask];
    UIButton *jump = [self gameButton:@"JUMP" mask:1u << CelesteJump];
    UIButton *dash = [self gameButton:@"DASH" mask:1u << CelesteDash];
    dash.backgroundColor = [UIColor colorWithRed:0.55 green:0.18 blue:0.38 alpha:0.8];
    jump.backgroundColor = [UIColor colorWithRed:0.05 green:0.43 blue:0.42 alpha:0.8];

    UIView *pad = [[UIView alloc] init];
    pad.translatesAutoresizingMaskIntoConstraints = NO;
    UIView *center = [[UIView alloc] init];
    UIStackView *(^row)(NSArray<UIView *> *) = ^UIStackView *(NSArray<UIView *> *views) {
        UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:views];
        stack.axis = UILayoutConstraintAxisHorizontal;
        stack.spacing = 3;
        stack.distribution = UIStackViewDistributionFillEqually;
        return stack;
    };
    UIStackView *padGrid = [[UIStackView alloc] initWithArrangedSubviews:@[
        row(@[upLeft, up, upRight]), row(@[left, center, right]),
        row(@[downLeft, down, downRight])
    ]];
    padGrid.translatesAutoresizingMaskIntoConstraints = NO;
    padGrid.axis = UILayoutConstraintAxisVertical;
    padGrid.spacing = 3;
    padGrid.distribution = UIStackViewDistributionFillEqually;
    [pad addSubview:padGrid];
    [NSLayoutConstraint activateConstraints:@[
        [pad.widthAnchor constraintEqualToConstant:180],
        [pad.heightAnchor constraintEqualToAnchor:pad.widthAnchor],
        [padGrid.topAnchor constraintEqualToAnchor:pad.topAnchor],
        [padGrid.leadingAnchor constraintEqualToAnchor:pad.leadingAnchor],
        [padGrid.trailingAnchor constraintEqualToAnchor:pad.trailingAnchor],
        [padGrid.bottomAnchor constraintEqualToAnchor:pad.bottomAnchor],
    ]];

    UIStackView *actions = [[UIStackView alloc] initWithArrangedSubviews:@[jump, dash]];
    actions.translatesAutoresizingMaskIntoConstraints = NO;
    actions.axis = UILayoutConstraintAxisHorizontal;
    actions.spacing = 10;
    actions.distribution = UIStackViewDistributionFillEqually;
    [jump.heightAnchor constraintGreaterThanOrEqualToConstant:72].active = YES;

    UIButton *pause = [self menuButton:@"Pause" action:@selector(togglePause:)];
    UIButton *stats = [self menuButton:@"Stats" action:@selector(showStats:)];
    UIButton *restart = [self menuButton:@"Restart" action:@selector(confirmRestart:)];
    UIStackView *menu = [[UIStackView alloc] initWithArrangedSubviews:@[pause, stats, restart]];
    menu.translatesAutoresizingMaskIntoConstraints = NO;
    menu.axis = UILayoutConstraintAxisHorizontal;
    menu.spacing = 8;
    menu.distribution = UIStackViewDistributionFillEqually;
    [menu.heightAnchor constraintEqualToConstant:38].active = YES;

    _pauseLabel = [[UILabel alloc] init];
    _pauseLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _pauseLabel.text = @"PAUSED";
    _pauseLabel.textColor = UIColor.whiteColor;
    _pauseLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.72];
    _pauseLabel.textAlignment = NSTextAlignmentCenter;
    _pauseLabel.font = [UIFont boldSystemFontOfSize:22];
    _pauseLabel.hidden = YES;
    _pauseLabel.accessibilityTraits = UIAccessibilityTraitStaticText;
    [self.view addSubview:_pauseLabel];
    [NSLayoutConstraint activateConstraints:@[
        [_pauseLabel.leadingAnchor constraintEqualToAnchor:screen.leadingAnchor],
        [_pauseLabel.trailingAnchor constraintEqualToAnchor:screen.trailingAnchor],
        [_pauseLabel.centerYAnchor constraintEqualToAnchor:screen.centerYAnchor],
        [_pauseLabel.heightAnchor constraintEqualToConstant:48],
    ]];

    [controls addSubview:pad];
    [controls addSubview:actions];
    [controls addSubview:menu];
    UIView *playArea = [[UIView alloc] init];
    playArea.translatesAutoresizingMaskIntoConstraints = NO;
    [controls insertSubview:playArea atIndex:0];
    [NSLayoutConstraint activateConstraints:@[
        [menu.leadingAnchor constraintEqualToAnchor:controls.leadingAnchor],
        [menu.trailingAnchor constraintEqualToAnchor:controls.trailingAnchor],
        [menu.bottomAnchor constraintEqualToAnchor:controls.bottomAnchor],
        [playArea.topAnchor constraintEqualToAnchor:controls.topAnchor],
        [playArea.leadingAnchor constraintEqualToAnchor:controls.leadingAnchor],
        [playArea.trailingAnchor constraintEqualToAnchor:controls.trailingAnchor],
        [playArea.bottomAnchor constraintEqualToAnchor:menu.topAnchor constant:-8],
        [pad.leadingAnchor constraintEqualToAnchor:playArea.leadingAnchor],
        [pad.centerYAnchor constraintEqualToAnchor:playArea.centerYAnchor],
        [pad.topAnchor constraintGreaterThanOrEqualToAnchor:playArea.topAnchor],
        [pad.bottomAnchor constraintLessThanOrEqualToAnchor:playArea.bottomAnchor],
        [actions.leadingAnchor constraintEqualToAnchor:pad.trailingAnchor constant:10],
        [actions.trailingAnchor constraintEqualToAnchor:playArea.trailingAnchor],
        [actions.centerYAnchor constraintEqualToAnchor:pad.centerYAnchor],
    ]];
}

- (void)buttonDown:(UIButton *)sender {
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
    [_activeButtons addObject:sender];
    [self recomputeButtons];
}

- (void)buttonUp:(UIButton *)sender {
    [_activeButtons removeObject:sender];
    [self recomputeButtons];
}

- (void)recomputeButtons {
    uint8_t buttons = 0;
    for (UIButton *button in _activeButtons) buttons |= (uint8_t)button.tag;
    render_buttons = buttons;
}

- (void)clearButtons {
    [_activeButtons removeAllObjects];
    render_buttons = 0;
}

- (void)tick:(CADisplayLink *)link {
    NSTimeInterval now = link.timestamp;
    NSTimeInterval elapsed = _lastTick > 0 ? MIN(now - _lastTick, 0.1) : 0;
    _lastTick = now;
    if (_paused || !_foreground) return;

    _unsavedPlaySeconds += elapsed;
    Celeste_P8_update();
    Celeste_P8_draw();
    if (celeste_ios_dashes >= _lastCoreDashes)
        _totalDashes += celeste_ios_dashes - _lastCoreDashes;
    _lastCoreDashes = celeste_ios_dashes;
    [self presentFrame];

    progress_update(&_progress, Celeste_Flipper_room(), Celeste_Flipper_fruit(),
                    Celeste_Flipper_deaths(), Celeste_Flipper_double_dash(),
                    Celeste_Flipper_completed());
    if (_unsavedPlaySeconds >= 1.0) [self saveProgress];
}

- (void)presentFrame {
    uint8_t rgba[128 * 128 * 4];
    for (NSUInteger pixel = 0; pixel < 128 * 128; pixel++) {
        uint8_t packed = render_frame[pixel >> 1];
        uint8_t color = (packed >> (4 * (pixel & 1))) & 0x0f;
        rgba[pixel * 4] = kPico8Palette[color][0];
        rgba[pixel * 4 + 1] = kPico8Palette[color][1];
        rgba[pixel * 4 + 2] = kPico8Palette[color][2];
        rgba[pixel * 4 + 3] = 255;
    }
    CFDataRef pixels = CFDataCreate(kCFAllocatorDefault, rgba, sizeof(rgba));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(pixels);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(128, 128, 8, 32, 128 * 4, space,
                                     kCGImageAlphaLast | kCGBitmapByteOrderDefault,
                                     provider, NULL, false, kCGRenderingIntentDefault);
    _screen.image = [UIImage imageWithCGImage:image scale:1 orientation:UIImageOrientationUp];
    CGImageRelease(image);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    CFRelease(pixels);
}

- (void)loadProgress {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    NSData *data = [defaults dataForKey:kProgressKey];
    if (data.length == sizeof(Progress)) [data getBytes:&_progress length:sizeof(Progress)];
    if (data.length != sizeof(Progress) || !progress_valid(&_progress)) progress_default(&_progress);
    _totalPlaySeconds = (uint64_t)[defaults doubleForKey:kPlaySecondsKey];
    _totalDashes = (uint64_t)[defaults doubleForKey:kTotalDashesKey];
    _lastCoreDashes = celeste_ios_dashes;
}

- (void)saveProgress {
    if (_unsavedPlaySeconds > 0) {
        _totalPlaySeconds += (uint64_t)floor(_unsavedPlaySeconds);
        _unsavedPlaySeconds -= floor(_unsavedPlaySeconds);
    }
    _progress.generation++;
    _progress.checksum = progress_checksum(&_progress);
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    [defaults setObject:[NSData dataWithBytes:&_progress length:sizeof(Progress)] forKey:kProgressKey];
    [defaults setDouble:(double)_totalPlaySeconds forKey:kPlaySecondsKey];
    [defaults setDouble:(double)_totalDashes forKey:kTotalDashesKey];
}

- (void)togglePause:(id)sender {
    (void)sender;
    _paused = !_paused;
    [self clearButtons];
    _pauseLabel.hidden = !_paused;
    if (!_paused) _lastTick = 0;
    [self saveProgress];
}

- (void)showStats:(id)sender {
    (void)sender;
    BOOL wasPaused = _paused;
    _paused = YES;
    [self clearButtons];
    [self saveProgress];
    uint64_t xp = (uint64_t)_progress.total_fruit + 10ULL * (uint64_t)_progress.playthroughs;
    NSString *message = [NSString stringWithFormat:
        @"Play time  %02llu:%02llu:%02llu\nDashes  %llu\nXP  %llu\nBerries  %u\nDeaths  %u\nRestarts  %u\nCompletions  %u\nBest room  %u/30",
        (unsigned long long)(_totalPlaySeconds / 3600),
        (unsigned long long)((_totalPlaySeconds / 60) % 60),
        (unsigned long long)(_totalPlaySeconds % 60),
        (unsigned long long)_totalDashes, (unsigned long long)xp,
        _progress.total_fruit, _progress.total_deaths,
        _progress.restarts, _progress.playthroughs, MIN(_progress.best_room + 1, 30u)];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Stats"
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    __weak typeof(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *action) {
        (void)action;
        GameViewController *strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_paused = wasPaused;
        strongSelf->_pauseLabel.hidden = !wasPaused;
        strongSelf->_lastTick = 0;
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)confirmRestart:(id)sender {
    (void)sender;
    BOOL wasPaused = _paused;
    _paused = YES;
    [self clearButtons];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Restart game?"
                                                                   message:@"Current run returns to the first room. Lifetime stats stay saved."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    __weak typeof(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel
                                            handler:^(UIAlertAction *action) {
        (void)action;
        GameViewController *strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_paused = wasPaused;
        strongSelf->_pauseLabel.hidden = !wasPaused;
        strongSelf->_lastTick = 0;
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Restart" style:UIAlertActionStyleDestructive
                                            handler:^(UIAlertAction *action) {
        (void)action;
        GameViewController *strongSelf = weakSelf;
        if (!strongSelf) return;
        progress_restart(&strongSelf->_progress);
        Celeste_Flipper_restart();
        Celeste_Flipper_set_completed(0);
        strongSelf->_paused = NO;
        strongSelf->_pauseLabel.hidden = YES;
        strongSelf->_lastTick = 0;
        [strongSelf saveProgress];
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)installLifecycleObservers {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center addObserver:self selector:@selector(applicationInactive:)
                   name:UIApplicationWillResignActiveNotification object:nil];
    [center addObserver:self selector:@selector(applicationActive:)
                   name:UIApplicationDidBecomeActiveNotification object:nil];
    [center addObserver:self selector:@selector(applicationBackground:)
                   name:UIApplicationDidEnterBackgroundNotification object:nil];
}

- (void)applicationInactive:(NSNotification *)note {
    (void)note;
    _foreground = NO;
    [self clearButtons];
    [self saveProgress];
}

- (void)applicationBackground:(NSNotification *)note {
    (void)note;
    [self clearButtons];
    [self saveProgress];
}

- (void)applicationActive:(NSNotification *)note {
    (void)note;
    _foreground = YES;
    _lastTick = 0;
}

- (void)dealloc {
    [_displayLink invalidate];
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskPortrait; }

@end

@interface CelesteSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation CelesteSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[GameViewController alloc] init];
    [self.window makeKeyAndVisible];
}
@end
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options; return YES;
}
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application; (void)options;
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"Default Configuration" sessionRole:session.role];
    configuration.delegateClass = CelesteSceneDelegate.class;
    return configuration;
}
@end
int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class)); }
}
