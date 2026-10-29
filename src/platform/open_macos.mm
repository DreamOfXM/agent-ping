// macOS 落点。表逐条抄自 Swift 侧 Sources/AgentPing/Engine.swift 的 openTargets（10-07 逐条核对），
// 那一份是带着各家实测注释的真源：qoder 有托盘没对外 URL、claude/codex 本机没有 .app、
// ZCode 对外只到工作区级、WorkBuddy 认领 workbuddy://chat/<uuid>。
//
// 阶梯只有这三层（deep link → 唤起 App → 打开记录目录），加上会话级的两把钥匙：
//   · Qoder：按名字点它自己托盘里的会话条目（ap_qoder_tray，见下方实测注释）
//   · ZCode：激活后替他把标题敲进 ⌘K 检索（ap_zcode_navigate）
// 两把都要辅助功能授权（ap_ax_trusted 回答现在有没有），拿不到就退回上面三层——
// 但退到哪儿必须由调用方把话说清（ap_open3 的 layer），不许把「打开了文件夹」说成「跳到了会话」。
// 终端恢复（Swift: Resume.run）没搬：用户用客户端就跳客户端，把会话塞进另一个终端进程是帮倒忙。
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>   // AXIsProcessTrusted / CGEvent（⌘K 驱动）
#import <Carbon/Carbon.h>                            // kVK_ANSI_*（⌘K 驱动）
#include <cstring>
#include <string>

#include "open.h"

namespace {

struct Target {
    const char* app_rel;      // 空串＝本机没有 .app（纯 CLI）
    const char* log_rel;      // 会话记录目录，兜底落点
    const char* deep_prefix;  // 空串＝这个 App 不认领 deep link
};

// 键＝采集器 id（与 scan.cpp 里 snap.id 同一套：qoder/claude/codex/zcode/workbuddy）
const char* kIds[] = {"qoder", "claude", "codex", "zcode", "workbuddy"};
const Target kTargets[] = {
    {"/Applications/Qoder.app",     ".qoder/projects",   ""},
    {"",                            ".claude/projects",  ""},
    {"",                            ".codex/sessions",   ""},
    {"/Applications/ZCode.app",     ".zcode/cli/db",     ""},
    {"/Applications/WorkBuddy.app", ".workbuddy/projects", "workbuddy://chat/"},
};
const int kN = (int)(sizeof kTargets / sizeof kTargets[0]);

const Target* Find(const char* id) {
    if (!id) return nullptr;
    for (int i = 0; i < kN; i++)
        if (!strcasecmp(kIds[i], id)) return &kTargets[i];
    return nullptr;
}

NSString* Expand(const char* rel) {
    if (!rel || !*rel) return nil;
    return [NSHomeDirectory() stringByAppendingPathComponent:@(rel)];
}

BOOL Exists(NSString* p) {
    return p.length > 0 && [[NSFileManager defaultManager] fileExistsAtPath:p];
}

void Say(char* reason, int n, NSString* s) {
    if (!reason || n <= 0) return;
    const char* u = s ? [s UTF8String] : "";
    strncpy(reason, u ? u : "", (size_t)n - 1);
    reason[n - 1] = 0;
}

// Swift 的 activateApp：已经在跑就直接 activate，省掉 LaunchServices 的 reopen 一程
// （这一条正是他 10-06 点名「点了半天没反应」的那一级）。
// 10-29 补第二手：activateWithOptions: 自 macOS 14 起废弃，在这台 26.x 上会返回 NO，
// 而旧写法把它的返回值直接当整个 ② 的成败——于是一跳就掉到 ③「打开记录目录」，
// 也就是用户 10-29 那张 Finder 截图。现在唤起不成再走一次 LaunchServices 的 open
// （在跑就是 bring to front），两条都不成才算这一级落空。
BOOL ActivateOrOpen(NSString* app_path) {
    NSURL* url = [NSURL fileURLWithPath:app_path];
    NSString* bundle = [[NSBundle bundleWithURL:url] bundleIdentifier];
    if (bundle.length)
        for (NSRunningApplication* r in [NSWorkspace sharedWorkspace].runningApplications)
            if ([r.bundleIdentifier isEqualToString:bundle])
                if ([r activateWithOptions:NSApplicationActivateIgnoringOtherApps]) return YES;
    return [[NSWorkspace sharedWorkspace] openURL:url];
}

NSString* DeepURL(const Target* t, const char* session_id) {
    if (!t->deep_prefix[0] || !session_id || !*session_id) return nil;
    return [NSString stringWithFormat:@"%s%@", t->deep_prefix, @(session_id)];
}

// ── ZCode 的 ⌘K 驱动（Swift 版 KeyDriver.navigate 的移植，2026-10-28）────────
// ZCode 对外没有会话级 URL（asar 路由表只有 oauth/workspace 两条，workspace/open
// 会新开任务），唯一能定位已有会话的入口是它自己的 Command Center（⌘K）：按标题
// 检索会话、回车即跳。这里替用户敲这一串：激活 → ⌘K → 粘贴标题 → 回车。
// 前提是本进程有辅助功能授权，没有就明说、不瞎敲（挂一个必然失败的落点比没有更坏）。

static void PostKey(CGEventFlags flags, CGKeyCode code) {
    CGEventRef dn = CGEventCreateKeyboardEvent(nullptr, code, true);
    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, code, false);
    CGEventSetFlags(dn, flags);
    CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, dn);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(dn);
    CFRelease(up);
}

/// Command Center 的检索是「词元级」的：查询里的连续段必须与标题词元一致。
/// 词元的切法只认标点/空白——标题「【示例工程】Remote Sync」的索引词元是「示例工程」
/// 「Remote Sync」，把中英连写再拆（示例/工程）就对不上，只剩文件结果（2026-10-28 晚实证）。
/// 每个词元再按文种交界补空格隔开查询侧的粘连即可，这里直接保持原段不拆。限量 8 段。
static NSString* KQueryFromTitle(NSString* title) {
    NSMutableArray<NSString*>* toks = [NSMutableArray array];
    NSMutableString* cur = [NSMutableString string];
    // 只留「文字/数字」：用系统的 alphanumericCharacterSet，别用手写码段——
    // 全角标点（【】（）「」等）码位 ≥ 0x80，按码段判会被当成词元内字符，
    // 等于标点没剥、粘出去还是原文（2026-10-28 晚实测踩过）。
    static NSCharacterSet* punct;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        punct = [[NSCharacterSet alphanumericCharacterSet] invertedSet];
    });
    for (NSString* raw in [title componentsSeparatedByCharactersInSet:punct]) {
        if (raw.length == 0) continue;
        [toks addObject:raw];
        if (toks.count >= 8) break;
    }
    return [toks componentsJoinedByString:@" "];
}

extern "C" int ap_zcode_navigate(const char* title, char* reason, int out_len) {
    if (!title || !*title) { Say(reason, out_len, @"⌘K 驱动缺标题，退回普通落点"); return 1; }
    {   // 防抖：连点多轮时每轮都是一次完整的 ⌘K 周期，下拉没关时粘贴会越叠越厚
        static double lastNavAt = 0;
        const double now = [NSProcessInfo processInfo].systemUptime;
        if (now - lastNavAt < 4.0) {
            Say(reason, out_len, @"⌘K 驱动 4 秒内重复触发，忽略本轮");
            return 1;
        }
        lastNavAt = now;
    }
    if (!AXIsProcessTrusted()) {
        // 2＝挡在授权这一层（与 ap_qoder_tray 同一码），面板据此叫他开开关，
        // 而不是把「没权限」混进「App 里没这一条」。
        Say(reason, out_len, @"未授权辅助功能，敲不进它的 ⌘K 检索");
        return 2;
    }
    if (!ActivateOrOpen(@"/Applications/ZCode.app")) {
        Say(reason, out_len, @"ZCode 没唤起来，⌘K 驱动放弃");
        return 1;
    }
    usleep(600 * 1000);        // 等窗口成 key（Swift 同款节奏）
    // 剪贴板先存后还：粘给面板的内容不该留在用户的剪贴板里
    NSString* saved = nil;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* cur = [pb stringForType:NSPasteboardTypeString];
    if (cur.length > 0) saved = cur;
    [pb clearContents];
    [pb setString:KQueryFromTitle(@(title)) forType:NSPasteboardTypeString];
    usleep(60 * 1000);
    PostKey(0, (CGKeyCode)kVK_Escape);          // 上一次可能还开着的下拉先关掉
    usleep(250 * 1000);
    PostKey(kCGEventFlagMaskCommand, (CGKeyCode)kVK_ANSI_K);
    usleep(600 * 1000);
    PostKey(kCGEventFlagMaskCommand, (CGKeyCode)kVK_ANSI_A);   // 清掉框里可能残留的旧查询
    usleep(150 * 1000);
    PostKey(kCGEventFlagMaskCommand, (CGKeyCode)kVK_ANSI_V);
    // 检索是异步 IPC（useGlobalTaskList → Controller.list），550ms 时结果还没回来，
    // 回车就扑空（实测「暂无相关结果」）。人工对照 2.5s 稳定出结果，按 2.6s 留余量。
    usleep(2600 * 1000);
    PostKey(0, (CGKeyCode)kVK_Return);
    usleep(150 * 1000);
    if (saved) { [pb clearContents]; [pb setString:saved forType:NSPasteboardTypeString]; }
    Say(reason, out_len, [NSString stringWithFormat:@"已把「%@」敲进 ZCode 的 ⌘K 检索并回车", @(title)]);
    return 0;
}

}   // namespace

// ── Qoder 会话级落点：读它自己托盘里的会话条目，按名字点中那一条 ────────────
// 2026-10-29 在本机 Qoder 0.4.11 上实测（探针 /tmp/apq/probe2.mm，只读＋定点 press）：
//   · qoder:// 只认领 settings/<节>、extensions/plugin/<id>、reference|mention/<…>，
//     没有会话级路由 —— 所以「打开 URL 定位会话」这条路在这台机器上根本不存在。
//   · AXExtrasMenuBar → AXMenuBar → AXMenuBarItem → AXMenu 的条目**不用点开菜单就能读**，
//     条目形状是「会话标题，工作区名」；对未展开的菜单项做 AXPress 也真生效
//     （err=0，菜单随即重排）。所以这里不再先 press 菜单栏那颗：省掉一次可见的菜单闪动，
//     也省掉旧版 400ms 的等待。读不到条目时才按老路 press 一次再读（兜别的版本行为）。
//   · 四个分组（运行中／未读／已置顶／最近）各只露 3 条 + 一颗「更多」，一共 12 条会话；
//     按「更多」不会在本菜单里追加条目（实测条目数 26→26），所以第 13 条往后的会话
//     在这一层没有落点 —— 这一点必须写进 reason，不许退化成「打开文件夹」冒充成功。
//   · 需要辅助功能授权：没有授权时返回 2，调用方据此叫他去系统设置，不许假装跳到了。
static const CFStringRef kAXKids = CFSTR("AXChildren");
static const CFStringRef kAXMenuAttr = CFSTR("AXMenu");

/// 「会话标题，工作区名」里取会话标题那一截：Qoder 用最后一个中文逗号拼工作区。
/// 没有逗号＝不是会话条目（分组头、「更多」、「新建聊天」都是这种），返回 nil 让它们不参与匹配。
static NSString* TraySessionPart(NSString* full) {
    if (!full.length) return nil;
    NSRange r = [full rangeOfString:@"，" options:NSBackwardsSearch];
    if (r.location == NSNotFound || r.location == 0) return nil;
    return [full substringToIndex:r.location];
}

/// 比较用的归一化：去空白与尾部省略号，转小写。托盘标题里「多 Agent」这种中英夹空格
/// 是常态，采集器存的标题也可能带省略号，不归一就会「明明同一条却判不成」。
static NSString* NormForMatch(NSString* s) {
    NSMutableString* out = [NSMutableString string];
    for (NSUInteger i = 0; i < s.length; i++) {
        unichar c = [s characterAtIndex:i];
        if (c == L'…' || c == 0x2026) continue;
        if ([[NSCharacterSet whitespaceAndNewlineCharacterSet] characterIsMember:c]) continue;
        [out appendString:[[NSString stringWithCharacters:&c length:1] lowercaseString]];
    }
    return out;
}

/// 一条会话标题与一个托盘条目的匹配分：0＝不相干，越大越像。
/// 两头都可能被截断（采集器把长标题裁到 40 字，Qoder 托盘也会截），所以前缀要双向判。
static int TrayScore(NSString* needle, NSString* itemFull) {
    NSString* part = TraySessionPart(itemFull);
    if (!part) return 0;
    NSString* a = NormForMatch(needle);
    NSString* b = NormForMatch(part);
    NSString* c = NormForMatch(itemFull);
    if (!a.length || !b.length) return 0;
    if ([a isEqualToString:b]) return 100;
    const NSUInteger shorter = MIN(a.length, b.length);
    if (shorter >= 8 && ([a hasPrefix:b] || [b hasPrefix:a])) return 80;
    if ([c containsString:needle] || [needle containsString:part]) return 60;
    if (shorter >= 8 && ([c containsString:b] || [a containsString:c])) return 50;
    return 0;
}

static pid_t QoderPid() {
    for (NSRunningApplication* r in [NSWorkspace sharedWorkspace].runningApplications) {
        NSString* bid = r.bundleIdentifier ?: @"";
        // 主进程才是托盘的持有者：com.qoder.app.helper 这类同前缀的辅助进程不算。
        if ([bid containsString:@".qoder."] && ![bid containsString:@"helper"]) return r.processIdentifier;
    }
    return -1;
}

/// 托盘菜单本体：AXExtrasMenuBar → 第一颗菜单栏项 → 它的 AXMenu 子项。
/// 数组释放后里面的元素会被一起释放，所以返回前 retain 一份。
static AXUIElementRef QoderTrayMenu(AXUIElementRef app) {
    CFTypeRef extras = nil;
    if (AXUIElementCopyAttributeValue(app, CFSTR("AXExtrasMenuBar"), &extras) != kAXErrorSuccess || !extras)
        return nil;
    AXUIElementRef out = nil;
    CFArrayRef bars = nil;
    if (AXUIElementCopyAttributeValue((AXUIElementRef)extras, kAXKids, (CFTypeRef*)&bars) == kAXErrorSuccess && bars) {
        for (CFIndex i = 0; i < CFArrayGetCount(bars) && !out; i++) {
            AXUIElementRef bar = (AXUIElementRef)CFArrayGetValueAtIndex(bars, i);
            CFTypeRef menu = nil;
            if (AXUIElementCopyAttributeValue(bar, kAXMenuAttr, &menu) == kAXErrorSuccess && menu)
                out = (AXUIElementRef)CFRetain(menu);
            if (menu) CFRelease(menu);
            if (!out) {   // 有的版本把 AXMenu 挂在 children 里而不是 AXMenu 属性上
                CFArrayRef kids = nil;
                if (AXUIElementCopyAttributeValue(bar, kAXKids, (CFTypeRef*)&kids) == kAXErrorSuccess && kids
                    && CFArrayGetCount(kids) > 0)
                    out = (AXUIElementRef)CFRetain(CFArrayGetValueAtIndex(kids, 0));
                if (kids) CFRelease(kids);
            }
        }
        CFRelease(bars);
    }
    CFRelease(extras);
    return out;
}

/// 在菜单里找 needle 对应的那条会话，返回得分最高的条目（不 press）。找不到返回 nil，
/// seen 里回填「这一轮读到几条会话」，写 reason 时要说清它为什么没中。
static AXUIElementRef TrayFindSession(AXUIElementRef menu, NSString* needle, int* seen) {
    CFArrayRef items = nil;
    if (!menu || AXUIElementCopyAttributeValue(menu, kAXKids, (CFTypeRef*)&items) != kAXErrorSuccess || !items)
        return nil;
    AXUIElementRef best = nil;
    int bestScore = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(items); i++) {
        AXUIElementRef it = (AXUIElementRef)CFArrayGetValueAtIndex(items, i);
        CFTypeRef t = nil;
        AXUIElementCopyAttributeValue(it, kAXTitleAttribute, &t);
        NSString* title = t ? (__bridge NSString*)t : nil;
        if (t) CFRelease(t);
        const int sc = TrayScore(needle, title);
        if (TraySessionPart(title)) (*seen)++;
        // items 释放会把里面的元素一起放掉，所以选中时要自己 retain 一份。
        if (sc > bestScore) { bestScore = sc; best = (AXUIElementRef)CFRetain(it); }
    }
    if (items) CFRelease(items);
    if (bestScore < 50 && best) { CFRelease(best); return nil; }
    return best;
}

extern "C" int ap_qoder_tray(const char* title, char* reason, int out_len) {
    if (!title || !*title) { Say(reason, out_len, @"托盘定位缺标题"); return 1; }
    if (!AXIsProcessTrusted()) {
        Say(reason, out_len, @"未授权辅助功能，点不到它托盘里的会话");
        return 2;
    }
    const pid_t pid = QoderPid();
    if (pid < 0) { Say(reason, out_len, @"Qoder 没在跑"); return 3; }
    AXUIElementRef app = AXUIElementCreateApplication(pid);
    AXUIElementRef menu = QoderTrayMenu(app);
    int seen = 0;
    AXUIElementRef target = menu ? TrayFindSession(menu, @(title), &seen) : nil;
    if (menu) CFRelease(menu);

    // 一条都没读到时才按老路：press 菜单栏那颗把菜单真开起来，再读一次。
    if (!target && seen == 0) {
        CFTypeRef extras = nil;
        if (AXUIElementCopyAttributeValue(app, CFSTR("AXExtrasMenuBar"), &extras) == kAXErrorSuccess && extras) {
            CFArrayRef bars = nil;
            if (AXUIElementCopyAttributeValue((AXUIElementRef)extras, kAXKids, (CFTypeRef*)&bars) == kAXErrorSuccess
                && bars && CFArrayGetCount(bars) > 0) {
                AXUIElementPerformAction((AXUIElementRef)CFArrayGetValueAtIndex(bars, 0), kAXPressAction);
                usleep(500 * 1000);
                AXUIElementRef m2 = QoderTrayMenu(app);
                target = m2 ? TrayFindSession(m2, @(title), &seen) : nil;
                if (m2) CFRelease(m2);
                if (!target) {   // 没点中就收起来，别把菜单留在他屏幕上
                    CGEventRef esc = CGEventCreateKeyboardEvent(nil, 53, true);
                    CGEventPost(kCGHIDEventTap, esc);
                    CFRelease(esc);
                }
                CFRelease(bars);
            }
            CFRelease(extras);
        }
    }

    int rc = 1;
    if (target) {
        const AXError e = AXUIElementPerformAction(target, kAXPressAction);
        if (e == kAXErrorSuccess) {
            Say(reason, out_len, [NSString stringWithFormat:@"已在 Qoder 托盘里点中「%@」", @(title)]);
            rc = 0;
        } else {
            Say(reason, out_len, [NSString stringWithFormat:@"托盘里找到了「%@」但按下去没应答（AX %d）",
                                  @(title), (int)e]);
        }
    } else {
        Say(reason, out_len, [NSString stringWithFormat:
                              @"托盘这一轮只列了 %d 条会话（四个分组各 3 条），没有「%@」这一条",
                              seen, @(title)]);
    }
    CFRelease(app);
    if (target) CFRelease(target);
    return rc;
}

// ── 辅助功能授权：面板要能回答「这一档现在有没有」，并一步跳到系统设置那页 ──────
// prompt!=0 时未授权就发起系统那次问询（macOS 会弹「XXX 想控制这台电脑」并把本 App
// 列进辅助功能表），只发起一次，不循环骚扰。
extern "C" int ap_ax_trusted(int prompt, char* reason, int out_len) {
    const BOOL trusted = AXIsProcessTrusted();
    if (!trusted && prompt) {
        const void* keys[] = { kAXTrustedCheckOptionPrompt };
        const void* vals[] = { kCFBooleanTrue };
        CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
        AXIsProcessTrustedWithOptions(opts);
        CFRelease(opts);
    }
    Say(reason, out_len, trusted ? @"已授权：可以点进别的 App" : @"未授权：点不到别的 App 里的会话");
    return trusted ? 0 : 1;
}

extern "C" int ap_open_privacy_pane(char* reason, int out_len) {
    NSURL* url = [NSURL URLWithString:
                  @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
    if (url && [[NSWorkspace sharedWorkspace] openURL:url]) {
        Say(reason, out_len, @"已打开系统设置的「隐私与安全性 → 辅助功能」");
        return 0;
    }
    Say(reason, out_len, @"系统设置没能打开，手动进：系统设置 → 隐私与安全性 → 辅助功能");
    return 1;
}

extern "C" int ap_has_client(const char* agent_id) {
    const Target* t = Find(agent_id);
    return t && t->app_rel[0] ? 1 : 0;
}

extern "C" int ap_open_plan(const char* agent_id, const char* session_id, char* reason, int out_len) {
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, @"无处可去：落点表里没有这一路采集器"); return 1; }
    NSString* deep = DeepURL(t, session_id);
    if (deep) { Say(reason, out_len, [NSString stringWithFormat:@"让 App 自己开到这条对话：开 %@", deep]); return 0; }
    NSString* app = t->app_rel[0] ? @(t->app_rel) : nil;
    if (Exists(app)) { Say(reason, out_len, [NSString stringWithFormat:@"打开 App：%@", app]); return 0; }
    // 表里写了客户端的路：本机没有那个 .app 就是落空，不拿记录目录冒充（与 ap_open3 同一判据）。
    if (t->app_rel[0]) { Say(reason, out_len, @"落空：这一路该跳客户端，但本机没有它"); return 1; }
    NSString* log = Expand(t->log_rel);
    if (Exists(log)) { Say(reason, out_len, [NSString stringWithFormat:@"在文件管理器里打开：%@", log]); return 0; }
    Say(reason, out_len, @"三条退路全落空");
    return 1;
}

extern "C" int ap_open(const char* agent_id, const char* session_id, char* reason, int out_len) {
    return ap_open3(agent_id, session_id, nullptr, reason, out_len, nullptr);
}

extern "C" int ap_open2(const char* agent_id, const char* session_id, const char* cwd,
                        char* reason, int out_len) {
    return ap_open3(agent_id, session_id, cwd, reason, out_len, nullptr);
}

extern "C" int ap_open3(const char* agent_id, const char* session_id, const char* cwd,
                        char* reason, int out_len, int* layer) {
    if (layer) *layer = 0;
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, @"无处可去：落点表里没有这一路采集器"); return 1; }
    NSString* app = t->app_rel[0] ? @(t->app_rel) : nil;
    NSString* log = Expand(t->log_rel);
    NSString* tail = @"";

    // ① deep link：唯一不需要辅助功能授权、又能精确到「那一条对话」的入口，排最前。
    NSString* deep = DeepURL(t, session_id);
    if (deep) {
        NSURL* url = [NSURL URLWithString:deep];
        if (url && [[NSWorkspace sharedWorkspace] openURL:url]) {
            Say(reason, out_len, [NSString stringWithFormat:@"已让 App 自己开到这条对话：%@", deep]);
            if (layer) *layer = 1;
            return 0;
        }
        tail = [NSString stringWithFormat:@"deep link 没人接住（%@），退下一级；", deep];
    }
    // ①′ 工作区级 URL：曾接过 zcode://workspace/open?path=…（2026-10-28 实测后撤掉）。
    //   撤掉的原因：ZCode 对外的 URL 只到工作区级（asar 路由表仅 oauth/workspace 两条），
    //   而已有窗口收到 workspace/open 时是「在那个工作区新开一个任务」，不是定位已有会话——
    //   会话级的 resumeTaskId 只存在于 renderer 内部状态，没有外部入口。接它等于替用户新开会话。
    //   实测记录：每次调 URL 都新建一个小窗（CGWindowList 前后对照），用户实测同判。
    //   能精确到会话的三级（托盘点名/终端恢复/⌘K 敲键）在 Swift 版里也都要辅助功能授权，未搬。
    // ② 本机装了它的 .app → 唤起。
    if (Exists(app) && ActivateOrOpen(app)) {
        Say(reason, out_len, [tail stringByAppendingString:[NSString stringWithFormat:@"已唤起：%@", app]]);
        if (layer) *layer = 2;
        return 0;
    }
    // ③ 前两条都不成才考虑开记录目录。
    // 这一级只留给「本机根本没有客户端」的那几路（claude/codex 是纯 CLI）。
    // 装了客户端却没跳进去，就报落空——开一堆文件夹冒充「跳到了会话」是骗人，
    // 用户 10-29 就是被这个误判气到的（他原话：不是应该定位到 qoder 对应的会话吗）。
    if (t->app_rel[0]) {
        Say(reason, out_len, [tail stringByAppendingString:
                              [NSString stringWithFormat:@"落空：客户端在 %@，但这一跳没能把它带到前台",
                               @(t->app_rel)]]);
        return 1;
    }
    if (Exists(log)) {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:log]]];
        Say(reason, out_len, [tail stringByAppendingString:[NSString stringWithFormat:@"已在文件管理器里打开：%@", log]]);
        if (layer) *layer = 3;
        return 0;
    }
    Say(reason, out_len, [tail stringByAppendingString:@"三条退路全落空"]);
    return 1;
}
