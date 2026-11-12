// macOS 落点。表逐条抄自 Swift 侧 Sources/AgentPing/Engine.swift 的 openTargets（10-07 逐条核对），
// 那一份是带着各家实测注释的真源：qoder 有托盘没对外 URL、claude/codex 本机没有 .app、
// ZCode 对外只到工作区级、WorkBuddy 认领 workbuddy://chat/<uuid>。
//
// 阶梯只有这三层（deep link → 唤起 App → 打开记录目录），加上会话级的两把钥匙：
//   · Qoder：按名字点它自己托盘里的会话条目（ap_qoder_tray，见下方实测注释）
//   · ZCode：读它侧栏的任务行按名字点中，主区标题当裁判（同一把钥匙 ap_zcode_navigate，
//     10-29 从「盲敲 ⌘K＋回车」改成「读完再点」；命令中心那条退路实测 0/10，同日撤掉）
// 两把都要辅助功能授权（ap_ax_trusted 回答现在有没有），拿不到就退回上面三层——
// 但退到哪儿必须由调用方把话说清（ap_open3 的 layer），不许把「打开了文件夹」说成「跳到了会话」。
// 终端恢复（Swift: Resume.run）没搬：用户用客户端就跳客户端，把会话塞进另一个终端进程是帮倒忙。
#import <AppKit/AppKit.h>
#include <unistd.h>
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#import <ApplicationServices/ApplicationServices.h>   // AXIsProcessTrusted / CGEvent（托盘收起时补一发 Escape）
#include <cstring>
#include <vector>
#include <string>
#include <sys/sysctl.h>   // ZCode 进程启动时刻（判「这条比它这次启动还晚」）

#include "open.h"

namespace {

struct Target {
    const char* app_rel;      // 空串＝本机没有 .app（纯 CLI）
    const char* log_rel;      // 会话记录目录，兜底落点
    const char* deep_prefix;  // 空串＝这个 App 不认领 deep link
    const char* cli_fmt;      // 纯 CLI 的会话级 resume 命令（%s＝会话 id）；
                              // 空＝没有（这类落点＝终端新窗口跑命令，见 ap_open3 ⓪）
};

// 键＝采集器 id（与 scan.cpp 里 snap.id 同一套：qoder/claude/codex/zcode/workbuddy/opencode）
const char* kIds[] = {"qoder", "claude", "codex", "zcode", "workbuddy", "opencode", "pi",
                       "qwenworkcn"};
// cli_fmt：纯 CLI 的会话级 resume 命令。只有旗标在本机核实过的才写
//（claude --resume 实测、pi --session 实测；codex 的 resume 旗标没核实，不猜，落 ③）。
const Target kTargets[] = {
    {"/Applications/Qoder.app",     ".qoder/projects",   "", ""},
    {"",                            ".claude/projects",  "", "claude --resume %s"},
    {"",                            ".codex/sessions",   "", ""},
    {"/Applications/ZCode.app",     ".zcode/cli/db",     "", ""},
    {"/Applications/WorkBuddy.app", ".workbuddy/projects", "workbuddy://chat/", ""},
    {"/Applications/OpenCode.app",  ".local/share/opencode", "", ""},
    {"",                            ".pi/agent/sessions", "", "pi --session %s"},
    {"/Applications/QwenWorkCN.app", ".qwenworkcn/projects", "", ""},
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

// ── ZCode 会话级落点：读它侧栏的任务行按名字点中，主区标题当裁判 ─────────────
// 2026-10-29 在本机 ZCode 3.16.3 上实测（探针 /tmp/apq/znav.mm、zgeo.mm，只读＋定点 press）：
//   · zcode:// 只认领 oauth/callback 与 workspace/open?path= 两条（asar 里逐条核过），
//     没有会话级路由。
//   · Chromium 默认不建无障碍树：必须先给 App 置 AXManualAccessibility=YES，否则整棵树
//     只有六层空 AXGroup，什么都读不到 —— 旧 ⌘K 那版从没踩到这一步，所以它只能盲敲。
//   · 侧栏「任务列表」的每一行＝AXList(AXContentList) 下可 AXPress 的 AXGroup，行里最内层
//     的 AXStaticText 就是会话标题（与采集器读的 session.title 同一字段）。实测 23 行，
//     点中后 500ms 内主区 AXSectionHeader 的 AXHeading 就换成这一条 —— 有可读裁判，
//     不必像 ⌘K 那样敲完回车赌它命中。
//   · 命令中心（⌘K）不是第二把钥匙，是假钥匙，10-29 撤：它自己的说明写「搜索并执行
//     当前工作区可用的命令」，作用域是当前工作区。实测两条都判它不能用——
//       ① 10-28 连测 10 次 0 命中、平均 17.1 秒（同期侧栏点法 10/10、约 1.2 秒）；
//       ② 10-29 拿一条侧栏明明列着的标题去搜，框里值确实粘进去了，
//          结果区还是「暂无相关结果」。
//     旧代码在它上面盲敲 ⌘K／⌘A／⌘V 且无条件 return 0，等于把「没跳到」报成「跳到了」。
//   · 侧栏那一列固定只挂 23 行（本机 DB 里有 132 条会话），滚出视口的行会被压成 0 高
//     但标题仍在树里；对末行做 AXScrollToVisible 只重排这些行，行数与标题集合都不变，
//     所以「侧栏没有这一条」就是真没有——按没这一条报，不假装还有下一级。
//   · App 在跑却一个窗口都没有（10-29 探针过程中实测到）：AXWindows 只剩一个空壳，读不到
//     AXWebArea。这一档要把窗口要回来，有两条路，10-29 各实跑过，取第二条：
//       ① `zcode://workspace/open?path=<会话目录>`（旧版选的）：窗口确实会开，但它开的是
//          「用外部 URL 打开这个文件夹」——ZCode 立刻弹一枚模态「是否在 ZCode 中打开此文件夹？
//          （只打开你信任来源的文件夹…）」/「取消｜打开文件夹」（10-29 18:5x 探针 /tmp/apq/ztree_boot.txt
//          逐字读到的）。这枚框拦在界面树前面：不点它，窗口一直停在 260x273，无障碍树永远不建；
//          而替他自己按「打开文件夹」是替他做了一个 ZCode 故意要问的安全决定，不能干。
//       ② 走 LaunchServices 的 reopen（openURL 那个 .app 本身，等价于点 Dock 图标）：
//          实测 3 秒内把原来那一扇以原几何（1241x799 @112,110）恢复出来，不弹任何框、
//          也不新开任务窗；随后按名字点侧栏那一行 0.7 秒落到会话。
//     [NSRunningApplication activateWithOptions:] 单独不产生这次 reopen（它只把已运行的 App
//     提到前台），所以这一档必须真发一次 openURL(文件 URL)。
//   · workspace/open 在有窗口时走的是另一支——「在那个工作区新开一个任务」，会把桌面多开一扇
//     （10-28、10-29 各实测一次），所以任何「要窗口」的动作都只允许在原生 AXWindows 数到 0 时做。

/// 把没窗口这一档要回来的那一次 openURL：文件 URL 走的是 LaunchServices 的 reopen
/// （与点 Dock 图标同一件事），ZCode 收到它才会恢复自己那一扇主窗。
static BOOL ReopenZcodeWindow() {
    return [[NSWorkspace sharedWorkspace]
        openURL:[NSURL fileURLWithPath:@"/Applications/ZCode.app"]];
}

static pid_t ZcodePid() {
    for (NSRunningApplication* r in [NSWorkspace sharedWorkspace].runningApplications) {
        NSString* bid = r.bundleIdentifier ?: @"";
        // 主进程才建得出界面树：同前缀的 helper（渲染/GPU）不是可点的那一档。
        if ([bid rangeOfString:@".zcode." options:NSCaseInsensitiveSearch].location != NSNotFound
            && ![bid containsString:@"helper"]
            && r.activationPolicy == NSApplicationActivationPolicyRegular) return r.processIdentifier;
    }
    return -1;
}

static NSString* AxStr(AXUIElementRef el, CFStringRef attr) {
    CFTypeRef v = nil;
    if (AXUIElementCopyAttributeValue(el, attr, &v) != kAXErrorSuccess || !v) return nil;
    NSString* out = (CFGetTypeID(v) == CFStringGetTypeID()) ? [(__bridge NSString*)v copy] : nil;
    CFRelease(v);
    return out;
}
/// 子节点带引用返回：父数组一释放，里面那些元素就没人持有了，直接攥裸指针会在下一跳被回收。
static void AxKids(AXUIElementRef el, std::vector<AXUIElementRef>& out) {
    CFArrayRef k = nil;
    if (AXUIElementCopyAttributeValue(el, kAXKids, (CFTypeRef*)&k) == kAXErrorSuccess && k) {
        for (CFIndex i = 0; i < CFArrayGetCount(k); i++)
            out.push_back((AXUIElementRef)CFRetain(CFArrayGetValueAtIndex(k, i)));
        CFRelease(k);
    }
}
static void AxRelease(std::vector<AXUIElementRef>& v) {
    for (auto e : v) if (e) CFRelease(e);
    v.clear();
}
static bool AxHasPress(AXUIElementRef el) {
    CFArrayRef a = nil;
    bool ok = false;
    if (AXUIElementCopyActionNames(el, &a) == kAXErrorSuccess && a) {
        for (CFIndex i = 0; i < CFArrayGetCount(a); i++)
            if (CFStringCompare((CFStringRef)CFArrayGetValueAtIndex(a, i), kAXPressAction, 0) == kCFCompareEqualTo)
                ok = true;
        CFRelease(a);
    }
    return ok;
}
/// bySubrole=真 按 AXSubrole 找，否则按 AXRole 找；结果带引用返回，调用方 AxRelease。
static void AxFind(AXUIElementRef el, bool bySubrole, NSString* want, int depth,
                   std::vector<AXUIElementRef>& out, int cap) {
    if (depth > 24 || (int)out.size() >= cap) return;
    std::vector<AXUIElementRef> kids;
    AxKids(el, kids);
    for (auto k : kids) {
        NSString* got = AxStr(k, bySubrole ? CFSTR("AXSubrole") : CFSTR("AXRole"));
        if (got && [got isEqualToString:want]) out.push_back((AXUIElementRef)CFRetain(k));
        AxFind(k, bySubrole, want, depth + 1, out, cap);
    }
    AxRelease(kids);
}
/// 行里最内层的那段文字就是标题（Chromium 把可访问名挂在 AXStaticText 的 value 上）
static NSString* AxFirstText(AXUIElementRef el, int depth) {
    if (depth > 12) return nil;
    std::vector<AXUIElementRef> kids;
    AxKids(el, kids);
    NSString* out = nil;
    for (auto k : kids) {
        NSString* role = AxStr(k, CFSTR("AXRole"));
        if (role && [role isEqualToString:@"AXStaticText"]) {
            NSString* t = AxStr(k, CFSTR("AXValue")) ?: AxStr(k, CFSTR("AXTitle"));
            if (t.length) { out = t; break; }
        }
        out = AxFirstText(k, depth + 1);
        if (out) break;
    }
    AxRelease(kids);
    return out;
}

/// 当前打开的是哪一条：主区 AXSectionHeader 里的 AXHeading。这一句是整条链路的裁判。
static NSString* ZcodeOpenTitle(AXUIElementRef web) {
    std::vector<AXUIElementRef> heads;
    AxFind(web, true, @"AXSectionHeader", 0, heads, 8);
    NSString* out = nil;
    for (auto h : heads) {
        std::vector<AXUIElementRef> ax;
        AxFind(h, false, @"AXHeading", 0, ax, 4);
        for (auto e : ax) if ((out = AxFirstText(e, 0))) break;
        AxRelease(ax);
        if (out) break;
    }
    AxRelease(heads);
    return out;
}

/// 侧栏里可点的会话行（元素带引用返回，调用方逐个 CFRelease）
static void ZcodeRows(AXUIElementRef web, std::vector<AXUIElementRef>& rows,
                      NSMutableArray<NSString*>* titles) {
    std::vector<AXUIElementRef> lists;
    AxFind(web, true, @"AXContentList", 0, lists, 200);
    for (auto lst : lists) {
        std::vector<AXUIElementRef> kids;
        AxKids(lst, kids);
        for (auto child : kids) {
            if (!AxHasPress(child)) continue;
            NSString* t = AxFirstText(child, 0);
            if (!t.length) continue;
            rows.push_back((AXUIElementRef)CFRetain(child));
            [titles addObject:t];
        }
        AxRelease(kids);
    }
    AxRelease(lists);
}

static int ZScore(NSString* needle, NSString* item) {
    NSString* a = NormForMatch(needle), *b = NormForMatch(item);
    if (!a.length || !b.length) return 0;
    if ([a isEqualToString:b]) return 100;
    const NSUInteger sh = MIN(a.length, b.length);
    if (sh >= 6 && ([a hasPrefix:b] || [b hasPrefix:a])) return 80;
    if ([b containsString:a] || [a containsString:b]) return 60;
    return 0;
}

static int BestOf(NSString* needle, NSArray<NSString*>* titles) {
    int best = -1, bs = 0;
    for (NSUInteger i = 0; i < titles.count; i++) {
        const int s = ZScore(needle, titles[i]);
        if (s > bs) { bs = s; best = (int)i; }
    }
    return bs >= 60 ? best : -1;
}

/// 点中一行后等主区换成这一条：500ms 一档，最多 6 档（实测 500ms 内就到）
static bool ZcodeLanded(AXUIElementRef web, NSString* title) {
    for (int i = 0; i < 6; i++) {
        usleep(500 * 1000);
        NSString* h = ZcodeOpenTitle(web);
        if (h.length && ZScore(title, h) >= 60) return true;
    }
    return false;
}

/// 有几个真窗口。只数 AXWindow 角色：10-29 实测它没有窗口时 AXWindows 里塞的是一个
/// 0x0 的 AXApplication 占位（数成了 1 就会误判「有窗口」，那一支就永远走不到要回窗口）。
/// 返回 -1＝连 AXWindows 都读不到。
static int ZcodeWindowCount(AXUIElementRef app) {
    CFArrayRef ws = nil;
    if (AXUIElementCopyAttributeValue(app, CFSTR("AXWindows"), (CFTypeRef*)&ws) != kAXErrorSuccess) return -1;
    if (!ws) return 0;
    int n = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(ws); i++) {
        AXUIElementRef w = (AXUIElementRef)CFArrayGetValueAtIndex(ws, i);
        NSString* role = AxStr(w, CFSTR("AXRole"));
        if ([role isEqualToString:@"AXWindow"]) n++;   // 别用 ==：那是比指针，实测永远不等
    }
    CFRelease(ws);
    return n;
}

/// cwd 末级＝侧栏里的项目组名（一个工作区目录就是一个组，组名就是那一级目录名）。没绑项目的会话落在
/// ~/.zcode/workspace/default，那一档的末级是 "default"、不是组名，别拿它去匹配（与 scan.cpp 同一判据）。
static NSString* ZcodeGroupOf(const char* cwd) {
    if (!cwd || !*cwd) return nil;
    NSString* p = @(cwd);
    if ([p rangeOfString:@"/.zcode/workspace/"].location != NSNotFound) return nil;
    NSString* last = p.lastPathComponent;
    return last.length ? last : nil;
}

/// 找 AXWebArea 并把它的引用放进 webs（找到就停）。
static void ZcodeWebArea(AXUIElementRef app, std::vector<AXUIElementRef>& webs) {
    CFArrayRef ws = nil;
    if (AXUIElementCopyAttributeValue(app, CFSTR("AXWindows"), (CFTypeRef*)&ws) == kAXErrorSuccess && ws) {
        for (CFIndex j = 0; j < CFArrayGetCount(ws) && webs.empty(); j++)
            AxFind((AXUIElementRef)CFArrayGetValueAtIndex(ws, j), false, @"AXWebArea", 0, webs, 1);
        CFRelease(ws);
    }
}

/// 数一节子树里的两样东西：可点的会话行（AXContentList 的可点子节点）、本组的「显示更多」，
/// 并把第一枚「显示更多」带引用留在 *more（moren>1 时不给，调用方要因为分不清归属而收手）。
static void ZcodeCountSection(AXUIElementRef el, int depth, long* rows, long* moren,
                              AXUIElementRef* more) {
    if (depth > 14) return;
    std::vector<AXUIElementRef> kids;
    AxKids(el, kids);
    for (auto k : kids) {
        NSString* role = AxStr(k, CFSTR("AXRole"));
        NSString* sub = AxStr(k, CFSTR("AXSubrole"));
        NSString* t = AxStr(k, CFSTR("AXTitle"));
        if (role && [role isEqualToString:@"AXButton"] && t && [t isEqualToString:@"显示更多"] && AxHasPress(k)) {
            (*moren)++;
            if (!*more) *more = (AXUIElementRef)CFRetain(k);
        }
        if (sub && [sub isEqualToString:@"AXContentList"]) {
            std::vector<AXUIElementRef> rk;
            AxKids(k, rk);
            for (auto c : rk) if (AxHasPress(c)) (*rows)++;
            AxRelease(rk);
        }
        ZcodeCountSection(k, depth + 1, rows, moren, more);
    }
    AxRelease(kids);
}

/// 目标项目组的那一节在哪一层——10-29 实测四个组的树形状：
///   一节＝直接子节点只有两枚 AXGroup（组头行 0 行 ＋ 行块 N 行）：
///     三个组各自上溯 2 层就是它那一节，分别 18 / 4 / 3 行、都是 2 枚子节点
///   再往上一层（其中一个组上溯 3 层）＝25 行 9 子——那是好几个组并排，
///   在这一层按「显示更多」翻的就是别人的组（先前那版正是这么把 22 行按到 35 行的）。
/// 所以：从组头一路上溯，第一个「装着行」的祖先才算这一节；它必须只有两枚直接子节点，
/// 且这一节里恰好一枚「显示更多」。任一条不满足就什么也不按——替他多翻一个组是动他界面。
static void ZcodeSectionFor(AXUIElementRef web, NSString* group, bool* seen, long* rows,
                            AXUIElementRef* more) {
    *seen = false; *rows = 0; *more = nil;    if (!group.length) return;
    std::vector<AXUIElementRef> btns;
    AxFind(web, false, @"AXButton", 0, btns, 400);
    AXUIElementRef head = nil;
    for (auto b : btns) {
        NSString* t = AxStr(b, CFSTR("AXTitle"));
        if (t && [t isEqualToString:group]) { head = (AXUIElementRef)CFRetain(b); break; }
    }
    AxRelease(btns);
    if (!head) return;
    *seen = true;
    // up 自己拿一份引用：往上那一圈每层都 release 掉手里的，若让它直接等于 head，
    // 第一层就会把 head 唯一的引用放掉，函数末尾再 release 一次是超放。
    AXUIElementRef up = (AXUIElementRef)CFRetain(head);
    for (int level = 1; level <= 4; level++) {
        CFTypeRef p = nil;
        if (AXUIElementCopyAttributeValue(up, kAXParentAttribute, &p) != kAXErrorSuccess || !p) break;
        CFRelease(up);
        up = (AXUIElementRef)p;
        long r = 0, m = 0;
        AXUIElementRef got = nil;
        ZcodeCountSection(up, 0, &r, &m, &got);
        if (r == 0) continue;                       // 这一组还没列出行：接着往上找装着它的那一节
        std::vector<AXUIElementRef> ks;
        AxKids(up, ks);
        const long kids = (long)ks.size();
        AxRelease(ks);
        if (kids > 2) { if (got) CFRelease(got); break; }   // 第一个有行的祖先已是多组并排，分不清归属
        *rows = r;
        if (m == 1) *more = got; else if (got) CFRelease(got);
        break;
    }
    if (up != head) CFRelease(up);
    CFRelease(head);
}

/// ZCode 本次启动的时刻（epoch 秒），0＝读不到。它这次的会话列表是启动时的那一份快照，
/// 拿这个与会话的 time_created 一比，就能分清「藏在显示更多后面」和「它压根没列」。
/// NSRunningApplication 不给启动时刻（编译期就没这个属性），走 sysctl KERN_PROC_PID。
static double ZcodeStartEpoch(pid_t pid) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);
    if (sysctl(mib, 4, &kp, &len, nullptr, 0) != 0 || len == 0) return 0;
    return (double)kp.kp_proc.p_starttime.tv_sec;
}

extern "C" int ap_zcode_navigate(const char* title, const char* cwd, double created,
                                 char* reason, int out_len) {
    if (!title || !*title) { Say(reason, out_len, @"ZCode 这一跳缺标题，退回普通落点"); return 1; }
    {   // 防抖：连点多轮时每一轮都是完整的「唤起＋读树＋点」，别叠着敲
        static double lastNavAt = 0;
        const double now = [NSProcessInfo processInfo].systemUptime;
        if (now - lastNavAt < 4.0) {
            Say(reason, out_len, @"ZCode 这一跳 4 秒内重复触发，忽略本轮");
            return 1;
        }
        lastNavAt = now;
    }
    if (!AXIsProcessTrusted()) {
        // 2＝挡在授权这一层（与 ap_qoder_tray 同一码），面板据此叫他开开关，
        // 而不是把「没权限」混进「App 里没这一条」。
        Say(reason, out_len, @"未授权辅助功能，点不到 ZCode 侧栏里的会话");
        return 2;
    }
    const pid_t pid = ZcodePid();
    if (pid < 0) {   // 3＝没在跑（与托盘那档同一码）：这一跳先由阶梯把 App 唤起来
        Say(reason, out_len, @"ZCode 没在跑，先把它唤起来；再点一次就能落到这一条");
        return 3;
    }
    if (!ActivateOrOpen(@"/Applications/ZCode.app")) {
        Say(reason, out_len, @"ZCode 没唤起来，侧栏那一步放弃");
        return 1;
    }
    AXUIElementRef app = AXUIElementCreateApplication(pid);
    // 不置这一档，Chromium 根本不建无障碍树：读到的只有六层空 AXGroup。
    AXUIElementSetAttributeValue(app, CFSTR("AXManualAccessibility"), kCFBooleanTrue);
    std::vector<AXUIElementRef> webs;
    BOOL woke = NO;   // 是不是我们替它把窗口要回来的（回执要说清，别混进「侧栏没这一条」）
    // 窗口数这一档不依赖 Chromium 的树，秒读，所以先用它分清两种「读不到界面」：
    // 压根没有窗口（可以替它开），和窗口在、树没建起来（等多久都没用，只能如实报）。
    const int wins0 = ZcodeWindowCount(app);
    if (wins0 == 0) {
        // 只在「一个窗口都没有」时 reopen：有窗口时这一发只会把它提到前台，不动桌面。
        // 不用 zcode://workspace/open —— 那条会弹「是否在 ZCode 中打开此文件夹」的模态框，
        // 界面树从此卡在框后面（见上面那段实测）。
        woke = ReopenZcodeWindow() ? YES : NO;
    }
    // 替它要回窗口之后不原地干等：reopen 实测 3 秒内出窗，但无障碍树要重新置一次才认这一扇
    // 新 renderer。面板一跳最多只能停两三秒，所以只等 2.4 秒；还没长出来就返回 5＝
    // 「窗口正在开，过几秒再点一次就能落到这一条」，别把面板冻在他脸上。
    const int rounds = woke ? 4 : 8;
    for (int i = 0; i < rounds && webs.empty(); i++) {
        // 新开的那一扇是自己长起来的：每轮重新置一次 ManualAccessibility——
        // 之前置的那一次不认这个新 renderer，只等不置就会白等满预算。
        if (woke) AXUIElementSetAttributeValue(app, CFSTR("AXManualAccessibility"), kCFBooleanTrue);
        ZcodeWebArea(app, webs);
        if (!webs.empty()) break;
        usleep(600 * 1000);
    }
    if (webs.empty()) {
        const int wins = ZcodeWindowCount(app);
        fprintf(stderr, "[落点·ZCode] 没有界面树：进函数时 %d 扇，现在 %d 扇，%s\n",
                wins0, wins, woke ? "窗口是本次要回来的，界面还没长出来" : "没发 reopen");
        fflush(stderr);
        CFRelease(app);
        if (woke) {   // 5＝这一跳没白做：窗口已经在开，下一跳就点得到
            Say(reason, out_len, @"ZCode 的窗口正在打开（界面还没长出来），过几秒再点一次就能落到这一条");
            return 5;
        }
        Say(reason, out_len,
            wins == 0 ? (woke ? @"让 ZCode 重开窗口，它没应答（窗口还是 0 扇）"
                              : @"ZCode 一个窗口都没有，这一跳也没能把它要回来")
            : wins < 0 ? @"ZCode 的窗口数都读不到（它没应答辅助功能查询）"
                       : @"ZCode 的窗口在，但没把界面树交出来（它自己的无障碍还没就绪）");
        return 1;
    }
    AXUIElementRef web = webs[0];
    std::vector<AXUIElementRef> rows;
    NSMutableArray<NSString*>* titles = [NSMutableArray array];
    // 刚要回来的窗口里，列表是自己填的：给它一轮，别拿半棵树去判「没这一条」。
    for (int i = 0; i < (woke ? 8 : 1); i++) {
        for (auto r : rows) CFRelease(r);
        rows.clear(); [titles removeAllObjects];
        ZcodeRows(web, rows, titles);
        if (BestOf(@(title), titles) >= 0 || (!woke && i)) break;
        usleep(500 * 1000);
    }
    int best = BestOf(@(title), titles);
    // 每个项目组只列它那一节的前几行，其余藏在本组那枚「显示更多」后面（10-29 实测：
    // 同一颗按下去行数会涨）。不替他翻到底就判「侧栏没这一条」，正是他这次报的现象。
    // 只翻目标那一组、且只认那一节里恰好一枚的按钮（判据见 ZcodeSectionFor），
    // 这一跳的翻页预算压在 2.5 秒内——面板不能冻在他脸上。
    bool group_seen = false;
    bool group_checked = false;   // 一上来就命中时根本没走翻页那一轮：日志不许说「组不在」
    long group_rows = 0;
    int more_presses = 0;   // 替他按了几次「显示更多」——回执要说出来，那是动了他界面
    NSString* group = ZcodeGroupOf(cwd);
    const double navT0 = [NSProcessInfo processInfo].systemUptime;
    for (int round = 0; round < 6 && best < 0; round++) {
        if ([NSProcessInfo processInfo].systemUptime - navT0 > 2.5) break;   // 按之前先看表，别按完才发现超时
        AXUIElementRef more = nil;
        group_checked = true;
        ZcodeSectionFor(web, group, &group_seen, &group_rows, &more);
        if (!more) break;
        const AXError e = AXUIElementPerformAction(more, kAXPressAction);
        CFRelease(more);
        if (e != kAXErrorSuccess) break;
        more_presses++;
        usleep(600 * 1000);
        for (auto r : rows) CFRelease(r);
        rows.clear(); [titles removeAllObjects];
        ZcodeRows(web, rows, titles);
        best = BestOf(@(title), titles);
    }
    int rc = 1;
    bool said = false;
    if (best < 0) {
        // 命令中心那条路 10-29 撤了（工作区级作用域，实测 0/10，见上面那段注释）：
        // 侧栏没有就是没有，如实报这一组有几行，别为了「看起来像跳到了」再敲一轮键。
        // 但「没有」分三种，下一步完全不同，必须分开说（判据都是 10-29 实测的）：
        //   ① 组头本身不在侧栏 → 它列的是这一轮它认得的那些工作区；
        //   ② 这条的建立时刻比 ZCode 本次启动晚 → 返回 6，单独一档（实测：本次启动之后建的
        //      15 条，一条都不在它这次的侧栏里；启动前建的在列，哪怕最新的几条已经翻出来了）；
        //   ③ 组在、也不晚 → 翻到底仍没匹配上，报这一组的行数。
        NSString* why;
        const double start = ZcodeStartEpoch(pid);
        if (group.length && !group_seen) {
            why = [NSString stringWithFormat:@"ZCode 侧栏里没有「%@」这个项目组，所以也没列它的会话", group];
        } else if (created > 0 && start > 0 && created >= start) {
            NSDateFormatter* df = [[NSDateFormatter alloc] init];
            df.dateFormat = @"MM-dd HH:mm";
            why = [NSString stringWithFormat:
                   @"这条 %@ 才建，比 ZCode 本次启动（%@）晚，它这次的侧栏列表里没有它",
                   [df stringFromDate:[NSDate dateWithTimeIntervalSince1970:created]],
                   [df stringFromDate:[NSDate dateWithTimeIntervalSince1970:start]]];
            rc = 6;
        } else if (!group.length) {
            why = [NSString stringWithFormat:
                   @"这条没绑项目（落在 ZCode 的默认工作区），侧栏里没有对应的项目组可定位：「%@」", @(title)];
        } else if (group_rows <= 0) {
            // 组头在、但没能单独认出它那一节：要么那一节没列出行，要么它和邻组并排挂在同一层
            // （判据见 ZcodeSectionFor）。这一档一个字都不许写成「翻到底只有 N 行」——那是没翻的证据。
            why = [NSString stringWithFormat:
                   @"ZCode 侧栏里有「%@」这一组，但没能把它单独认出来（它那一节没列出行，或与别的组并排在同一层），所以没替它翻页", group];
        } else {
            // 「翻到底」只有在真按过那颗才算说过；一颗没按就报行数时，必须连带说这一组
            // 已经没有「显示更多」了——否则他会以为再等一等就能翻出来。
            why = [NSString stringWithFormat:
                   more_presses ? @"ZCode 侧栏「%@」这一组翻到底列了 %ld 行，没有「%@」这一条"
                                : @"ZCode 侧栏「%@」这一组现在只列了 %ld 行（那颗「显示更多」已经不在这组里），没有「%@」这一条",
                   group, group_rows, @(title)];
        }
        Say(reason, out_len, why);
        said = true;
    } else {
        // 刚 reopen 出来的第一跳实测会空一发（8 次里 1 次：AXPress 返回成功，但主区还在画
        // 它自己恢复的那一条，裁判 3 秒内等不到标题变）。补一次「重读行＋再按一次」，
        // 裁判一个字都不放松：仍然要主区真换成这一条才算命中。
        AXError e = kAXErrorFailure;
        int idx = best;
        const int tries = woke ? 2 : 1;
        for (int a = 0; a < tries; a++) {
            if (a) {   // 第二轮：窗口是新开出来的，行元素可能已经换了，重新读一遍再按
                for (auto r : rows) CFRelease(r);
                rows.clear(); [titles removeAllObjects];
                ZcodeRows(web, rows, titles);
                idx = BestOf(@(title), titles);
                if (idx < 0) {
                    Say(reason, out_len, [NSString stringWithFormat:
                                          @"ZCode 窗口刚开起来，侧栏这一轮只剩 %lu 行、没有「%@」这一条",
                                          (unsigned long)titles.count, @(title)]);
                    said = true;
                    break;
                }
            }
            e = AXUIElementPerformAction(rows[(NSUInteger)idx], kAXPressAction);
            if (e == kAXErrorSuccess && ZcodeLanded(web, @(title))) {
                NSString* head = [NSString stringWithFormat:@"%@%@",
                                  woke ? @"先替它开出了窗口，" : @"",
                                  more_presses ? @"替它翻开了那一组，" : @""];
                Say(reason, out_len, [NSString stringWithFormat:@"%@已在 ZCode 侧栏点中「%@」",
                                      head, titles[(NSUInteger)idx]]);
                said = true;
                rc = 0;
                break;
            }
            usleep(400 * 1000);
        }
        if (!said) {
            Say(reason, out_len, e == kAXErrorSuccess
                ? @"点中了侧栏那一行，但主区没换成这条"
                : [NSString stringWithFormat:@"侧栏里找到了但按下去没应答（AX %d）", (int)e]);
            said = true;
        }
    }
    for (auto r : rows) CFRelease(r);
    CFRelease(web);
    CFRelease(app);
    fprintf(stderr, "[落点·ZCode] 窗口 %d 扇，侧栏 %lu 行（组「%s」%s，本组 %ld 行，显示更多按了 %d 次）%s，"
                    "标题「%s」/ 建于 %.0f → %s｜%s\n",
            wins0, (unsigned long)titles.count, group.UTF8String ?: "-",
            !group_checked ? "未查" : (group_seen ? "在" : "不在"), group_rows, more_presses,
            woke ? "（窗口是本次要回来的）" : "", title, created,
            rc == 0 ? "命中" : (rc == 6 ? "比本次启动晚" : "未落到"), reason ?: "");
    fflush(stderr);
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

// ── 纯 CLI 的终端托管（2026-11-12）：iTerm2 优先、Terminal 兜底 ──────────────
// AppleScript 字符串转义（\ 与 "），命令体里的单引号由 ShQ 负责。
static NSString *EscAS(NSString *s) {
    return [s stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"]
            ?: @"";
}
// osascript 子进程 + 超时杀掉：iTerm2 的自动化在授权框没人点/接口卡住时会永久挂起，
// NSAppleScript 同步跑会把调用线程一起拖死（2026-11-12 实拍）。osascript 走独立进程，
// 超时即 terminate 退下一级；首次会弹一次 TCC 授权框，点一次允许就永久生效。
// 带输出捕获的 osascript（超时同 RunOSC 语义）；*ok 回执行成败，返回 stdout。
static NSString *RunOSOut(NSString *src, NSTimeInterval secs, BOOL *ok) {
    @try {
        NSTask *t = [[NSTask alloc] init];
        t.launchPath = @"/usr/bin/osascript";
        t.arguments = @[ @"-e", src ];
        NSPipe *out = [NSPipe pipe];
        t.standardOutput = out;
        t.standardError = [NSPipe pipe];
        [t launch];
        NSDate *dl = [NSDate dateWithTimeIntervalSinceNow:secs];
        while ([t isRunning] && [dl timeIntervalSinceNow] > 0)
            [NSThread sleepForTimeInterval:0.05];
        if ([t isRunning]) { [t terminate]; if (ok) *ok = NO; return @""; }
        if (ok) *ok = (t.terminationStatus == 0);
        NSData *d = [out.fileHandleForReading readDataToEndOfFile];
        return [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] ?: @"";
    } @catch (...) { if (ok) *ok = NO; return @""; }
}
static NSString *g_lastOscErr = @"";
static BOOL RunOSC(NSString *src, NSTimeInterval secs) {
    g_lastOscErr = @"";
    @try {
        NSTask *t = [[NSTask alloc] init];
        t.launchPath = @"/usr/bin/osascript";
        t.arguments = @[ @"-e", src ];
        t.standardOutput = [NSPipe pipe];
        NSPipe *errp = [NSPipe pipe];
        t.standardError = errp;
        [t launch];
        NSDate *dl = [NSDate dateWithTimeIntervalSinceNow:secs];
        while ([t isRunning] && [dl timeIntervalSinceNow] > 0)
            [NSThread sleepForTimeInterval:0.05];
        if ([t isRunning]) { [t terminate]; g_lastOscErr = @"osascript 超时"; return NO; }
        if (t.terminationStatus == 0) return YES;
        NSData *ed = [errp.fileHandleForReading readDataToEndOfFile];
        NSString *e = [[NSString alloc] initWithData:ed encoding:NSUTF8StringEncoding] ?: @"?";
        g_lastOscErr = [e stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        return NO;
    } @catch (...) { g_lastOscErr = @"osascript 异常"; return NO; }
}
static BOOL RunOSA(NSString *src) {   // 可靠通路（Terminal.app）保留同步版
    @try {
        NSAppleScript *as = [[NSAppleScript alloc] initWithSource:src];
        NSDictionary *err = nil;
        [as executeAndReturnError:&err];
        return err == nil;
    } @catch (...) { return NO; }
}
static NSString *ShQ(NSString *s) {
    if (!s) s = @"";
    NSString *inner = [s stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"];
    return [[@"'" stringByAppendingString:inner] stringByAppendingString:@"'"];
}
// 在 iTerm2 新开 tab / Terminal 新开窗口执行一个「脚本文件路径」——命令本体写在
// 文件里（ObjC 直写，不过 AppleScript 字符串这一层），AS 只看到一句纯 ASCII 的
// sh /tmp/xxx.sh，uuid/中文路径/引号都没有被打乱的面（2026-11-12：之前把整条命令
// 内插进 do script，uuid 被打烂成 I'⁺^A 卡在 zsh 续行提示上，实拍）。
// iTerm2 永远优先（在跑或装了都试，activate 顺手拉起），它的 AS 报错才退 Terminal。
// 终端新开窗口跑一段命令体：ObjC 直写临时脚本（引号/中文/uuid 都不过 AS 字符串层），
// AppleScript 只看到 sh <纯ASCII路径>。iTerm2 新窗口（create window 无既有 tab 竞态），
// 失败退 Terminal.do script。2026-11-12：两处调用方曾把内插命令串当「路径」传进来，
// 又被本函数加 sh 前缀——uuid 打烂成 I'⁺^A 的实拍根因。现在语义唯一：传命令体。
static BOOL RunInTerminal(NSString *cmdBody) {
    char pathp[64];
    snprintf(pathp, sizeof pathp, "/tmp/ap-run-%lld.sh", (long long)time(nil));
    FILE *f = fopen(pathp, "w");
    if (!f) return NO;
    fprintf(f, "%s\n", cmdBody.UTF8String ?: "");
    fclose(f);
    chmod(pathp, 0755);
    NSString *path = [NSString stringWithUTF8String:pathp];
    // 终式（2026-11-12 实测两种证据）：create window 直接带 command 参数——会话以该
    // 命令启动，不存在「开窗后再模拟敲键」的竞态（write text 方案在真机上 flakes 两次）。
    // 用户规则（2026-11-12 第三次强调）：有 iTerm2 就优先打开 iTerm2，终端优先级靠后。
    // iTerm2 失败绝不静默换 Terminal——先把失败原因留在 g_lastOscErr 里（调用方写进
    // reason，面板与日志都看得见），Terminal 只做「iTerm2 失败后的最后兜底」并注明。
    const BOOL itermInstalled =
        [[NSFileManager defaultManager] fileExistsAtPath:@"/Applications/iTerm.app"] ||
        [[NSFileManager defaultManager] fileExistsAtPath:@"/Applications/iTerm2.app"];
    if (itermInstalled) {
        // ignoring application responses：create-window-command 这个事件在会话命令
        // 持续运行时不回执（实测 8s 仍挂）——同步等它必被 5s 超时误判成失败、再去开
        // Terminal（双开）。发完即返回，窗口照开；结果由后续进程检查兜底。
        NSString *it = [NSString stringWithFormat:
            @"ignoring application responses\n"
             "tell application \"iTerm2\"\nactivate\n"
             "create window with default profile command \"sh %@\"\nend tell\n"
             "end ignoring", path];
        if (RunOSC(it, 3.0)) return YES;
        fprintf(stderr, "[终端] iTerm2 脚本失败，退 Terminal 兜底 ｜ %s\n",
                g_lastOscErr.UTF8String ?: "");
        fflush(stderr);
    }
    NSString *te = [NSString stringWithFormat:
        @"tell application \"Terminal\"\nactivate\ndo script \"sh %@\"\nend tell", path];
    return RunOSA(te);
}
// 活终端切前台：osascript activate。NSRunningApplication.activateWithOptions: 自 macOS 14
// 起废弃成「无效果」（2026-11-12 实拍：返回成功但窗口纹丝不动，toast 谎报已切）。
static BOOL WakeTerminalFront(void) {
    NSRunningApplication *it = [[NSRunningApplication runningApplicationsWithBundleIdentifier:
        @"com.googlecode.iterm2"] firstObject];
    if (it) return RunOSC(@"tell application \"iTerm2\" to activate", 2.0);
    NSRunningApplication *te = [[NSRunningApplication runningApplicationsWithBundleIdentifier:
        @"com.apple.Terminal"] firstObject];
    if (te) return RunOSC(@"tell application \"Terminal\" to activate", 2.0);
    return NO;
}
extern "C" int ap_open_cli_resume(const char* agent_id, const char* session_id, const char* cwd,
                                  char* reason, int out_len, int* layer) {
    const Target* t = Find(agent_id);
    if (!t || !t->cli_fmt[0]) { Say(reason, out_len, @"这一路没有配置终端 resume 命令"); return 1; }
    // 命令体：cd '<cwd 转义>' + exec <resume 命令>。会话 id 是 uuid（无 shell 元字符），
    // 裸传；cwd 含中文/空格，用 POSIX 单引号包住并转义内嵌单引号。
    // 命令体在 C 侧拼（snprintf 的 %s 吃 char*，语义正确）。之前的写法把 NSString
    // 塞进 %@ 格式串当 %s 用——打出的是对象指针字节（脚本里出现 I'╊ 这种乱码 uuid，
    // 2026-11-12 用户实测三轮「新窗口没会话」的直接根因）。
    char cmdb[512];
    snprintf(cmdb, sizeof cmdb, t->cli_fmt, session_id ? session_id : "");
    NSString *cliCmd = @(cmdb);
    // 探测段：iTerm2 的 command 会话没有用户 PATH，裸 `claude` 会 not found——
    // 脚本头逐个探测常见安装位，用解析出的绝对路径 exec。
    NSString *cliName = [[cliCmd componentsSeparatedByString:@" "] firstObject] ?: @"claude";
    NSString *rest = cliCmd.length > cliName.length
        ? [cliCmd substringFromIndex:cliName.length + 1] : @"";
    NSString *probe = [NSString stringWithFormat:
        @"CLI=\"\"\n"
         "for c in /opt/homebrew/bin/%@ /usr/local/bin/%@ \"$HOME/.%@/local/%@\" /usr/bin/%@; do\n"
         " [ -x \"$c\" ] && CLI=\"$c\" && break\n"
         "done\n"
         "if [ -z \"$CLI\" ]; then echo \"%@ not found in standard locations\"; exit 1; fi\n",
         cliName, cliName, cliName, cliName, cliName, cliName];
    NSString *body = [NSString stringWithFormat:
        @"%@\ncd %@ && exec \"$CLI\" %@",
        probe, ShQ(@(cwd)), rest];
    if (layer) *layer = 4;
    if (RunInTerminal(body)) {
        Say(reason, out_len, @"已在新终端窗口续上这条会话");
        return 0;
    }
    Say(reason, out_len, @"终端没开成（iTerm2 / Terminal 都试过）");
    if (layer) *layer = 0;
    return 1;
}
extern "C" int ap_wake_cli_term(char* reason, int out_len) {
    if (WakeTerminalFront()) { Say(reason, out_len, @"终端已切到前台"); return 0; }
    Say(reason, out_len, @"没有在跑的终端可切");
    return 1;
}

// 活 CLI 进程的 tty：pgrep -f <cli> 逐 pid 比对 cwd（lsof），命中后取 ps tty。
// 返回 YES 且 ttyOut 带上（如 ttys003）。

// 按 tty 在 iTerm2（先）→ Terminal 里定位标签/会话并选中切过去。
// 活 CLI 进程的 tty：ps 全表扫一行（tty + 命令），命令含 cli 词且 tty 形如 ttysNNN。
// 2026-11-12 实测：「ps -axo tty=,command=」一行就能看到「ttys000 claude」。
static NSString *LiveCliTTY(const char* cli) {
    char cmd[128];
    snprintf(cmd, sizeof cmd, "ps -axo tty=,command=");
    FILE *pp = popen(cmd, "r");
    if (!pp) return nil;
    char line[1024];
    NSString *hit = nil;
    while (fgets(line, sizeof line, pp)) {
        if (strncmp(line, "ttys", 4) != 0) continue;
        char *sp = line + 4;
        while (*sp && *sp != ' ') sp++;
        if (*sp != ' ') continue;
        *sp = 0;
        // 命令体：词元含 cli 名（claude/pi）。login/AgentPing 自身不误中：不含该词。
        if (strstr(sp + 1, cli) && !strstr(sp + 1, "AgentPing")) {
            hit = [NSString stringWithFormat:@"/dev/%s", line];
            break;
        }
    }
    pclose(pp);
    return hit;
}
// 按 tty 选 tab（2026-11-12 实测 selected）：窗口→标签→「tty of first session of T」
// 单会话定点访问安全快速（9ms 级）；挂死的只是「repeat with S in sessions」集合遍历，
// 本函数不碰集合。选 tab + 选窗口 + 激活，一步到位。
static BOOL SelectTabByTTY(const char* ttyDev) {
    NSString *sc = [NSString stringWithFormat:
        @"set out to \"nomatch\"\n"
         "tell application \"iTerm2\"\n"
         "repeat with W in windows\n"
         "repeat with T in tabs of W\n"
         "try\n"
         "set tt to tty of first session of T\n"
         "on error\n"
         "set tt to \"\"\n end try\n"
         "if tt is \"%@\" then\n"
         "tell T to select\n select W\n set out to \"selected\"\n exit repeat\n end if\n"
         "end repeat\n"
         "if out is \"selected\" then exit repeat\n"
         "end repeat\n"
         "end tell\n"
         "activate application \"iTerm2\"\n"
         "return out", @(ttyDev)];
    BOOL ok = NO;
    NSString *o = RunOSOut(sc, 4.0, &ok);
    return ok && [o containsString:@"selected"];
}
// 按窗口名选中：本机 iTerm2 的 tab「name」全部非文本（-1700，实测三枚全炸），而
// 窗口名可用（~/Desktop / OC / Greeting，窗口即标签形态）。多 needle 逐个试：
// 会话目录末级名 → cli 名（claude/pi）→（claude 加 "Claude"）。name 读取带 try，
// 一条坏名字不再炸整轮遍历。
static BOOL SelectWindowByNeedles(NSArray<NSString*> *needles) {
    for (NSString *nd in needles) {
        if (nd.length == 0) continue;
        NSString *low = [nd lowercaseString];
        NSString *sc = [NSString stringWithFormat:
            @"set out to \"nomatch\"\n"
             "tell application \"iTerm2\"\n"
             "repeat with W in windows\n"
             "try\n"
             "set nm to ((name of W) as text)\n"
             "on error\n"
             "set nm to \"\"\n end try\n"
             "if (nm as lowercaseString) contains \"%@\" then\n"
             "select W\n set out to \"selected\"\n exit repeat\n end if\n"
             "end repeat\n"
             "end tell\n"
             "activate application \"iTerm2\"\n"
             "return out", low];
        BOOL ok = NO;
        NSString *o = RunOSOut(sc, 3.0, &ok);
        if (ok && [o containsString:@"selected"]) return YES;
    }
    return NO;
}

extern "C" int ap_locate_cli_session(const char* agent_id, const char* session_id,
                                     const char* cwd, char* reason, int out_len, int* layer) {
    const Target* t = Find(agent_id);
    if (!t || !t->cli_fmt[0]) { Say(reason, out_len, @"这一路没有配置终端 resume 命令"); return 1; }
    NSString *fmt = @(t->cli_fmt);
    NSString *cli = [[fmt componentsSeparatedByString:@" "] firstObject] ?: @"";
    // ① 活进程按 tty 精确到 tab（含「非活动 tab」——窗口名只显示活动 tab，够不着，
    //    2026-11-12 用户实测两次进不了已开标签的正是这种）。
    NSString *tty = LiveCliTTY(cli.UTF8String);
    if (tty && SelectTabByTTY(tty.UTF8String)) {
        Say(reason, out_len, [NSString stringWithFormat:
            @"已进入跑着这条会话的标签（%@）", tty]);
        if (layer) *layer = 5;
        return 0;
    }
    // ② 退窗口名匹配（活动 tab 场景）。
    NSMutableArray<NSString*> *ns = [NSMutableArray array];
    if (cwd && *cwd) {
        const char *slash = strrchr(cwd, '/');
        if (slash && slash[1]) [ns addObject:[NSString stringWithUTF8String:slash + 1]];
    }
    [ns addObject:cli];
    if ([cli isEqualToString:@"claude"]) [ns addObject:@"Claude"];
    if (SelectWindowByNeedles(ns)) {
        Say(reason, out_len, @"已切到跑着这条会话的终端窗口");
        if (layer) *layer = 5;
        return 0;
    }
    Say(reason, out_len, @"没有找到跑着这条会话的标签，将新开一个续上");
    if (layer) *layer = 6;
    return 1;
}

extern "C" int ap_open3(const char* agent_id, const char* session_id, const char* cwd,
                        char* reason, int out_len, int* layer) {
    if (layer) *layer = 0;
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, @"无处可去：落点表里没有这一路采集器"); return 1; }
    NSString* app = t->app_rel[0] ? @(t->app_rel) : nil;
    NSString* log = Expand(t->log_rel);
    NSString* tail = @"";

    // ⓪ 纯 CLI 的会话级落点（claude/pi）：iTerm2›Terminal 新开窗口跑 resume 命令。
    //    真正「回到那一条会话」，比开一堆文件夹诚实；活会话的「只切前台」在面板侧判断。
    if (t->cli_fmt[0]) {
        char cmdb[512];
        snprintf(cmdb, sizeof cmdb, t->cli_fmt, session_id ? session_id : "");
        NSString* cliCmd = @(cmdb);
        NSString* cliName = [[cliCmd componentsSeparatedByString:@" "] firstObject] ?: @"claude";
        NSString* rest = cliCmd.length > cliName.length
            ? [cliCmd substringFromIndex:cliName.length + 1] : @"";
        NSString* probe = [NSString stringWithFormat:
            @"CLI=\"\"\n"
             "for c in /opt/homebrew/bin/%@ /usr/local/bin/%@ \"$HOME/.%@/local/%@\" /usr/bin/%@; do\n"
             " [ -x \"$c\" ] && CLI=\"$c\" && break\n"
             "done\n"
             "if [ -z \"$CLI\" ]; then echo \"%@ not found in standard locations\"; exit 1; fi\n",
             cliName, cliName, cliName, cliName, cliName, cliName];
        NSString* body = [NSString stringWithFormat:
            @"%@\ncd %@ && exec \"$CLI\" %@",
            probe, ShQ(@(cwd)), rest];
        if (layer) *layer = 4;
        if (RunInTerminal(body)) {
            Say(reason, out_len, @"已在新终端窗口续上这条会话");
            return 0;
        }
        Say(reason, out_len, @"终端没开成（iTerm2 / Terminal 都试过）");
        return 1;
    }

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
    //   能精确到会话的那两级（Qoder 托盘点名 / ZCode 侧栏点行）都靠辅助功能授权，
    //   归会话级钥匙 ap_qoder_tray / ap_zcode_navigate，不在这条阶梯里。
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
