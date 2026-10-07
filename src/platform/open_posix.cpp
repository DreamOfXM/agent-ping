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

struct Target { const char* log_rel; const char* deep_prefix; };
const char* kIds[] = {"qoder", "claude", "codex", "zcode", "workbuddy"};
const Target kTargets[] = {
    {".qoder/projects", ""},
    {".claude/projects", ""},
    {".codex/sessions", ""},
    {".zcode/cli/db", ""},
    {".workbuddy/projects", "workbuddy://chat/"},
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
    const Target* t = Find(agent_id);
    if (!t) { Say(reason, out_len, "无处可去：落点表里没有这一路采集器"); return 1; }
    if (t->deep_prefix[0] && session_id && *session_id) {
        std::string url = std::string(t->deep_prefix) + session_id;
        if (SpawnOpen(url)) { Say(reason, out_len, "已交给桌面环境打开：" + url); return 0; }
    }
    std::string log = Expand(t->log_rel);
    if (Exists(log) && SpawnOpen(log)) { Say(reason, out_len, "已在文件管理器里打开：" + log); return 0; }
    Say(reason, out_len, "三条退路全落空（Linux 侧落点未实机验证）");
    return 1;
}
