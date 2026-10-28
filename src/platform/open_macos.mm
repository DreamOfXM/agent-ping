// macOS 落点。表逐条抄自 Swift 侧 Sources/AgentPing/Engine.swift 的 openTargets（10-07 逐条核对），
// 那一份是带着各家实测注释的真源：qoder 有托盘没对外 URL、claude/codex 本机没有 .app、
// ZCode 对外只到工作区级、WorkBuddy 认领 workbuddy://chat/<uuid>。
//
// 还没从 Swift 搬过来的三级（没搬过来之前不许挂上——挂一个必然失败的落点比没有落点更坏，原注释同此）：
//   · 按名字点 Qoder 自己托盘菜单里的那一项（Swift: QoderTray.press）
//   · 在终端里 `qoder -r <id>` / `claude -r <id>` 恢复这一条（Swift: Resume.run）
//   · 替他把 ⌘K → ⌘V → 回车 敲进 ZCode 窗口（Swift: KeyDriver.navigate）
// 所以这里的落点比 Swift 粗一档：能唤起对的 App，精确到「哪一条对话」只有 WorkBuddy 做得到。
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
BOOL ActivateOrOpen(NSString* app_path) {
    NSString* bundle = [[NSBundle bundleWithURL:[NSURL fileURLWithPath:app_path]] bundleIdentifier];
    if (bundle.length)
        for (NSRunningApplication* r in [NSWorkspace sharedWorkspace].runningApplications)
            if ([r.bundleIdentifier isEqualToString:bundle])
                return [r activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    return [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:app_path]];
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
        Say(reason, out_len,
            @"需要辅助功能授权：系统设置 → 隐私与安全性 → 辅助功能，勾选 pin_imgui_shell 后再点一次");
        return 1;
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

extern "C" int ap_open_plan(const char* agent_id, const char* session_id, char* reason, int out_len) {
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, @"无处可去：落点表里没有这一路采集器"); return 1; }
    NSString* deep = DeepURL(t, session_id);
    if (deep) { Say(reason, out_len, [NSString stringWithFormat:@"让 App 自己开到这条对话：开 %@", deep]); return 0; }
    NSString* app = t->app_rel[0] ? @(t->app_rel) : nil;
    if (Exists(app)) { Say(reason, out_len, [NSString stringWithFormat:@"打开 App：%@", app]); return 0; }
    NSString* log = Expand(t->log_rel);
    if (Exists(log)) { Say(reason, out_len, [NSString stringWithFormat:@"在文件管理器里打开：%@", log]); return 0; }
    Say(reason, out_len, @"三条退路全落空");
    return 1;
}

extern "C" int ap_open(const char* agent_id, const char* session_id, char* reason, int out_len) {
    return ap_open2(agent_id, session_id, nullptr, reason, out_len);
}

extern "C" int ap_open2(const char* agent_id, const char* session_id, const char* cwd,
                        char* reason, int out_len) {
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
        return 0;
    }
    // ③ 前两条都不成才开记录目录。
    if (Exists(log)) {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:log]]];
        Say(reason, out_len, [tail stringByAppendingString:[NSString stringWithFormat:@"已在文件管理器里打开：%@", log]]);
        return 0;
    }
    Say(reason, out_len, [tail stringByAppendingString:@"三条退路全落空"]);
    return 1;
}
