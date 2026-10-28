// macOS：LaunchAgent。文件存在即视为已装 —— launchd 在下次登录时读目录，
// 这里不调 launchctl bootstrap（那是「立刻开始跑」，与「登录时启动」不是一回事，
// 而且对已经从 Finder 双击开着的实例会再起一份）。
//
// HOME 优先读环境变量：测试要在沙盒 HOME 里跑完整安装/卸载，不许碰真实 ~/Library。
#import <Foundation/Foundation.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <mach-o/dyld.h>

#include "autostart.h"

namespace {

const char* kLabel = "io.github.dreamofxm.agentping";
const char* kPlistRel = "Library/LaunchAgents/io.github.dreamofxm.agentping.plist";

std::string Home() {
    const char* h = getenv("HOME");
    if (h && *h) return h;
    return [NSHomeDirectory() UTF8String];
}

std::string PlistPath() { return Home() + "/" + kPlistRel; }

std::string ExePath() {
    char buf[2048];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return {};
    char real[2048];
    return realpath(buf, real) ? std::string(real) : std::string(buf);
}

void Say(char* reason, int n, const std::string& s) {
    if (!reason || n <= 0) return;
    snprintf(reason, (size_t)n, "%s", s.c_str());
}

std::string XmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}

}   // namespace

extern "C" int ap_autostart_get(char* where, int out_len) {
    const std::string p = PlistPath();
    if (where && out_len > 0) snprintf(where, (size_t)out_len, "%s", p.c_str());
    return access(p.c_str(), F_OK) == 0 ? 1 : 0;
}

extern "C" int ap_autostart_set(int on, char* reason, int out_len) {
    const std::string p = PlistPath();
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
    if (exe.empty()) { Say(reason, out_len, "读不到自己的可执行文件路径，写不了 LaunchAgent"); return 1; }
    const std::string dir = Home() + "/Library/LaunchAgents";
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "%s", dir.c_str());
    // 先建目录（mkdir -p 的等价：只建这一层，父目录 ~/Library 一定在）
    if (mkdir(cmd, 0755) != 0 && errno != EEXIST) {
        char e[512];
        snprintf(e, sizeof e, "建不了 %s：%s", cmd, strerror(errno));
        Say(reason, out_len, e);
        return 1;
    }
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>Label</key><string>" + std::string(kLabel) + "</string>\n"
        "  <key>ProgramArguments</key>\n"
        "  <array><string>" + XmlEscape(exe) + "</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>ProcessType</key><string>Interactive</string>\n"
        "</dict>\n"
        "</plist>\n";
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) {
        char e[512];
        snprintf(e, sizeof e, "写不了 %s：%s", p.c_str(), strerror(errno));
        Say(reason, out_len, e);
        return 1;
    }
    const size_t n = fwrite(xml.data(), 1, xml.size(), f);
    const bool ok = (n == xml.size()) && fclose(f) == 0;
    if (!ok) {
        unlink(p.c_str());
        Say(reason, out_len, "写 " + p + " 时出错，已删掉半截文件");
        return 1;
    }
    Say(reason, out_len, "已写入 " + p + "（下次登录生效）");
    return 0;
}
