// Linux：XDG Autostart 规范的那一个 .desktop 文件。
// 桌面环境在登录时扫 ~/.config/autostart/*.desktop 并执行 Exec。
// 文件存在即「已装」，不另存开关。$XDG_CONFIG_HOME 为空时按规范退到 ~/.config。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "autostart.h"

namespace {

std::string Home() {
    const char* h = getenv("HOME");
    return (h && *h) ? h : "";
}

std::string ConfigHome() {
    const char* c = getenv("XDG_CONFIG_HOME");
    if (c && *c) return c;
    const std::string h = Home();
    return h.empty() ? std::string() : h + "/.config";
}

std::string DesktopPath() {
    const std::string c = ConfigHome();
    return c.empty() ? std::string() : c + "/autostart/agentping.desktop";
}

std::string ExePath() {
    char buf[4096];
    ssize_t r = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (r <= 0) return {};
    buf[r] = 0;
    return buf;
}

// .desktop 的 Exec 里空格要引号包住；引号内的 " 与 \ 要转义。
std::string ExecQuote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '`' || c == '$') o += '\\';
        o += c;
    }
    return o + "\"";
}

void Say(char* reason, int n, const std::string& s) {
    if (!reason || n <= 0) return;
    snprintf(reason, (size_t)n, "%s", s.c_str());
}

bool Mkdirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (i < path.size()) cur += path[i];
    }
    return true;
}

}   // namespace

extern "C" int ap_autostart_get(char* where, int out_len) {
    const std::string p = DesktopPath();
    if (where && out_len > 0) snprintf(where, (size_t)out_len, "%s", p.c_str());
    if (p.empty()) return -1;
    return access(p.c_str(), F_OK) == 0 ? 1 : 0;
}

extern "C" int ap_autostart_set(int on, char* reason, int out_len) {
    const std::string p = DesktopPath();
    if (p.empty()) { Say(reason, out_len, "HOME / XDG_CONFIG_HOME 都是空的，定不了 autostart 路径"); return 1; }
    if (!on) {
        if (unlink(p.c_str()) != 0 && errno != ENOENT) {
            char e[512];
            snprintf(e, sizeof e, "删不掉 %s：%s", p.c_str(), strerror(errno));
            Say(reason, out_len, e);
            return 1;
        }
        Say(reason, out_len, "已删除 " + p);
        return 0;
    }
    const std::string exe = ExePath();
    if (exe.empty()) { Say(reason, out_len, "读不到 /proc/self/exe，写不了 autostart"); return 1; }
    std::string dir = p.substr(0, p.rfind('/'));
    if (!Mkdirs(dir)) {
        char e[512];
        snprintf(e, sizeof e, "建不了 %s：%s", dir.c_str(), strerror(errno));
        Say(reason, out_len, e);
        return 1;
    }
    const std::string body =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Agent Ping\n"
        "Comment=Menu-bar panel for local coding-agent sessions\n"
        "Exec=" + ExecQuote(exe) + "\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n";
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) {
        char e[512];
        snprintf(e, sizeof e, "写不了 %s：%s", p.c_str(), strerror(errno));
        Say(reason, out_len, e);
        return 1;
    }
    const size_t n = fwrite(body.data(), 1, body.size(), f);
    const bool ok = (n == body.size()) && fclose(f) == 0;
    if (!ok) {
        unlink(p.c_str());
        Say(reason, out_len, "写 " + p + " 时出错，已删掉半截文件");
        return 1;
    }
    Say(reason, out_len, "已写入 " + p + "（下次登录生效）");
    return 0;
}
