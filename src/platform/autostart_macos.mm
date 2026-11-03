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
    std::string p = realpath(buf, real) ? std::string(real) : std::string(buf);

    // brew 把每个版本装进 Cellar/<name>/<version>/，升级时旧目录整个删掉；
    // opt/<name> 是跟着版本走的软链。开机项要活过 brew upgrade，就得记 opt 那条。
    const std::string cellar = "/Cellar/";
    size_t at = p.find(cellar);
    if (at != std::string::npos) {
        size_t name_end = p.find('/', at + cellar.size());
        size_t ver_end = name_end == std::string::npos ? std::string::npos : p.find('/', name_end + 1);
        if (name_end != std::string::npos && ver_end != std::string::npos) {
            std::string name = p.substr(at + cellar.size(), name_end - (at + cellar.size()));
            p = p.substr(0, at) + "/opt/" + name + p.substr(ver_end);
        }
    }
    return p;
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

std::string ReadFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string out;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// 只认 ap_autostart_set 自己写出来的那个形状：ProgramArguments 之后第一个 <string>。
// 读不出（被人手改过、形状不认识）就返回空串，调用方按「不动」处理，不猜。
std::string RecordedExe(const std::string& xml) {
    size_t at = xml.find("<key>ProgramArguments</key>");
    if (at == std::string::npos) return {};
    at = xml.find("<string>", at);
    if (at == std::string::npos) return {};
    at += 8;
    size_t end = xml.find("</string>", at);
    if (end == std::string::npos || end == at) return {};
    std::string s = xml.substr(at, end - at);
    // XmlEscape 的反向：&amp; 必须最后解，否则 &amp;lt; 会被解成 <
    for (size_t i = 0; (i = s.find("&lt;", i)) != std::string::npos; i += 1) s.replace(i, 4, "<");
    for (size_t i = 0; (i = s.find("&gt;", i)) != std::string::npos; i += 1) s.replace(i, 4, ">");
    for (size_t i = 0; (i = s.find("&amp;", i)) != std::string::npos; i += 1) s.replace(i, 5, "&");
    return s;
}

// 马上要消失的副本：DMG 挂载点、Gatekeeper 的 App Translocation（在 /private/var/folders 下）。
// 从这些地方跑起来时不许把登录项改过去 —— 改完下次登录就是个死路径。
bool HasPrefix(const std::string& p, const char* pre) {
    return p.compare(0, strlen(pre), pre) == 0;
}

bool IsTransient(const std::string& p) {
    return HasPrefix(p, "/Volumes/") || HasPrefix(p, "/private/var/folders/") ||
           HasPrefix(p, "/private/tmp/");
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

extern "C" int ap_autostart_repair(char* reason, int out_len) {
    const std::string p = PlistPath();
    if (access(p.c_str(), F_OK) != 0) { Say(reason, out_len, ""); return 0; }   // 没开这个功能
    const std::string want = ExePath();
    if (want.empty()) {
        Say(reason, out_len, "读不到自己的可执行文件路径，对不上开机项");
        return -1;
    }
    if (IsTransient(want)) {
        Say(reason, out_len, "这份是从临时位置跑的（" + want + "），开机项不动");
        return 0;
    }
    const std::string rec = RecordedExe(ReadFile(p));
    if (rec.empty()) {
        Say(reason, out_len, "读不懂 " + p + " 里的路径（被手改过？），不动");
        return 0;
    }
    if (rec == want) {
        Say(reason, out_len, "已经指着在跑的这份 " + want + "，不用动");
        return 0;
    }
    if (ap_autostart_set(1, reason, out_len) != 0) return -1;
    std::string msg = "开机项原来指 " + rec + "，已改成本次在跑的 " + want;
    Say(reason, out_len, msg);
    return 1;
}
