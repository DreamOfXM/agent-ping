// 权益层的取证工具：不渲染像素、不点任何按钮，只把「本机到底读到了什么」摊开。
// 存在的理由：这一层的每一格都可能「本机根本没有」，而界面对外的说法是「状态未知」。
// 没有这个产物，那句话就只是我写的注释，谁也没法复核它今天是不是真的。
#include "claims.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {

/// 每个读数都同时给本地时刻与 UTC，方便和他那台机器的日志行对账。
std::string stamp(double epoch) {
    if (epoch <= 0) return "—";
    const time_t t = (time_t)epoch;
    struct tm lt{}, gt{};
    ::localtime_r(&t, &lt);
    ::gmtime_r(&t, &gt);
    char a[32], b[32];
    ::strftime(a, sizeof a, "%Y-%m-%d %H:%M:%S", &lt);
    ::strftime(b, sizeof b, "%H:%M:%SZ", &gt);
    return std::string(a) + " 本地 / " + b;
}

}  // namespace

int main(int argc, char** argv) {
    double now = 0;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--now") && i + 1 < argc) now = std::strtod(argv[++i], nullptr);
    }
    if (now <= 0) now = (double)std::time(nullptr);

    std::printf("取数时刻：%s\n\n", stamp(now).c_str());
    const auto cs = ap::claims_scan(now);
    std::fputs(ap::claims_dump(cs).c_str(), stdout);

    std::printf("\n逐条时刻展开：\n");
    for (const auto& c : cs) {
        std::printf("  %-10s seen=%s\n", c.vendor.c_str(), stamp(c.seen_at).c_str());
        std::printf("  %-10s appears=%s   expires=%s\n", "",
                    stamp(c.appears_at).c_str(), stamp(c.expires_at).c_str());
        std::printf("  %-10s last_ready=%s   服务端清单=%s\n", "",
                    stamp(c.last_ready_at).c_str(), stamp(c.list_at).c_str());
    }
    return cs.empty() ? 2 : 0;
}
