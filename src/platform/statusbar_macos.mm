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
                        quit:(NSString*)quitLabel
                       click:(sb_click_fn)click
                        user:(void*)ud;
- (void)setLabel:(NSString*)text;
@end

@implementation SBController

- (instancetype)initWithIcon:(NSString*)path
                     tooltip:(NSString*)tip
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

    // 不给 _item.menu：一给，左键也被系统吃掉去弹菜单，click 回调就再也不触发。
    _menu = [[NSMenu alloc] init];
    NSMenuItem* toggle = [[NSMenuItem alloc] initWithTitle:@"显示／隐藏面板"
                                                    action:@selector(showHide:)
                                             keyEquivalent:@""];
    toggle.target = self;
    [_menu addItem:toggle];
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
    _item.button.title = text.length ? [@" " stringByAppendingString:text] : @"  ";
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

int sb_show(const char* icon_path, const char* tooltip, const char* quit_label,
            sb_click_fn on_click, void* ud) {
    @autoreleasepool {
        // 挂上状态项＝这个 App 的入口在菜单栏，不在 Dock。GLFW 建窗口时把进程注册成了
        // Regular（实测 lsappinfo type="Foreground" 且启动后抢在前台），而 Info.plist 里的
        // LSUIElement 只在走 LaunchServices（open / 双击）时才生效——从命令行直接 exec 二进制
        // 会被忽略。所以这里当场翻成 Accessory：无 Dock 图标、不抢前台，Swift 侧同一档。
        if (NSApp) [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSString* path = icon_path ? [NSString stringWithUTF8String:icon_path] : nil;
        NSString* tip = tooltip ? [NSString stringWithUTF8String:tooltip] : @"";
        NSString* quit = quit_label ? [NSString stringWithUTF8String:quit_label] : @"";
        g_ctl = [[SBController alloc] initWithIcon:path tooltip:tip quit:quit click:on_click user:ud];
        if (!g_ctl) return 1;
        [g_ctl setLabel:@""];
        fprintf(stderr, "[状态项] 策略＝%d（0 常驻 Dock／1 只在菜单栏）\n",
                NSApp ? (int)NSApp.activationPolicy : -1);
        fflush(stderr);
        return 0;
    }
}

void sb_set_label(const char* utf8) {
    if (!g_ctl) return;
    @autoreleasepool {
        [g_ctl setLabel:utf8 ? [NSString stringWithUTF8String:utf8] : @""];
    }
}

void sb_pump(void) {}

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
