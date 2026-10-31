// macOS 状态项。core 只认 statusbar.h 的 C 接口，Cocoa 头不外泄。
#include "statusbar.h"

#include <cstdio>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>   // CGWindowListCopyWindowInfo

@interface SBController : NSObject {
    NSStatusItem* _item;
    NSMenu* _menu;
    sb_click_fn _onClick;
    void* _ud;
}
- (instancetype)initWithIcon:(NSString*)path
                     tooltip:(NSString*)tip
                    showHide:(NSString*)showHideLabel
                        quit:(NSString*)quitLabel
                       click:(sb_click_fn)click
                        user:(void*)ud;
- (void)setLabel:(NSString*)text;
- (void)setTooltip:(NSString*)text;
- (int)capture:(NSString*)path;
- (void)readback:(const char*)why;
@end

@implementation SBController

- (instancetype)initWithIcon:(NSString*)path
                     tooltip:(NSString*)tip
                    showHide:(NSString*)showHideLabel
                        quit:(NSString*)quitLabel
                       click:(sb_click_fn)click
                        user:(void*)ud {
    self = [super init];
    if (!self) return nil;
    _onClick = click;
    _ud = ud;

    NSImage* img = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    if (img) {
        img.size = NSMakeSize(18, 18);   // 36px rep 配 18pt：Retina 不糊
        [img setTemplate:YES];           // 状态项要单色轮廓；`template` 在 C++ 里是关键字
    } else {
        img = [NSImage imageWithSystemSymbolName:@"sparkles" accessibilityDescription:@"Agent Ping"];
    }

    _item = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    _item.button.image = img;
    _item.button.imagePosition = NSImageLeft;
    _item.button.target = self;
    _item.button.action = @selector(clicked:);
    [_item.button sendActionOn:(NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp)];
    if (tip.length) _item.button.toolTip = tip;
    // 初始标题就摆成 setLabel:@"") 的那个形状，否则第一帧会报一条「数字变了」——
    // 那条其实是「从空串变成空串」，读日志的人会当成丢了数。
    _item.button.title = @"  ";

    // 不给 _item.menu：一给，左键也被系统吃掉去弹菜单，click 回调就再也不触发。
    _menu = [[NSMenu alloc] init];
    if (showHideLabel.length) {
        NSMenuItem* toggle = [[NSMenuItem alloc] initWithTitle:showHideLabel
                                                        action:@selector(showHide:)
                                                 keyEquivalent:@""];
        toggle.target = self;
        [_menu addItem:toggle];
    }
    if (quitLabel.length) {
        NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:quitLabel
                                                      action:@selector(quitApp:)
                                               keyEquivalent:@""];
        quit.target = self;
        [_menu addItem:quit];
    }
    return self;
}

- (void)setLabel:(NSString*)text {
    NSString* old = _item.button.title;
    NSString* want = text.length ? [@" " stringByAppendingString:text] : @"  ";
    if ([old isEqual:want]) return;
    _item.button.title = want;
    [self readback:"数字变了"];
}

- (void)setTooltip:(NSString*)text {
    NSString* old = _item.button.toolTip ?: @"";
    if ([old isEqual:text]) return;
    _item.button.toolTip = text.length ? text : nil;
    [self readback:"悬停说明变了"];
}

/// 把「系统那一侧现在到底挂着什么」读回来打一行。为什么必须读而不是直接打我们传进去的串：
/// 状态项在菜单栏里，命令行截屏在这台机器上只会得到纯黑（没有屏幕录制授权），肉眼看不到，
/// 于是「数字改了」和「我以为改了」在日志里长一样。这里取的是 NSStatusBarButton 自己的
/// title/toolTip——setter 没生效就是空，报不出来。
- (void)readback:(const char*)why {
    NSString* t = _item.button.title ?: @"";
    NSString* tip = _item.button.toolTip ?: @"";
    // 菜单那两项也从 NSMenu 自己身上读：六语接上之后，「把文案传进去」和「系统真挂上了」
    // 是两件事——只打传入串的话，标题没生效照样能报出一句本地化。
    NSString* m1 = _menu && _menu.numberOfItems > 0 ? [_menu itemAtIndex:0].title : @"";
    NSString* m2 = _menu && _menu.numberOfItems > 1 ? [_menu itemAtIndex:1].title : @"";
    fprintf(stderr, "[状态项] %s：标题＝「%s」｜悬停＝「%s」｜菜单＝「%s」「%s」\n", why,
            t.UTF8String, tip.UTF8String, m1.UTF8String, m2.UTF8String);
    fflush(stderr);
}

/// 把按钮自己画进 PNG：cacheDisplay 走的是系统那一套排版（模板图染色 + 标题字距），
/// 不是我们照着重画的示意图。背景是透明菜单条材质，拍出来带 alpha，读图时要垫一层深色。
- (int)capture:(NSString*)path {
    NSRect b = _item.button.bounds;
    if (b.size.width <= 0 || b.size.height <= 0) return 1;
    NSBitmapImageRep* rep = [_item.button bitmapImageRepForCachingDisplayInRect:b];
    [_item.button cacheDisplayInRect:b toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png) return 2;
    if (![png writeToFile:path atomically:YES]) return 3;
    fprintf(stderr, "[状态项] 实拍 %s %ldx%ld 像素（按钮自绘，含系统模板图染色）\n",
            path.UTF8String, (long)rep.pixelsWide, (long)rep.pixelsHigh);
    fflush(stderr);
    return 0;
}

- (void)clicked:(id)sender {
    NSEvent* e = NSApp.currentEvent;
    BOOL right = (e.type == NSRightMouseDown) ||
                 (e.type == NSLeftMouseDown && (e.modifierFlags & NSEventModifierFlagControl));
    if (right) {
        NSPoint p = NSMakePoint(-10, _item.button.bounds.size.height);
        [_menu popUpMenuPositioningItem:nil atLocation:p inView:_item.button];
        return;
    }
    if (_onClick) _onClick(_ud);
}

- (void)showHide:(id)sender { if (_onClick) _onClick(_ud); }
- (void)quitApp:(id)sender { exit(0); }

@end

static SBController* g_ctl = nil;

int sb_show(const char* icon_path, const char* tooltip, const char* show_hide_label,
            const char* quit_label, sb_click_fn on_click, void* ud) {
    @autoreleasepool {
        // 挂上状态项＝这个 App 的入口在菜单栏，不在 Dock。GLFW 建窗口时把进程注册成了
        // Regular（实测 lsappinfo type="Foreground" 且启动后抢在前台），而 Info.plist 里的
        // LSUIElement 只在走 LaunchServices（open / 双击）时才生效——从命令行直接 exec 二进制
        // 会被忽略。所以这里当场翻成 Accessory：无 Dock 图标、不抢前台，Swift 侧同一档。
        if (NSApp) [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSString* path = icon_path ? [NSString stringWithUTF8String:icon_path] : nil;
        NSString* tip = tooltip ? [NSString stringWithUTF8String:tooltip] : @"";
        NSString* sh = show_hide_label ? [NSString stringWithUTF8String:show_hide_label] : @"";
        NSString* quit = quit_label ? [NSString stringWithUTF8String:quit_label] : @"";
        g_ctl = [[SBController alloc] initWithIcon:path tooltip:tip showHide:sh quit:quit
                                             click:on_click user:ud];
        if (!g_ctl) return 1;
        [g_ctl setLabel:@""];
        [g_ctl readback:"挂上状态项"];
        fprintf(stderr, "[状态项] 策略＝%d（0 常驻 Dock／1 只在菜单栏）\n",
                NSApp ? (int)NSApp.activationPolicy : -1);
        fflush(stderr);
        return 0;
    }
}

void sb_set_label(const char* utf8) {
    if (!g_ctl) return;
    @autoreleasepool {
        // 每 3 秒一轮都会走到这里：只有真变了才读回来打日志，不然重扫会把日志刷满。
        [g_ctl setLabel:utf8 ? [NSString stringWithUTF8String:utf8] : @""];
    }
}

void sb_set_tooltip(const char* utf8) {
    if (!g_ctl) return;
    @autoreleasepool {
        [g_ctl setTooltip:utf8 ? [NSString stringWithUTF8String:utf8] : @""];
    }
}

void sb_pump(void) {}

int sb_capture(const char* png_path) {
    if (!g_ctl || !png_path) return 1;
    @autoreleasepool { return [g_ctl capture:[NSString stringWithUTF8String:png_path]]; }
}

int sb_float_window(void) {
    @autoreleasepool {
        int n = 0;
        for (NSWindow* w in [NSApp windows]) {
            // 实测面板那块是 GLFWWindow、styleMask=0x4（GLFW 的"无边框"是留着位再藏标题栏，
            // 不是 NSWindowStyleMaskBorderless=0），所以按类名认，不按 styleMask。
            if (![NSStringFromClass([w class]) hasPrefix:@"GLFW"]) continue;
            [w setHasShadow:YES];
            [w setOpaque:NO];
            [w setBackgroundColor:[NSColor clearColor]];
            NSRect f = w.frame;
            // isVisible 只说明"被排上过屏"，不代表没被别的窗口盖住。occlusionState 又分不清
            // "被挡住"和"accessory 应用不跟踪"，所以直接查系统的在屏窗口表（这条不需要
            // 「屏幕录制」授权，能拿到 windowNumber/layer/alpha/是否 onscreen）。
            bool onscreen = false; int layer = -1; double wa = 0;
            if (CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID)) {
                for (CFIndex i = 0; i < CFArrayGetCount(list); i++) {
                    CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
                    int num = 0;
                    CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowNumber);
                    if (n) CFNumberGetValue(n, kCFNumberSInt32Type, &num);
                    if (num != w.windowNumber) continue;
                    CFBooleanRef on = (CFBooleanRef)CFDictionaryGetValue(d, kCGWindowIsOnscreen);
                    onscreen = (on == kCFBooleanTrue);
                    CFNumberRef ln = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowLayer);
                    if (ln) CFNumberGetValue(ln, kCFNumberSInt32Type, &layer);
                    CFNumberRef al = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowAlpha);
                    if (al) CFNumberGetValue(al, kCFNumberDoubleType, &wa);
                    break;
                }
                CFRelease(list);
            }
            printf("[浮层] %s frame=(%.0f,%.0f) %.0fx%.0f opaque=%d shadow=%d visible=%d "
                   "occlusion=0x%lx｜系统窗口表：在屏=%d layer=%d alpha=%.2f\n",
                   [[w className] UTF8String], f.origin.x, f.origin.y, f.size.width, f.size.height,
                   w.isOpaque, w.hasShadow, w.isVisible, (unsigned long)w.occlusionState,
                   onscreen, layer, wa);
            fflush(stdout);
            n++;
        }
        // 一个都没匹配上时必须出声并摊开每个窗口：否则「浮层没生效」和「没找到窗口」在日志里长一样。
        if (n == 0) {
            printf("[浮层] NSApp 里 %ld 个窗口，没有 GLFWWindow —— 面板不是这条路径建的：\n",
                   (long)[[NSApp windows] count]);
            for (NSWindow* w in [NSApp windows]) {
                NSRect f = w.frame;
                printf("        %s styleMask=0x%lx frame=(%.0f,%.0f) %.0fx%.0f visible=%d\n",
                       [[w className] UTF8String], (unsigned long)w.styleMask,
                       f.origin.x, f.origin.y, f.size.width, f.size.height, w.isVisible);
            }
        }
        fflush(stdout);
        return n;
    }
}

// 右缘手柄的「真穿透」裁判。core 报上来的点是 glfw 那套（y 向下、主屏左上为原点），
// AppKit 的屏幕坐标是 y 向上、主屏左下为原点，先翻一次。然后从最前面那块往下问：
// 落在我们窗口上、而它此刻正 ignoresMouseEvents，就往下一层继续问——穿透态下
// 系统本来就会把点击交给底下那家，问到的第一个「会收点击的窗口」才是真落点。
int sb_hit_test(double x, double y) {
    @autoreleasepool {
        NSScreen* primary = NSScreen.screens.firstObject;
        if (!primary) return -1;
        const NSRect pf = primary.frame;
        const NSPoint p = NSMakePoint(x, pf.origin.y + pf.size.height - y);
        NSInteger below = 0;
        for (int step = 0; step < 64; step++) {
            const NSInteger num = [NSWindow windowNumberAtPoint:p belowWindowWithWindowNumber:below];
            if (num <= 0) return 0;   // 底下没有窗口了（桌面）
            NSWindow* mine = nil;
            for (NSWindow* w in [NSApp windows])
                if (w.windowNumber == num) { mine = w; break; }
            if (mine && mine.ignoresMouseEvents) {   // 自己的穿透窗口：跳过它继续往下
                below = num;
                continue;
            }
            return mine ? 1 : 2;
        }
        return -1;
    }
}
