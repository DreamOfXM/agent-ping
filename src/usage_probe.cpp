// usage_probe：v8·M16 分模型日聚合的纯逻辑自测（不碰 GUI、不碰真状态目录）。
// 只验 scan 层不变量：分模型合计==工具总量、今天幂等重写、历史不回改、没装的源不写 0 行、
// 没四档的来源 has_parts=false（界面据此画「—」而不是编一个命中率）。
#include "scan.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) {                                                          \
            g_pass++;                                                        \
        } else {                                                             \
            g_fail++;                                                        \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                    \
    } while (0)

static long daykey(double e) {
    std::time_t t = (std::time_t)e;
    std::tm tv;
    localtime_r(&t, &tv);
    return (long)tv.tm_year * 10000 + (long)(tv.tm_mon + 1) * 100 + (long)tv.tm_mday;
}

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--selftest") != 0) {
        std::printf("usage: usage_probe --selftest\n");
        return 2;
    }
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base && *base ? base : ".") + "/ap_usage_probe_XXXXXX";
    char* dir = mkdtemp(tmpl.data());
    if (!dir) {
        std::printf("mkdtemp failed\n");
        return 2;
    }
    setenv("AGENTPING_STATE_DIR", dir, 1);
    const double now = (double)time(nullptr);
    std::time_t t0 = (std::time_t)now;
    std::tm tv0;
    localtime_r(&t0, &tv0);
    tv0.tm_hour = 0; tv0.tm_min = 0; tv0.tm_sec = 0;
    const double ws = (double)mktime(&tv0);
    const long today = daykey(ws);
    const long yest = daykey(ws - 86400);

    ap::Snapshot s;
    s.scanned_at = now;
    s.window_start = ws;
    ap::AgentSnap a;
    a.id = "claude"; a.name = "Claude"; a.available = true;
    ap::ModelUse m1;
    m1.name = "claude-sonnet-4-6"; m1.tokens = 1000;
    m1.parts.input = 600; m1.parts.output = 100; m1.parts.cache_read = 250; m1.parts.cache_write = 50;
    m1.sessions = 2; m1.has_parts = true;
    ap::ModelUse m2;
    m2.name = "claude-opus-4-6"; m2.tokens = 400;
    m2.parts.input = 200; m2.parts.output = 50; m2.parts.cache_read = 120; m2.parts.cache_write = 30;
    m2.sessions = 1; m2.has_parts = true;
    a.models.push_back(m1); a.models.push_back(m2);
    a.usage.input = 800; a.usage.output = 150; a.usage.cache_read = 370; a.usage.cache_write = 80;
    s.agents.push_back(a);
    ap::AgentSnap z;
    z.id = "zcode"; z.name = "ZCode"; z.available = true;
    ap::ModelUse zm;
    zm.name = "zcode-pro"; zm.tokens = 100; zm.has_parts = false; zm.sessions = 1;
    z.models.push_back(zm);
    z.usage.input = 100;
    s.agents.push_back(z);
    ap::AgentSnap absent;
    absent.id = "opencode"; absent.name = "OpenCode"; absent.available = false;
    s.agents.push_back(absent);
    s.today_tokens = 1500; s.today_credits = 0;

    ap::roll_daily(s);
    CHECK(s.model_daily.count(today) > 0);
    CHECK(s.model_daily[today].count("claude") > 0);
    CHECK(s.model_daily[today]["claude"].size() == 2);
    long sum = 0;
    for (const auto& kv : s.model_daily[today]["claude"]) sum += kv.second.tokens;
    CHECK(sum == 1400);                                   // 分模型合计 == 工具总量
    CHECK(s.model_daily[today]["claude"]["claude-sonnet-4-6"].sessions == 2);
    CHECK(s.model_daily[today]["claude"]["claude-sonnet-4-6"].has_parts == true);
    CHECK(s.model_daily[today]["zcode"]["zcode-pro"].has_parts == false);  // 没四档不编
    CHECK(s.model_daily[today].count("opencode") == 0);   // 没装的源不写 0 行
    bool found_today = false; long dt = 0;
    {
        std::ifstream f(std::string(dir) + "/daily.txt");
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream is(line);
            long d; std::string who; long tk; double cr;
            if (is >> d >> who >> tk >> cr)
                if (d == today && who == "*") { found_today = true; dt = tk; }
        }
    }
    CHECK(found_today && dt == 1500);
    // 历史不回改：手工塞一条昨天分模型行，再滚一轮，昨天应原样保留、今天幂等重写。
    {
        std::ofstream f(std::string(dir) + "/models_daily.txt", std::ios::app);
        f << yest << " claude old-model 50 0 10 5 30 5 1 1\n";
    }
    {
        std::ofstream f(std::string(dir) + "/daily.txt", std::ios::app);
        f << yest << " claude 50 0 10 5 30 5 -1 -1\n";
    }
    ap::roll_daily(s);
    CHECK(s.model_daily.count(yest) > 0);
    CHECK(s.model_daily[yest]["claude"]["old-model"].tokens == 50);
    CHECK(s.model_daily[today]["claude"].size() == 2);
    CHECK(s.model_daily[today].count("opencode") == 0);
    std::printf("usage_probe: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}