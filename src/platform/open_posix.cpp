// Linux 落点：只用 xdg-open / gio 这一条通用通道，不 include 任何桌面库，
// 这样 core 侧的调用形状与 macOS 完全一致（同一个 C 接口、同一套逐级退）。
//
// 现状要说清：Linux 侧「唤起某个 App」的判据本机没法实测（这台机器是 macOS，
// Linux 分支从未跑过真数据），所以这里只做两件事——
// ① 有 deep link 就丢给 xdg-open（scheme 由桌面环境的默认程序接住）；
// ② 否则打开该采集器的会话记录目录。
// 都不成返回非 0，reason 里写明「Linux 侧落点未实机验证」。
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>

#include "open.h"

extern char** environ;

namespace {

struct Target { const char* log_rel; const char* deep_prefix; int has_client; };
const char* kIds[] = {"qoder", "claude", "codex", "zcode", "workbuddy"};
// has_client 与 macOS 那张表同判据：这几路是桌面客户端（Linux 上是否有包未实机核），
// claude/codex 两边都是纯 CLI，本机根本没有可跳的 App，只能落到记录目录。
const Target kTargets[] = {
    {".qoder/projects", "", 1},
    {".claude/projects", "", 0},
    {".codex/sessions", "", 0},
    {".zcode/cli/db", "", 1},
    {".workbuddy/projects", "workbuddy://chat/", 1},
};
const int kN = (int)(sizeof kTargets / sizeof kTargets[0]);

const Target* Find(const char* id) {
    if (!id) return nullptr;
    for (int i = 0; i < kN; i++)
        if (!strcasecmp(kIds[i], id)) return &kTargets[i];
    return nullptr;
}

std::string Home() {
    const char* h = getenv("HOME");
    return h && *h ? h : "/tmp";
}

std::string Expand(const char* rel) { return Home() + "/" + rel; }

bool Exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

void Say(char* reason, int n, const std::string& s) {
    if (!reason || n <= 0) return;
    strncpy(reason, s.c_str(), (size_t)n - 1);
    reason[n - 1] = 0;
}

/// fork + execvp（参数走 argv，绝不拼 shell 字符串），等它退出，exit 0 算成。
bool SpawnOpen(const std::string& target) {
    const char* progs[] = {"xdg-open", "gio", "open"};
    for (const char* prog : progs) {
        std::string a0(prog);
        char* argv[4];
        const char* extra = nullptr;
        if (!strcmp(prog, "gio")) extra = "open";        // gio 要 `gio open <path>`
        int i = 0;
        argv[i++] = (char*)a0.c_str();
        if (extra) argv[i++] = (char*)extra;
        argv[i++] = (char*)target.c_str();
        argv[i] = nullptr;
        pid_t pid;
        if (posix_spawn(&pid, prog, nullptr, nullptr, argv, environ) != 0) continue;
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return true;
    }
    return false;
}

}   // namespace

extern "C" int ap_open_plan(const char* agent_id, const char* session_id, char* reason, int out_len) {
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, "无处可去：落点表里没有这一路采集器"); return 1; }
    if (t->deep_prefix[0] && session_id && *session_id) {
        Say(reason, out_len, std::string("让 App 自己开到这条对话：xdg-open ") + t->deep_prefix + session_id);
        return 0;
    }
    std::string log = Expand(t->log_rel);
    if (Exists(log)) { Say(reason, out_len, "在文件管理器里打开：" + log); return 0; }
    Say(reason, out_len, "落空：这一路的记录目录不存在（Linux 侧落点未实机验证）");
    return 1;
}

extern "C" int ap_open(const char* agent_id, const char* session_id, char* reason, int out_len) {
    return ap_open3(agent_id, session_id, nullptr, reason, out_len, nullptr);
}

// ap_open 的加 cwd 版。Linux 侧 cwd 同样不参与落点：这里没有「按工作区打开」的 URL，
// 参数保留只为让 core 侧的调用形状与 macOS 一致（同一个阶梯、同一套 reason）。
extern "C" int ap_open2(const char* agent_id, const char* session_id, const char* cwd,
                        char* reason, int out_len) {
    return ap_open3(agent_id, session_id, cwd, reason, out_len, nullptr);
}

// layer 的档位与 macOS 完全一致（1 deep link / 2 唤起 App / 3 记录目录 / 0 没落上），
// 面板才能不分平台地把回执说准。本平台没有「唤起某个 App」的判据（xdg-open 只会把
// URL 丢给默认程序，认不出 scheme 就失败），所以只有 ①③ 两层能真命中。
extern "C" int ap_open3(const char* agent_id, const char* session_id, const char* cwd,
                        char* reason, int out_len, int* layer) {
    (void)cwd;
    if (layer) *layer = 0;
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, "无处可去：落点表里没有这一路采集器"); return 1; }
    if (t->deep_prefix[0] && session_id && *session_id) {
        std::string url = std::string(t->deep_prefix) + session_id;
        if (SpawnOpen(url)) {
            Say(reason, out_len, "已交给桌面环境打开：" + url);
            if (layer) *layer = 1;
            return 0;
        }
    }
    std::string log = Expand(t->log_rel);
    // 与 macOS 不同，这里不拦「有客户端却退到目录」那一档：本平台根本没有唤起步（xdg-open 认不出
    // scheme 就失败），拦住之后只剩「什么都不发生」。落到目录至少人还在数据旁边，
    // 而 layer=3 会让面板照实说「只打开了记录目录，没到那条会话」。
    if (Exists(log) && SpawnOpen(log)) {
        Say(reason, out_len, "已在文件管理器里打开：" + log);
        if (layer) *layer = 3;
        return 0;
    }
    Say(reason, out_len, "三条退路全落空（Linux 侧落点未实机验证）");
    return 1;
}

// 这一路在本机有没有可跳的客户端：判据与 macOS 同一套（那张表看 .app 在不在，
// 这里只能按表里的标记说），面板据此选按钮文案与预期。
extern "C" int ap_has_client(const char* agent_id) {
    const Target* t = Find(agent_id);
    return t && t->has_client ? 1 : 0;
}

// ZCode 的会话级落点在 macOS 上是读它侧栏的任务行按名字点中（AX，见 open_macos.mm）。
// 本平台没有等价能力：ZCode 没有会话级 URL，也没有能读别的 App 界面树并点某一行的通用通道
// （xdotool 那类要真桌面会话在场，本机从未在有桌面的 Linux 上跑过，不敢当作可用）。
// 返回 4（这一档在本平台不适用）并写清原因，调用方按 open.h 的契约退回上面的阶梯——
// 不静默、也不谎称跳到了，更不许显示成「客户端侧栏里没这一条」（那根本没读过它的树）。
extern "C" int ap_zcode_navigate(const char* title, const char* cwd, double created,
                                 char* reason, int out_len) {
    (void)title; (void)cwd; (void)created;
    Say(reason, out_len, "本平台没有读界面树点侧栏行的能力，落点退回唤起或打开记录目录");
    return 4;
}

// Qoder 的会话级落点在 macOS 上是点它自己托盘里的会话条目（AX，见 open_macos.mm）。
// Linux 侧没有等价能力：桌面环境不提供「读另一个 App 的菜单并点某一项」的通用通道，
// 而 qoder:// 本身没有会话级路由。返回 4（这一档在本平台不适用）并写清挡在哪一层，
// 调用方按 open.h 的契约把这句摆到屏幕上——不静默，也不拿「打开记录目录」冒充跳到了会话。
extern "C" int ap_qoder_tray(const char* title, char* reason, int out_len) {
    (void)title;
    Say(reason, out_len, "本平台没有托盘会话表这一档，只能唤起 App 或打开记录目录");
    return 4;
}

// 辅助功能是 macOS 那一档授权；本平台没有这一档（既没有 AX，也没有能点进别的 App 的
// 通用通道），所以返回 4＝不适用，并把话写进 reason。面板据此整格不画——
// 显示成「未授权」是逼他去拨一个这儿根本不存在的开关。
extern "C" int ap_ax_trusted(int prompt, char* reason, int out_len) {
    (void)prompt;
    Say(reason, out_len, "本平台没有辅助功能这一档");
    return 4;
}

extern "C" int ap_open_privacy_pane(char* reason, int out_len) {
    Say(reason, out_len, "本平台没有系统设置里的那一页");
    return 1;
}
