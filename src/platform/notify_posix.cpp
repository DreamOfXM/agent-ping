// 非 Apple 平台（Linux/其它 POSIX）的提醒层：横幅走 notify-send，出声走桌面音库。
// 这里的每一条返回都写清「装什么才有」——本机第一次真跑通的是 docker + Xvfb，
// 那台容器里没有通知守护进程，所以它只会报「投递失败」，那是诚实读数而不是通过。
#include "notify.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* kPlayers[] = { "paplay", "pw-play", "ffplay", "canberra-gtk-play", "aplay", "cxplay" };
// freedesktop 声音主题：Debian/Ubuntu 的 sound-theme-freedesktop 装完就有这一批。
// aplay 只吃 wav，所以它在下面单独配 .wav 候选。
const char* kSoundFiles[] = {
    "/usr/share/sounds/freedesktop/stereo/message.oga",
    "/usr/share/sounds/freedesktop/stereo/complete.oga",
    "/usr/share/sounds/freedesktop/stereo/message-new-instant.oga",
    "/usr/share/sounds/freedesktop/stereo/ping.oga",
    "/usr/share/sounds/freedesktop/stereo/bell.oga",
    "/usr/share/sounds/alsa/Front_Center.wav",
};

bool exists(const char* p) { struct stat st; return p && stat(p, &st) == 0 && S_ISREG(st.st_mode); }

bool findInPath(const char* exe, std::string* out) {
    const char* path = getenv("PATH");
    if (!path || !*path) path = "/usr/bin:/bin:/usr/local/bin";
    std::string p = path;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find(':', i);
        std::string dir = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!dir.empty()) {
            std::string cand = dir + "/" + exe;
            if (access(cand.c_str(), X_OK) == 0) { if (out) *out = cand; return true; }
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return false;
}

/// fork/exec 一个短命令并收尸，返回退出码；-1 起不来，-2 超时被杀。
/// 超时是必需的：没有通知守护进程时 notify-send 会去等 D-Bus 答复，能把主循环挂住。
/// err_out 非空时把子进程的 stderr 原话带回来（notify.h 的契约写的是「reason 写平台原话」，
/// 退出码 1 可能是「没有守护进程」也可能是「我们这串参数被拒」，只看码分不开这两件事）。
int runWait(const std::vector<std::string>& argv, int timeout_ms, std::string* err_out = nullptr) {
    std::vector<char*> cv;
    for (const auto& s : argv) cv.push_back(const_cast<char*>(s.c_str()));
    cv.push_back(nullptr);
    char errpath[64];
    errpath[0] = 0;
    if (err_out) snprintf(errpath, sizeof errpath, "/tmp/agentping-err-%ld.log", (long)getpid());
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd = -1;
        if (errpath[0]) fd = open(errpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) { dup2(fd, 2); close(fd); }
        else { int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 2); close(dn); } }
        execvp(cv[0], &cv[0]);
        _exit(127);
    }
    int status = 0;
    const int step = 20;
    bool done = false;
    for (int waited = 0; waited < timeout_ms; waited += step) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) { done = true; break; }
        if (r < 0) return -1;
        usleep(step * 1000);
    }
    if (!done) {   // 超时：守护进程没回执，这条发没发出去不知道
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        if (errpath[0]) unlink(errpath);
        return -2;
    }
    if (err_out && errpath[0]) {
        if (FILE* f = fopen(errpath, "rb")) {
            char buf[200] = {0};
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            // 平台原话常常是多行（ALSA 一次能吐四行），压成一行才好和判据同屏读。
            for (size_t i = 0; i < n; i++) if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') buf[i] = ' ';
            while (n && (buf[n - 1] == ' ')) buf[--n] = 0;
            *err_out = buf;
        }
        unlink(errpath);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
}

void copyReason(char* reason, int out_len, const std::string& s) {
    if (!reason || out_len <= 0) return;
    strncpy(reason, s.c_str(), (size_t)out_len - 1);
    reason[out_len - 1] = 0;
}

std::string bannerErr(int rc, const std::string& err) {
    std::string s;
    switch (rc) {
    case 0:  return "已投递";
    // 退出码 1 分不开「没有守护进程」和「我们这串参数被拒」，所以平台原话必须一起带回来。
    case 1:  s = "守护进程没接上或参数被拒"; break;
    case 127: s = "二进制没起来"; break;
    case -2: s = "等 D-Bus 答复超时被杀"; break;
    default: { char b[48]; snprintf(b, sizeof b, "退出码 %d", rc); s = b; break; }
    }
    if (!err.empty()) s += "；平台原话：" + err;
    return s;
}

/// 玩家与音源必须成对选出来：paplay 只吃 ogg/flac、aplay 只吃 wav，
/// 极简镜像两样都没有。找不到就返回 false，让调用方明说「无声可出」。
/// 抽成一个函数是因为设置屏的测试音和提醒的兜底出声必须走同一个选择器，
/// 否则「测试响了」并不等于「提醒会响」。
bool pickSound(std::string& player, std::string& file) {
    for (const char* pl : kPlayers) {
        if (!findInPath(pl, nullptr)) continue;
        for (const char* f : kSoundFiles) {
            if (!exists(f)) continue;
            bool wav = strlen(f) >= 4 && !strcmp(f + strlen(f) - 4, ".wav");
            if (!strcmp(pl, "aplay") && !wav) continue;   // aplay 只吃 wav
            if (!strcmp(pl, "paplay") && wav) continue;   // paplay 只吃 ogg/flac
            player = pl; file = f; return true;
        }
    }
    return false;
}

std::vector<std::string> soundArgv(const std::string& player, const std::string& file) {
    if (player == "paplay" || player == "pw-play") return { player, file };
    if (player == "aplay" || player == "cxplay") return { player, "-q", file };
    if (player == "ffplay")
        return { player, "-loglevel", "error", "-nodisp", "-autoexit", "-quiet", file };
    return { player, "-i", "message", "-l", "0.8" };   // canberra-gtk-play 按事件名放
}

}  // namespace

extern "C" void ap_notify_on_click(ap_notify_click_fn fn, void* ud) {
    // Linux 侧「点横幅跳回那一条会话」需要通知守护进程回传激活动作
    // （freedesktop 的 Action 机制，KDE/GNOME 行为又不一致）。2026-10-07 在 dunst 上实测：
    // 一旦 notify-send 带 `-A`，它就恒不返回，投递状态因此永远问不出来 ——
    // 所以本平台连 `-A` 都不发，点击回传从源头就没有。这里明写不接，而不是假装接了。
    (void)fn; (void)ud;
    fprintf(stderr, "[提醒] 本平台未接横幅点击回调（带 action 会让 notify-send 恒不返回，实测于 dunst 1.9）\n");
    fflush(stderr);
}

extern "C" int ap_notify_channel(char* reason, int out_len) {
    std::string bin;
    if (!findInPath("notify-send", &bin)) {
        copyReason(reason, out_len, "本机没有 notify-send（Debian 需 apt-get install libnotify-bin）");
        return 1;
    }
    // notify-send 在不代表弹得出来：它要能连上会话总线，否则退出码 1。
    // 用一条空转的探测命令去问真实状态，比只看二进制存在诚实。
    // 不带 `-A`：2026-10-07 在 Debian 12 + dunst 1.9 实测，一旦声明 action，notify-send
    // 会一直等守护进程回传「用户点了哪一项」，没人点就永远不返回 —— 探测因此恒判「问不出来」，
    // 而 post 那条更糟：每条都超时被杀 → 账本不记 → 每轮重发，变成 3 秒一次的横幅风暴。
    std::string err;
    int rc = runWait({ "notify-send", "-a", "Agent Ping", "Agent Ping 通道探测",
                       "这条只用来验证通道，不含待处理会话" }, 3000, &err);
    copyReason(reason, out_len, "notify-send=" + bin + "，" + bannerErr(rc, err));
    return rc == 0 ? 0 : (rc == -2 ? 2 : 1);
}

extern "C" int ap_notify_post(const char* title, const char* body,
                              const char* agent_id, const char* session_id,
                              int want_sound, char* reason, int out_len) {
    std::string bin, err;
    const bool have_notify = findInPath("notify-send", &bin);
    int rc = -1;
    if (have_notify) {
        // 标题里是「谁 + 什么状态」，正文是那条会话的原话；身份走 freedesktop 的
        // string hint 带过去（本平台没接点击回传，带上只是给守护进程/日志留线索，不谎称能跳）。
        // hint 的写法是 TYPE:NAME:VALUE —— 写成 `string:名=值` 会被 notify-send 当场拒掉
        // （实测原话「Invalid hint syntax specified. Use TYPE:NAME:VALUE.」）。
        std::vector<std::string> argv = { "notify-send", "-a", "Agent Ping", "-t", "10000" };
        if (agent_id && *agent_id && session_id && *session_id) {
            argv.push_back("-h");
            argv.push_back("string:x-agentping-session:" + std::string(agent_id) + "/" + session_id);
        }
        argv.push_back(title ? title : "");
        argv.push_back(body ? body : "");
        rc = runWait(argv, 3000, &err);
    }
    if (rc == -2) {   // 守护进程没回执：本轮不算发过，账本不许记，下轮再试
        copyReason(reason, out_len, "notify-send 等 D-Bus 答复超时被杀，本轮不算发出");
        return 3;
    }
    std::string banner = !have_notify ? "本机没有 notify-send（需 libnotify-bin）"
                                      : "notify-send " + bannerErr(rc, err);
    if (rc == 0) { copyReason(reason, out_len, "横幅已投递（notify-send 退出码 0）"); return 0; }
    if (!want_sound) {
        copyReason(reason, out_len, banner + "；提示音在设置里关着，本轮不出声");
        return 2;
    }
    // 出声：玩家 + 音源都得真找到才算响。桌面音库没装（很多极简镜像没有）就明写。
    std::string player, file;
    if (!pickSound(player, file)) {
        copyReason(reason, out_len, banner + "；无声可出：没找到 paplay/pw-play/ffplay 或桌面音库文件");
        return 2;
    }
    std::string perr;
    int prc = runWait(soundArgv(player, file), 4000, &perr);
    if (prc == -2) {   // 播放器被超时杀掉：到底响没响不知道，记「没发过」让下轮再试
        copyReason(reason, out_len, banner + "；出声没在 4s 内回执（" + player + "），本轮不算发出");
        return 3;
    }
    copyReason(reason, out_len, banner + "；兜底出声：" + player + " 退出码 " +
               std::to_string(prc) + "（" + file + "）" +
               (prc == 0 || perr.empty() ? std::string() : "；平台原话：" + perr));
    return prc == 0 ? 1 : 2;
}

// 设置屏把「提示音」翻到开时当场响一声，用户不必等下一次提醒才知道开关真起作用。
// 走的是 ap_notify_post 兜底出声那同一条选择器（pickSound + soundArgv），
// 所以「测试响了」和「提醒会响」是同一件事，不是两套代码各自的运气。
extern "C" int ap_sound_test(char* reason, int out_len) {
    std::string player, file;
    if (!pickSound(player, file)) {
        copyReason(reason, out_len, "无声可出：没找到 paplay/pw-play/ffplay/cxplay/aplay，"
                                    "或桌面音库文件缺失（Debian 需 sound-theme-freedesktop）");
        return 1;
    }
    std::string perr;
    int prc = runWait(soundArgv(player, file), 4000, &perr);
    if (prc == -2) {
        copyReason(reason, out_len, player + " 没在 4s 内退出（声卡被占或守护进程没应答）");
        return 1;
    }
    if (prc != 0) {
        copyReason(reason, out_len, player + " 退出码 " + std::to_string(prc) +
                                    (perr.empty() ? std::string() : "；平台原话：" + perr));
        return 1;
    }
    copyReason(reason, out_len, "已出声：" + player + " " + file);
    return 0;
}
