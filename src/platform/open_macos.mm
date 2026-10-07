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
