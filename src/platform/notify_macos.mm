// macOS 提醒层：横幅走 UserNotifications，出声走 NSSound / afplay。
// core 只认 notify.h 的 C 接口。判据口径逐条对齐 Swift 侧 Engine.swift:324-419。
#include "notify.h"
#include "open.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

static ap_notify_click_fn g_click = nullptr;
static void* g_click_ud = nullptr;
/// 一次事件可能连着要响四下。2026-10-07 在 bundle 内实测：只挂一个实例时第一下 play()=true、
/// 后三下全 false（同一个正在播的 NSSound 不接第二次 play()），于是「命中 4」只响 1 下，
/// 另外三条照样进账本 —— 静默丢掉三条「该吵你」，比不响更糟。
/// Swift 侧每次调用各自新建（Engine.swift:397），这里同规矩：每次新建，播完就松手。
static NSMutableArray<NSSound*>* g_sounds_live = nil;
/// 下一声最早什么时候起（一轮四条不能叠成一声爆音）。CFAbsoluteTime，0＝没排过。
static CFAbsoluteTime g_sound_free_at = 0;

namespace {

/// 这个进程有没有 bundle 身份。裸二进制（build/pin_imgui_shell --dump / --shot）里
/// `UNUserNotificationCenter.current()` 会抛 NSInternalInconsistencyException 直接崩掉整个进程
/// —— Swift 侧 Engine.swift:384 就是为这一条写的 guard，这里同规矩。
bool hasBundle() {
    NSBundle* b = [NSBundle mainBundle];
    NSString* p = b.bundlePath;
    return p != nil && [[p pathExtension] isEqualToString:@"app"] && b.bundleIdentifier.length > 0;
}

NSString* glassSoundPath() {
    static NSString* cached = nil;
    if (cached) return cached;
    NSArray* roots = @[ [NSHomeDirectory() stringByAppendingPathComponent:@"/Library/Sounds"],
                        @"/Library/Sounds", @"/System/Library/Sounds" ];
    for (NSString* root in roots) {
        for (NSString* name in @[ @"Glass.aiff", @"Glass.wav", @"Ping.aiff", @"Submarine.aiff" ]) {
            NSString* f = [root stringByAppendingPathComponent:name];
            if ([[NSFileManager defaultManager] fileExistsAtPath:f]) { cached = f; return cached; }
        }
    }
    return nil;
}

/// 同步出声，只在裸二进制取证时用：进程发完就退，异步的 NSSound 会被退出截断，
/// 听起来就是「没响」—— 那是最坏的失效方式。返回 afplay 的真实退出码，-1 表示没起来。
int playSync(NSString* path) {
    if (!path) return -1;
    NSTask* t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:@"/usr/bin/afplay"];
    t.arguments = @[ @"-t", @"3", path ];
    @try {
        [t launch];
        [t waitUntilExit];
    } @catch (NSException* e) { return -1; }
    return (int)t.terminationStatus;
}

/// 出声。常驻 App 用 NSSound（异步）：afplay 要等整段放完才返回，一次事件就把主循环冻半秒以上。
/// 两条路都要把结果写成机器能读的判据 —— Swift 侧注释说得对：play() 返回 false 是静默失败。
/// 一轮四条时后三条要排在前面那条放完之后：Swift 侧四条同刻起，实测同一刻的四声 Glass
/// 听感就是一坨爆音，分不出「响了几条」—— 而屏幕上写的是四条。
void copyReason(char* reason, int out_len, const std::string& s);   // 定义在本文件下方
bool playFallback(std::string* via);                                // 同上

extern "C" int ap_sound_test(char* reason, int out_len) {
    if (!NSApp) { copyReason(reason, out_len, "AppKit 未就绪"); return 1; }
    std::string via;
    const bool ok = playFallback(&via);
    copyReason(reason, out_len, "测试音 " + std::string(ok ? "已出声" : "没出成") + "（" + via + "）");
    return ok ? 0 : 1;
}

bool playFallback(std::string* via) {
    NSString* p = glassSoundPath();
    if (NSApp) {
        NSSound* s = p ? [[NSSound alloc] initWithContentsOfFile:p byReference:YES]
                       : [NSSound soundNamed:@"Glass"];
        if (!s) { *via = "AppKit 取不到 Glass，系统 Sounds 目录里也没有"; return false; }
        if (!g_sounds_live) g_sounds_live = [NSMutableArray array];
        for (NSSound* old in [g_sounds_live copy]) if (!old.isPlaying) [g_sounds_live removeObject:old];
        NSString* name = p.lastPathComponent ?: @"Glass(按名)";
        const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        const CFAbsoluteTime start = g_sound_free_at > now ? g_sound_free_at : now;
        g_sound_free_at = start + (s.duration > 0.05 ? s.duration : 1.0);
        const double delay = (start - now) * 1000.0;
        if (delay < 1.0) {   // 前一条早已放完：当场起
            BOOL ok = [s play];
            if (ok) [g_sounds_live addObject:s];
            *via = [NSString stringWithFormat:@"%@ play()=%@ isPlaying=%@ 在场 %lu 条", name,
                    ok ? @"true" : @"false", s.isPlaying ? @"true" : @"false",
                    (unsigned long)g_sounds_live.count].UTF8String;
            return ok == YES;
        }
        // 排到队尾：当场不能断言响没响，真结果由 main 上的回执打出来。
        *via = [NSString stringWithFormat:@"%@ 排入队尾 +%dms（在场 %lu 条）", name,
                (int)lround(delay), (unsigned long)g_sounds_live.count].UTF8String;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{
            BOOL ok = [s play];
            if (ok) [g_sounds_live addObject:s];
            fprintf(stderr, "[提醒] 队尾出声 +%dms %s play()=%d isPlaying=%d\n",
                    (int)lround(delay), name.UTF8String, ok, s.isPlaying);
            fflush(stderr);
        });
        return true;
    }
    if (!p) { *via = "系统 Sounds 目录里找不到 Glass/Ping"; return false; }
    int rc = playSync(p);
    *via = [NSString stringWithFormat:@"afplay 退出码 %d（%@）", rc, p.lastPathComponent].UTF8String;
    return rc == 0;
}

void copyReason(char* reason, int out_len, const std::string& s) {
    if (!reason || out_len <= 0) return;
    strncpy(reason, s.c_str(), (size_t)out_len - 1);
    reason[out_len - 1] = 0;
}

/// 授权档位 → 一句人话。横幅能不能弹是这一档决定的，不能只报「成功/失败」。
NSString* authName(UNAuthorizationStatus st) {
    switch (st) {
    case UNAuthorizationStatusAuthorized:    return @"已授权";
    case UNAuthorizationStatusProvisional:   return @"临时授权（只进通知中心，不弹出）";
    case UNAuthorizationStatusDenied:        return @"系统里被关着（系统设置→通知→Agent Ping）";
    case UNAuthorizationStatusNotDetermined: return @"未申请过";
    default:                                 return @"未知档位";
    }
}

/// 拿一次真实通道状态。CLI 取证等 3s（要当场给结论）；GUI 只等 300ms ——
/// 主线程一挂就是整块面板停帧，而「系统没回」通常是它正在等人回答授权弹窗，
/// 这时候该做的是本轮不发、下轮再试（返回 -1），不是把界面冻住。
NSInteger channelReady(std::string* why) {
    if (!hasBundle()) {
        if (why) *why = "裸二进制没有 bundle 身份，横幅通道天生不存在（.app 里跑才有）";
        return 0;
    }
    const int64_t budget = NSApp ? 300 : 3000;
    const int64_t ns = budget * (int64_t)NSEC_PER_MSEC;
    UNUserNotificationCenter* c = [UNUserNotificationCenter currentNotificationCenter];
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block NSString* desc = nil;
    [c requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                     completionHandler:^(BOOL ok, NSError* _Nullable err) {
        if (err) desc = err.localizedDescription;
        else if (!ok) desc = @"requestAuthorization 回了 NO";
        dispatch_semaphore_signal(sem);
    }];
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, ns)) != 0) {
        if (why) { char b[64]; snprintf(b, sizeof b, "%lld", (long long)budget);
                   *why = "授权问询 " + std::string(b) + "ms 没回（系统多半正在等你回答授权弹窗）"; }
        return -1;
    }
    __block UNAuthorizationStatus st = UNAuthorizationStatusNotDetermined;
    [c getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* s) {
        st = s.authorizationStatus;
        dispatch_semaphore_signal(sem);
    }];
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, ns)) != 0) {
        if (why) *why = "读通道状态超时没回";
        return -1;
    }
    BOOL canBanner = (st == UNAuthorizationStatusAuthorized ||
                      st == UNAuthorizationStatusProvisional);
    if (!canBanner && why) {
        std::string s = "授权档位＝";
        NSString* m = desc ? [NSString stringWithFormat:@"%@（%@）", authName(st), desc]
                           : authName(st);
        s += [m UTF8String];
        *why = s;
    }
    return canBanner ? 1 : 0;
}

}  // namespace

@interface APNotifyDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation APNotifyDelegate

// 面板是我们自己的窗口，App 常常就在前台；不实现这条的话，
// 前台时系统直接把横幅吞掉——表现为「弹不出来」，而判据其实是走到了的。
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)n
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion {
    completion(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}

// 点横幅要能回到「那一个」会话：身份是 post() 塞进 userInfo 的两个键。
// 落点失败必须写出来，不许静默报成功（ap_open 的 reason 就是那一句话）。
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       didReceiveNotificationResponse:(UNNotificationResponse*)resp
                withCompletionHandler:(void (^)(void))completion {
    NSDictionary* ui = resp.notification.request.content.userInfo;
    NSString* agent = ui[@"agent"];
    NSString* session = ui[@"session"];
    if (![agent isKindOfClass:[NSString class]] || agent.length == 0) {
        fprintf(stderr, "[横幅点击] userInfo 里没有 agent 键，不知道要打开谁\n");
        fflush(stderr);
        if (completion) completion();
        return;
    }
    const char* sid = [session isKindOfClass:[NSString class]] ? session.UTF8String : "";
    if (g_click) {
        g_click(agent.UTF8String, sid ? sid : "", g_click_ud);
    } else {
        char why[256] = {0};
        int rc = ap_open(agent.UTF8String, sid ? sid : "", why, (int)sizeof why);
        fprintf(stderr, "[横幅点击] 落点 rc=%d %s\n", rc, why);
        fflush(stderr);
    }
    if (completion) completion();
}

@end

extern "C" void ap_notify_on_click(ap_notify_click_fn fn, void* ud) {
    g_click = fn;
    g_click_ud = ud;
    NSBundle* b = [NSBundle mainBundle];
    BOOL bundled = b.bundlePath != nil && [[b.bundlePath pathExtension] isEqualToString:@"app"];
    if (bundled) {
        // delegate 是 weak 引用，所以这个实例必须挂着；用 static 让它活到进程结束。
        static APNotifyDelegate* delegate = nil;
        if (!delegate) delegate = [APNotifyDelegate new];
        [UNUserNotificationCenter currentNotificationCenter].delegate = delegate;
    }
    fprintf(stderr, "[提醒] bundle=%d delegate=%s\n", bundled ? 1 : 0,
            bundled ? "已挂" : "无 bundle 身份，不挂（裸二进制里 UN 会抛异常）");
    fflush(stderr);
}

extern "C" int ap_app_frontmost(const char* bundle_id) {
    if (!bundle_id || !*bundle_id) return 0;
    NSString* want = [NSString stringWithUTF8String:bundle_id];
    NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    NSString* bid = front.bundleIdentifier;
    return (bid && [bid isEqualToString:want]) ? 1 : 0;
}

extern "C" int ap_notify_channel(char* reason, int out_len) {
    std::string why;
    NSInteger st = channelReady(&why);
    copyReason(reason, out_len, st == 1 ? "横幅通道可用" : why);
    return st == 1 ? 0 : (st == 0 ? 1 : 2);
}

extern "C" int ap_notify_post(const char* title, const char* body,
                              const char* agent_id, const char* session_id,
                              int want_sound, char* reason, int out_len) {
    std::string why;
    NSInteger st = channelReady(&why);
    if (st == 1) {
        UNUserNotificationCenter* c = [UNUserNotificationCenter currentNotificationCenter];
        UNMutableNotificationContent* n = [[UNMutableNotificationContent alloc] init];
        n.title = [NSString stringWithUTF8String:title ? title : ""] ?: @"";
        n.body = [NSString stringWithUTF8String:body ? body : ""] ?: @"";
        // 设置里提示音关着，横幅就不该带声音。Swift 侧是无条件 .default，
        // 于是「关掉提示音」只压住了兜底出声、横幅照样响 —— 这里按屏幕上那句承诺改。
        if (want_sound) n.sound = [UNNotificationSound defaultSound];
        if (agent_id && *agent_id) {
            NSMutableDictionary* ui = [NSMutableDictionary dictionary];
            ui[@"agent"] = [NSString stringWithUTF8String:agent_id];
            if (session_id) ui[@"session"] = [NSString stringWithUTF8String:session_id];
            n.userInfo = ui;
        }
        NSString* ident = [NSString stringWithFormat:@"%u-%s", (unsigned)arc4random(),
                           session_id && *session_id ? session_id : "alert"];
        UNNotificationRequest* req = [UNNotificationRequest requestWithIdentifier:ident
                                                                          content:n trigger:nil];
        __block NSString* errStr = nil;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [c addNotificationRequest:req withCompletionHandler:^(NSError* _Nullable err) {
            if (err) errStr = err.localizedDescription;
            dispatch_semaphore_signal(sem);
        }];
        const int64_t budget = NSApp ? 300 : 3000;
        BOOL returned = (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                                                    budget * (int64_t)NSEC_PER_MSEC)) == 0);
        if (returned && !errStr) {
            copyReason(reason, out_len, "横幅已投递");
            return 0;
        }
        if (!returned) {
            // 没回不等于没响：请求已经交出去了，只是系统没给回执。
            // 记 3 让调用方下一轮重试同一批 —— 比悄悄丢掉整条承诺安全。
            copyReason(reason, out_len, "横幅投递没在时限内回执，本轮不算发出");
            return 3;
        }
        copyReason(reason, out_len, std::string("横幅投递失败：") +
                   (errStr ? errStr.UTF8String : "空错误"));
        // 到这里不再补出声：投递被拒通常是「系统刚把通道关了」，
        // 兜底出声要留给「压根没有通道」那一档，否则一次事件响两遍。
        return 2;
    }
    if (st < 0) {
        copyReason(reason, out_len, "通道状态没问出来（" + why + "）；本轮一条都不发、不出声");
        return 3;   // 临时态：下轮重扫再试，不记进账本
    }
    // 横幅通道不通 → 按设置兜底出声。
    if (want_sound) {
        std::string via;
        bool played = playFallback(&via);
        copyReason(reason, out_len, "横幅不可用（" + why + "）；兜底出声：" +
                   (played ? "已响 " : "失败 ") + via);
        return played ? 1 : 2;
    }
    copyReason(reason, out_len, "横幅不可用（" + why + "）；提示音在设置里关着，本轮不出声");
    return 2;
}
