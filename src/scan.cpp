#include "scan.h"

#include "jsonl.h"
#ifdef __APPLE__
#include "platform/credits.h"
#endif

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#ifdef AP_HAVE_SQLITE
#include <sqlite3.h>
#endif

namespace ap {
namespace {

// 与 Swift 侧 Models.swift 同名常量同值，理由都写在那份文件的注释里。
const double kNeedsUserStale = 20 * 60;
const double kStalledToolUse = 90;
const int kDaysBack = 8;

std::string home() {
    const char* h = getenv("HOME");
    return (h && *h) ? std::string(h) : std::string("/");
}
std::string join(const std::string& a, const std::string& b) { return a + "/" + b; }

struct FStat {
    bool ok = false;
    bool dir = false;
    double mtime = 0;
    long long size = 0;
};
FStat fst(const std::string& p) {
    FStat s;
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return s;
    s.ok = true;
    s.dir = S_ISDIR(st.st_mode);
    s.mtime = (double)st.st_mtime;
    s.size = (long long)st.st_size;
    return s;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const char* n = e->d_name;
        if (n[0] == '.') continue;
        out.push_back(n);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

void walk(const std::string& dir, std::vector<std::string>& out, int depth = 0) {
    if (depth > 6) return;
    for (const std::string& n : list_dir(dir)) {
        std::string p = join(dir, n);
        FStat s = fst(p);
        if (!s.ok) continue;
        if (s.dir) walk(p, out, depth + 1);
        else out.push_back(p);
    }
}

double window_start(double now) {
    time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    lt.tm_hour = 0;
    lt.tm_min = 0;
    lt.tm_sec = 0;
    return (double)mktime(&lt);
}

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// 按码点裁剪，不按字节：会话标题里全是中文，按字节切会留下半个 UTF-8 序列。
std::string utf8_clip(const std::string& s, std::size_t max_cp, const char* ellipsis) {
    std::size_t cps = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (++cps > max_cp) return s.substr(0, i) + ellipsis;
        i += (std::size_t)len;
        if (i > s.size()) i = s.size();
    }
    return s;
}

std::size_t cp_count(const std::string& s) {
    std::size_t n = 0;
    for (char c : s) if (((unsigned char)c & 0xC0) != 0x80) n++;
    return n;
}

std::string first_line(const std::string& s) {
    std::size_t nl = s.find('\n');
    return trim(nl == std::string::npos ? s : s.substr(0, nl));
}

// ── 余额日志的公共读取件 ─────────────────────────────────────────────────────
// 这一层只读两家自己写盘的账户读数。两份日志的行首时间戳不是一支：ZCode 是不带 Z 的
// 本地时刻，Qoder 是带 Z 的 UTC —— 各用各的解析器，不混着用。
double parse_bracket_local(const std::string& line) {
    int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
    if (line.size() < 20 || line[0] != '[') return 0;
    if (std::sscanf(line.c_str(), "[%4d-%2d-%2d %2d:%2d:%2d", &y, &mo, &d, &hh, &mi, &ss) != 6)
        return 0;
    struct tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = hh; t.tm_min = mi; t.tm_sec = ss; t.tm_isdst = -1;
    const double r = (double)::mktime(&t);
    return r > 0 ? r : 0;
}
double parse_bracket_iso(const std::string& line) {
    int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
    if (line.size() < 20 || line[0] != '[') return 0;
    if (std::sscanf(line.c_str(), "[%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &hh, &mi, &ss) != 6)
        return 0;
    struct tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = hh; t.tm_min = mi; t.tm_sec = ss;
    return (double)::timegm(&t);
}

/// 从文件尾往前按 512 KB 一块扫，返回命中 needle 的整行，新的在前。
/// 面板每 3 秒扫一轮，cap 限住这一轮最多读多少。
std::vector<std::string> tail_lines(const std::string& path, const char* needle,
                                   std::size_t max_hits, std::size_t cap_bytes) {
    std::vector<std::string> out;
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return out;
    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) { ::close(fd); return out; }
    const std::size_t CH = 512 * 1024;
    off_t pos = st.st_size;
    std::size_t budget = cap_bytes ? cap_bytes : (std::size_t)st.st_size;
    std::string carry;
    while (pos > 0 && out.size() < max_hits && budget > 0) {
        std::size_t n = std::min<std::size_t>((std::size_t)pos, CH);
        if (n > budget) n = budget;
        budget -= n;
        const off_t off = pos - (off_t)n;
        std::string buf;
        buf.resize(n);
        const ssize_t got = ::pread(fd, &buf[0], n, off);
        if (got <= 0) break;
        buf.resize((std::size_t)got);
        pos = off;
        buf += carry;
        std::vector<std::string> parts;
        std::size_t s = 0;
        while (true) {
            const std::size_t nl = buf.find('\n', s);
            if (nl == std::string::npos) { parts.push_back(buf.substr(s)); break; }
            parts.push_back(buf.substr(s, nl - s));
            s = nl + 1;
        }
        if (pos > 0 && !parts.empty()) {
            carry = parts.front();
            parts.erase(parts.begin());
        } else {
            carry.clear();
        }
        for (auto it = parts.rbegin(); it != parts.rend() && out.size() < max_hits; ++it) {
            if (it->find(needle) != std::string::npos) out.push_back(std::move(*it));
        }
        if (got < (ssize_t)n) break;
    }
    ::close(fd);
    if (out.size() > max_hits) out.resize(max_hits);
    return out;
}

/// 数字字段既可能是 JSON 数，也可能是字符串（Codex 的 credits.balance 就是 "0"）。
long num_at(const Json* o, const char* k, bool* got) {
    const Json* v = o ? o->at(k) : nullptr;
    if (got) *got = false;
    if (!v) return 0;
    if (v->kind == Json::NUM) { if (got) *got = true; return (long)v->num; }
    if (v->kind == Json::STR && !v->str.empty()) {
        char* end = nullptr;
        const double n = std::strtod(v->str.c_str(), &end);
        if (end && end != v->str.c_str()) { if (got) *got = true; return (long)n; }
    }
    return 0;
}

/// 单位归一：token/tokens 一支、credit/credits 一支，其它原样。界面按归一号念。
std::string balance_unit(const std::string& raw) {
    std::string u;
    for (char ch : raw) u += (char)std::tolower((unsigned char)ch);
    if (u == "token" || u == "tokens") return "tokens";
    if (u == "credit" || u == "credits") return "Credits";
    return raw;
}

/// 余额读数按 (mtime, size) 缓存：日志没变就不重读。两份日志都很大，面板每 3 秒一轮。
struct BalanceCache {
    double mtime = -1, size = -1;
    Balance bal;
};
std::map<std::string, BalanceCache>& balance_cache() {
    static std::map<std::string, BalanceCache> m;
    return m;
}

// ── transcript（Qoder CLI 与 Claude Code 同构） ───────────────────────────────
struct Raw {
    double last_ts = 0;
    double first_ts = 0;     // 头部第一条时间戳：`.live` 的「连续」从这里起算
    bool has_voiced = false;
    bool voiced_user = false;
    std::string stop;
    bool errored = false;
    std::string error_text;
    int queue_live = 0;
    std::string log_title;   // customTitle / agentName
    std::string cwd;
    std::string prompt;      // 首条 user 正文首行
    std::string last_cmd;    // 尾部最后一条 tool_use 的真命令
    /// 尾部 tool_use 原文序列（≤10 条 (时刻, 首行)）：N1 循环检测的证据，只看尾部所以只会漏报。
    std::vector<std::pair<double, std::string>> tool_seq;
    int loop_count = 0;      // N1：尾部连续相同 tool_use 的条数（≥3 才非零）
    double loop_from = 0;    // N1：重复段第一条的时刻
    /// N2：重复段逐条证据（与 loop_count 同一段）。N3：今日窗 tool_use 聚合。
    std::vector<std::pair<double, std::string>> loop_events;
    std::vector<std::string> op_files, op_cmds;
    int op_files_more = 0, op_cmds_more = 0;
    long tokens = 0;
    double credits = 0;
    UsageParts usage;                  // 今天（或窗内）四档；tokens 与它同一口径
    std::string model;                 // 窗内用得最多的模型
    std::map<std::string, ModelUse> models;
    std::array<long, 24> hourly{};     // 本机 0–23 点
    std::array<double, 24> chourly{};  // 同小时的 Credits（Qoder 的 usage.credits 逐条有）
    long y_tokens = 0;      // 昨天同一个窗内的量，只喂表头的「较昨日」
    double y_credits = 0;
    std::array<long, 24> y_hourly{};     // 昨天的小时格
    std::array<double, 24> y_chourly{};  // 昨天的 Credits 小时格
    int turns = 0;          // 窗内人打出去的指令条数（排掉工具回执）
    /// 15 分钟一格的 assistant 记录数（键＝ts/900），供稿子的 Sparkline。
    /// 口径差：稿子数「所有带 timestamp 的 assistant 记录」，这里只能顺数带 usage 的那批
    /// （复用同一次回扫，不再多读一遍文件）；没有 usage 的是合成记录，本就极少。
    std::map<long, int> slots;
    std::vector<int> spark;   // slots 尾部 24 格展开，长度恒 24（有 slots 时）
};

struct Entry {
    double mtime = 0;
    long long size = 0;
    double since = 0;
    Raw raw;
};
std::map<std::string, Entry>& cache() {
    static std::map<std::string, Entry> c;
    return c;
}

std::string brief_error(const Json& v) {
    if (v.kind == Json::STR) return utf8_clip(v.str, 60, "");
    if (v.kind == Json::OBJ) {
        for (const char* k : {"type", "error", "message"})
            if (const std::string* s = v.str_at(k)) if (!s->empty()) return utf8_clip(*s, 60, "");
    }
    return "";
}

/// 读不出任何带时间戳的记录 = 这条会话没有可陈述的状态，整条不列。
bool transcript_raw(const std::string& path, const FStat& st, double since, Raw& out) {
    auto it = cache().find(path);
    if (it != cache().end() && it->second.mtime == st.mtime && it->second.size == st.size &&
        it->second.since == since) {
        out = it->second.raw;
        return out.last_ts > 0;
    }

    auto tail = tail_records(path);
    if (tail.empty()) return false;

    Raw r;
    const Json* last_voiced = nullptr;
    for (const Json& rec : tail) {
        if (const std::string* t = rec.str_at("customTitle")) if (!t->empty()) r.log_title = *t;
        if (const std::string* t = rec.str_at("agentName")) if (!t->empty()) r.log_title = *t;
        if (const std::string* c = rec.str_at("cwd")) if (!c->empty()) r.cwd = *c;
        double d = json_date_at(rec, "timestamp");
        if (d > r.last_ts) r.last_ts = d;

        const std::string* type = rec.str_at("type");
        if (type && (*type == "assistant" || *type == "user") && d > 0) last_voiced = &rec;

        // 队列口径：必须按文件行序累计，不能按 timestamp 排序（同毫秒会把 dequeue 排到
        // 它对应的 enqueue 之前）。这里只看尾部窗口，所以只会漏报、不会误报。
        if (type && *type == "queue-operation") {
            const std::string* op = rec.str_at("operation");
            if (op) {
                if (*op == "enqueue") r.queue_live++;
                else if (r.queue_live > 0) r.queue_live--;
            }
        }
        // `.live` 的 `$ cmd`：只认 tool_use 里真带出来的那一截原文。挑第一个非空的
        // command → file_path → pattern → description；都没有就不画这一行。
        const Json* msg = rec.at("message");
        const Json* content = msg ? msg->at("content") : nullptr;
        if (content && content->kind == Json::ARR) {
            for (const Json& blk : content->arr) {
                const std::string* bt = blk.str_at("type");
                if (!bt || *bt != "tool_use") continue;
                const Json* in = blk.at("input");
                if (!in) continue;
                std::string cmd;
                for (const char* k : {"command", "file_path", "pattern", "description"}) {
                    if (const std::string* v = in->str_at(k)) if (!v->empty()) { cmd = *v; break; }
                }
                if (!cmd.empty()) {
                    const std::string fl = first_line(cmd);
                    r.last_cmd = fl;
                    r.tool_seq.emplace_back(d, fl);
                    if (r.tool_seq.size() > 10) r.tool_seq.erase(r.tool_seq.begin());
                }
            }
        }
    }

    // N1 循环检测：只认「尾部连续相同」这一种形态（≥3 条），并把首条重复的时刻一并留下。
    // 中间断开过的不算——那是两次独立调用，不是死循环；宁可漏报不误报（与队列口径同精神）。
    {
        const auto& v = r.tool_seq;
        if (!v.empty()) {
            int n = 1;
            for (size_t i = v.size() - 1; i > 0 && v[i - 1].second == v.back().second; --i) ++n;
            if (n >= 3) {
                r.loop_count = n;
                r.loop_from = v[v.size() - (size_t)n].first;
                // N2：这一段的逐条证据原样带走，详情屏只摆事实不下判断。
                r.loop_events.assign(v.end() - n, v.end());
            }
        }
    }

    if (last_voiced) {
        r.has_voiced = true;
        const std::string* type = last_voiced->str_at("type");
        r.voiced_user = type && *type == "user";
        const Json* msg = last_voiced->at("message");
        if (msg) if (const std::string* s = msg->str_at("stop_reason")) r.stop = *s;
        const Json* api_err = last_voiced->at("isApiErrorMessage");
        const Json* err = last_voiced->at("error");
        if ((api_err && api_err->kind == Json::BOOL && api_err->boolean) ||
            (err && !err->is_null())) {
            r.errored = true;
            if (err) r.error_text = brief_error(*err);
        }
    }

    HeadFacts hf = head_facts(path);
    if (r.cwd.empty()) r.cwd = hf.cwd;
    r.prompt = first_line(hf.prompt);
    r.first_ts = hf.first_ts;

    // 一次回扫同时拿「今天」和「昨天」：窗起点只差一天，分两趟就把同一批字节读两遍。
    const double ysince = since - 86400;
    if (r.last_ts >= ysince) {
        scan_window(path, ysince, "\"usage\"", [&](const Json& rec, double ts) {
            const std::string* ty = rec.str_at("type");
            if (ty && *ty == "assistant" && ts > 0) r.slots[(long)(ts / 900)]++;
            const Json* msg = rec.at("message");
            const Json* u = msg ? msg->at("usage") : nullptr;
            if (!u) return;
            auto num = [&](const char* k) -> long {
                const Json* v = u->at(k);
                return (v && v->kind == Json::NUM) ? (long)v->num : 0;
            };
            UsageParts p;
            p.input = num("input_tokens");
            p.output = num("output_tokens");
            p.cache_read = num("cache_read_input_tokens");
            p.cache_write = num("cache_creation_input_tokens");
            const Json* c = u->at("credits");
            double cr = (c && c->kind == Json::NUM) ? c->num : 0;
            // 没有 timestamp 的记录（ts==0）留在今天这一侧，口径与改动前一致
            if (ts == 0 || ts >= since) {
                r.usage.input += p.input;
                r.usage.output += p.output;
                r.usage.cache_read += p.cache_read;
                r.usage.cache_write += p.cache_write;
                r.tokens += p.total();
                r.credits += cr;
                if (ts > 0) {
                    std::time_t tt = (std::time_t)ts;
                    std::tm tv;
                    localtime_r(&tt, &tv);
                    r.hourly[tv.tm_hour] += p.total();
                    r.chourly[tv.tm_hour] += cr;
                }
                if (msg) {
                    if (const std::string* m = msg->str_at("model")) {
                        if (!m->empty() && *m != "<synthetic>") {
                            // v8·M16：分模型四档／Credits 逐条累加（日志真给了 model+usage）。
                            ModelUse& mu = r.models[*m];
                            mu.name = *m;
                            mu.tokens += p.total();
                            mu.parts.input += p.input;
                            mu.parts.output += p.output;
                            mu.parts.cache_read += p.cache_read;
                            mu.parts.cache_write += p.cache_write;
                            mu.credits += cr;
                            mu.has_parts = true;
                        }
                    }
                }
            } else {
                r.y_tokens += p.total();
                r.y_credits += cr;
                if (ts > 0) {
                    std::time_t tt = (std::time_t)ts;
                    std::tm tv;
                    localtime_r(&tt, &tv);
                    r.y_hourly[tv.tm_hour] += p.total();
                    r.y_chourly[tv.tm_hour] += cr;
                }
            }
        });
    }
    // 轮数：一轮＝一条人打出来的 user 记录。工具回执也是 type=="user"，但它带
    // toolUseResult 键，得排掉（口径同 Swift 侧 TranscriptScan.swift:84）。
    if (r.last_ts >= since) {
        scan_window(path, since, "\"type\":\"user\"", [&](const Json& rec, double) {
            const std::string* type = rec.str_at("type");
            if (type && *type == "user" && !rec.has("toolUseResult")) r.turns++;
        });
    }

    // N3 操作记录（今日窗）：一趟 needle="tool_use" 的倒读回扫，聚「改过的文件」与「跑过的命令」。
    // 只为详情屏与接力简报供料，判态不碰它。命令按首行、连续相同压成一条（循环段 12 条额度
    // 不该被同一条命令吃光）；文件去重保序；超额如实计数，界面念「另有 N」。
    {
        std::set<std::string> fseen;
        r.op_cmds.reserve(14);
        scan_window(path, since, "\"tool_use\"", [&](const Json& rec, double) {
            const Json* msg = rec.at("message");
            const Json* content = msg ? msg->at("content") : nullptr;
            if (!content || content->kind != Json::ARR) return;
            for (const Json& blk : content->arr) {
                const std::string* bt = blk.str_at("type");
                if (!bt || *bt != "tool_use") continue;
                const Json* in = blk.at("input");
                if (!in) continue;
                if (const std::string* f = in->str_at("file_path")) {
                    if (!f->empty() && fseen.insert(*f).second) {
                        if ((int)r.op_files.size() < 12) r.op_files.push_back(*f);
                        else r.op_files_more++;
                    }
                }
                if (const std::string* c = in->str_at("command")) {
                    if (!c->empty()) {
                        const std::string fl = first_line(*c);
                        if (r.op_cmds.empty() || r.op_cmds.back() != fl) {
                            r.op_cmds.push_back(fl);
                            if ((int)r.op_cmds.size() > 12) {
                                r.op_cmds.erase(r.op_cmds.begin());
                                r.op_cmds_more++;
                            }
                        }
                    }
                }
            }
        });
    }

    // 稿子 collect.py:156：以「最后一条记录所在的格」为右端，往前取 24 格，空格补 0。
    if (!r.slots.empty()) {
        long hi = r.slots.rbegin()->first;
        for (long k = hi - 23; k <= hi; k++) {
            auto s = r.slots.find(k);
            r.spark.push_back(s == r.slots.end() ? 0 : s->second);
        }
    }
    long best = 0;
    for (const auto& kv : r.models)
        if (kv.second.tokens > best) { best = kv.second.tokens; r.model = kv.first; }

    out = r;
    cache()[path] = Entry{st.mtime, st.size, since, r};
    return out.last_ts > 0;
}

State derive(const Raw& r, double now, bool& inferred) {
    inferred = false;
    State state = ST_RESTING;
    if (r.has_voiced) {
        if (r.voiced_user) state = ST_WORKING;
        else if (r.stop == "end_turn" || r.stop == "stop_sequence") state = ST_NEEDS_USER;
        else if (r.stop == "tool_use" && now - r.last_ts >= kStalledToolUse) {
            // 工具发出去 90 秒没回：卡在「允许吗」上和真在跑长命令长得一模一样，
            // 所以只标疑似、不许响铃。
            state = ST_AWAIT;
            inferred = true;
        } else state = ST_WORKING;
    }
    if (r.errored) state = ST_FAILED;
    if (r.queue_live > 0 && state != ST_FAILED) { state = ST_QUEUED; inferred = false; }
    if (now - r.last_ts > kNeedsUserStale) state = ST_RESTING;
    return state;
}

AgentSnap scan_transcript(const std::string& id, const std::string& name, const std::string& root,
                          double now, double since, const std::map<std::string, std::string>& titled,
                          const std::set<std::string>& app_indexed,
                          long* y_tokens, double* y_credits) {
    AgentSnap snap;
    snap.id = id;
    snap.name = name;
    FStat rs = fst(root);
    if (!rs.ok || !rs.dir) return snap;      // available=false：本机没这个源，不算「今天没活动」
    snap.available = true;

    const double cutoff = now - kDaysBack * 86400;
    for (const std::string& dir : list_dir(root)) {
        std::string dpath = join(root, dir);
        FStat ds = fst(dpath);
        if (!ds.ok || !ds.dir) continue;
        for (const std::string& f : list_dir(dpath)) {
            if (f.size() < 7 || f.compare(f.size() - 6, 6, ".jsonl") != 0) continue;
            std::string path = join(dpath, f);
            FStat st = fst(path);
            if (!st.ok || st.mtime < cutoff) continue;
            Raw r;
            if (!transcript_raw(path, st, since, r)) continue;

            Session s;
            s.id = strip_ext(f);
            s.app_indexed = app_indexed.count(s.id) > 0;
            s.last_activity = r.last_ts;
            s.error_text = r.error_text;
            s.state = derive(r, now, s.inferred_wait);
            s.turns = r.turns;
            s.spark = r.spark;
            // 「连续」只在起点也落在今天时成立；跨夜就从今天零点起算，免得把昨天的
            // 时间算进「连续」。面板那边拿到的是秒数，画不画由它按 span_sec>0 判。
            s.first_activity = r.first_ts >= since ? r.first_ts : 0;
            s.last_cmd = r.last_cmd;
            s.loop_count = r.loop_count;
            s.loop_from = r.loop_from;
            s.loop_events = r.loop_events;
            s.op_files = r.op_files;
            s.op_cmds = r.op_cmds;
            s.op_files_more = r.op_files_more;
            s.op_cmds_more = r.op_cmds_more;
            s.first_prompt = r.prompt;
            s.queue_pos = (s.state == ST_QUEUED) ? r.queue_live : 0;
            s.tokens = r.last_ts >= since ? r.tokens : 0;
            s.credits = r.last_ts >= since ? r.credits : 0;
            if (r.last_ts >= since) {
                s.usage = r.usage;
                s.model = r.model;
                snap.usage.input += r.usage.input;
                snap.usage.output += r.usage.output;
                snap.usage.cache_read += r.usage.cache_read;
                snap.usage.cache_write += r.usage.cache_write;
                for (const auto& kv : r.models) {
                    bool found = false;
                    for (auto& m : snap.models)
                        if (m.name == kv.first) {
                            m.tokens += kv.second.tokens;
                            m.parts.input += kv.second.parts.input;
                            m.parts.output += kv.second.parts.output;
                            m.parts.cache_read += kv.second.parts.cache_read;
                            m.parts.cache_write += kv.second.parts.cache_write;
                            m.credits += kv.second.credits;
                            m.sessions += 1;
                            m.has_parts = m.has_parts || kv.second.has_parts;
                            found = true;
                            break;
                        }
                    if (!found) {
                        ModelUse mu = kv.second;
                        mu.sessions = 1;
                        snap.models.push_back(mu);
                    }
                }
                for (int h = 0; h < 24; h++) {
                    snap.hourly[h] += r.hourly[h];
                    snap.credits_hourly[h] += r.chourly[h];
                }
            }
            // 昨天的小时格独立于「今天有没有动静」：昨天跑过、今天没开的会话也要进昨天的柱图。
            for (int h = 0; h < 24; h++) {
                snap.yday_hourly[h] += r.y_hourly[h];
                snap.yday_credits_hourly[h] += r.y_chourly[h];
            }
            if (y_tokens) *y_tokens += r.y_tokens;
            if (y_credits) *y_credits += r.y_credits;
            s.project = r.cwd.empty() ? dir : basename_last(r.cwd);
            s.cwd = r.cwd;
            // 标题四级，从严到宽：① App 侧栏里他自己起的名字；② CLI 写进日志的
            // customTitle/agentName；③ 首条 user 正文前 40 字；④ 目录名。
            auto t = titled.find(s.id);
            if (t != titled.end()) s.title = t->second;
            else if (!r.log_title.empty()) s.title = r.log_title;
            else if (!r.prompt.empty()) s.title = utf8_clip(r.prompt, 40, "…");
            else s.title = s.project;
            snap.sessions.push_back(std::move(s));
        }
    }
    std::sort(snap.models.begin(), snap.models.end(),
              [](const ModelUse& a, const ModelUse& b) { return a.tokens > b.tokens; });
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    return snap;
}

// ── Codex：~/.codex/sessions/年/月/日/rollout-*.jsonl ─────────────────────────
std::string codex_human_prompt(const std::string& in) {
    std::string s = trim(in);
    if (s.empty() || s[0] == '<') return "";           // <environment_context> / <turn_aborted>
    const char* marks[] = {"## My request:", "PLEASE IMPLEMENT THIS PLAN:"};
    for (const char* m : marks) {
        std::size_t i = s.find(m);
        if (i != std::string::npos) { s = trim(s.substr(i + strlen(m))); break; }
    }
    if (s.rfind("# AGENTS.md", 0) == 0 || s.rfind("# Files mentioned by", 0) == 0) return "";
    s = first_line(s);
    std::size_t tag = s.find(" <");                     // 附件占位 <image name=…>
    if (tag != std::string::npos) s = s.substr(0, tag);
    while (!s.empty() && s[0] == '#') s = trim(s.substr(1));
    if (cp_count(s) < 2) return "";
    return utf8_clip(s, 48, "");
}

AgentSnap scan_codex(const std::string& name, const std::string& root, double now, double since) {
    AgentSnap snap;
    snap.id = "codex";
    snap.name = name;
    FStat rs = fst(root);
    if (!rs.ok || !rs.dir) return snap;
    snap.available = true;

    const double cutoff = now - kDaysBack * 86400;
    std::vector<std::string> files;
    walk(root, files);
    std::map<std::string, Session> latest;
    std::map<std::string, long> merged_tokens;
    std::map<std::string, UsageParts> merged_usage;

    for (const std::string& path : files) {
        std::string base = basename_last(path);
        if (base.rfind("rollout-", 0) != 0 || base.size() < 6 ||
            base.compare(base.size() - 6, 6, ".jsonl") != 0) continue;
        FStat st = fst(path);
        if (!st.ok || st.mtime < cutoff) continue;

        auto recs = tail_records(path, 4u << 20);      // rollout 实测很小，整份读
        if (recs.empty()) continue;

        std::vector<std::string> parts;
        std::string sid = base;
        {
            std::string stem = strip_ext(base);
            std::string cur;
            for (char ch : stem) {
                if (ch == '-') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(ch);
            }
            parts.push_back(cur);
            if (parts.size() >= 6) {
                sid = parts[parts.size() - 4];
                for (std::size_t k = parts.size() - 3; k < parts.size(); k++) sid += "-" + parts[k];
            }
        }

        double last_ts = 0, first_ts = 0;
        State outcome = ST_RESTING;
        bool have_outcome = false, failed = false;
        std::string project = "Codex", title, err, last_cmd, client;
        long tokens = 0;
        long prev_total = 0;
        UsageParts prev_parts, total_parts;
        std::array<long, 24> hourly{};
        std::map<std::string, ModelUse> models;
        std::string model;
        Quota quota;

        for (const Json& r : recs) {
            double d = json_date_at(r, "timestamp");
            if (d > last_ts) last_ts = d;
            if (d > 0 && (first_ts == 0 || d < first_ts)) first_ts = d;
            const Json* p = r.at("payload");
            if (!p) continue;
            const std::string* rtype = r.str_at("type");
            // session_meta 的 payload 没有 type 字段，必须在下面那道 guard 之前处理，
            // 否则 cwd 和真正的 session_id 永远读不到。
            if (rtype && *rtype == "session_meta") {
                if (const std::string* c = p->str_at("cwd")) if (!c->empty()) project = basename_last(*c);
                if (const std::string* s = p->str_at("session_id")) if (!s->empty()) sid = *s;
                // 来源标签：桌面 App 的铁证是 originator=="Codex Desktop"（实测 11-13：
                // TUI 会话也标 source=vscode，按 source 判会把终端会话错送桌面端——
                // 桌面端列表里没有它，codex:// 深链被静默吞掉，双击像坏了）。
                // 老会话没写 originator 时退回 source 口径。
                if (const std::string* og = p->str_at("originator"))
                    client = (*og == "Codex Desktop") ? "App" : "CLI";
                else if (const std::string* sc = p->str_at("source"))
                    client = (*sc == "cli" || sc->empty()) ? "CLI" : "App";
                continue;
            }
            const std::string* pt = p->str_at("type");
            if (!pt) continue;
            // `.live` 的 `$ cmd`：Codex 的 function_call 把参数放在 JSON 字符串里，
            // 只认 shell 调用的 command 那一格；解不开就跳过，不拿工具名顶替。
            if (rtype && *rtype == "response_item" && *pt == "function_call") {
                const std::string* args = p->str_at("arguments");
                Json aj;
                if (args && json_parse(args->c_str(), args->size(), aj)) {
                    if (const std::string* c = aj.str_at("command"))
                        if (!c->empty()) last_cmd = first_line(*c);
                }
            }
            if (*pt == "task_started") { outcome = ST_WORKING; have_outcome = true; }
            else if (*pt == "task_complete") {
                const Json* e = p->at("error");
                failed = e && !e->is_null();
                outcome = failed ? ST_FAILED : ST_NEEDS_USER;
                have_outcome = true;
                if (failed) err = brief_error(*e);
            } else if (*pt == "turn_context") {
                if (const std::string* m = p->str_at("model")) if (!m->empty()) model = *m;
            } else if (*pt == "token_count") {
                const Json* info = p->at("info");
                const Json* tot = info ? info->at("total_token_usage") : nullptr;
                if (!tot) tot = info;
                auto pick = [](const Json* o, const char* k) -> long {
                    const Json* v = o ? o->at(k) : nullptr;
                    return (v && v->kind == Json::NUM) ? (long)v->num : 0;
                };
                auto read_parts = [&](const Json* o) {
                    UsageParts p;
                    if (!o) return p;
                    // Codex 的 input_tokens 自带 cached；做差后才是「未被缓存的输入」。
                    const long raw_in = pick(o, "input_tokens");
                    p.cache_read = pick(o, "cached_input_tokens");
                    p.input = raw_in > p.cache_read ? raw_in - p.cache_read : 0;
                    p.cache_write = pick(o, "cache_write_input_tokens");
                    p.output = pick(o, "output_tokens");
                    return p;
                };
                UsageParts cur = read_parts(tot);
                UsageParts delta;
                if (cur.total() >= prev_parts.total()) {
                    delta.input = cur.input - prev_parts.input;
                    delta.output = cur.output - prev_parts.output;
                    delta.cache_read = cur.cache_read - prev_parts.cache_read;
                    delta.cache_write = cur.cache_write - prev_parts.cache_write;
                } else {
                    // 上下文被压缩过：这一轮的总量从头起算，用事件自带的增量。
                    delta = read_parts(info ? info->at("last_token_usage") : nullptr);
                }
                prev_parts = cur;
                if (d >= since && delta.total() > 0) {
                    total_parts.input += delta.input;
                    total_parts.output += delta.output;
                    total_parts.cache_read += delta.cache_read;
                    total_parts.cache_write += delta.cache_write;
                    std::time_t tt = (std::time_t)d;
                    std::tm tv;
                    localtime_r(&tt, &tv);
                    hourly[tv.tm_hour] += delta.total();
                }
                if (!model.empty() && d >= since && delta.total() > 0) {
                    // v8·M16：Codex 分模型四档（token_count 增量归到当前 turn_context 的 model）。
                    ModelUse& mu = models[model];
                    mu.name = model;
                    mu.tokens += delta.total();
                    mu.parts.input += delta.input;
                    mu.parts.output += delta.output;
                    mu.parts.cache_read += delta.cache_read;
                    mu.parts.cache_write += delta.cache_write;
                    mu.has_parts = true;
                }

                const Json* rl = p->at("rate_limits");
                if (rl) {
                    auto window = [&](const char* k, QuotaWindow& w) {
                        const Json* s = rl->at(k);
                        if (!s) return;
                        const Json* up = s->at("used_percent");
                        if (!up || up->kind != Json::NUM) return;
                        if (d <= w.seen_at) return;
                        w.known = true;
                        // Codex 的 used_percent 是 0–100 的数；内部统一存 0–1 的占比。
                        w.used = up->num / 100.0;
                        if (w.used < 0) w.used = 0;
                        if (w.used > 1) w.used = 1;
                        const Json* rs = s->at("resets_at");
                        w.resets_at = (rs && rs->kind == Json::NUM) ? rs->num : 0;
                        const Json* wm = s->at("window_minutes");
                        w.window_min = (wm && wm->kind == Json::NUM) ? (int)wm->num : 0;
                        w.seen_at = d;
                    };
                    window("primary", quota.five);
                    window("secondary", quota.week);
                    // 主窗口不一定是 5 小时（历史上有 reserve 档），按声明的分钟数归位。
                    if (quota.five.known && quota.five.window_min > 0 &&
                        quota.five.window_min != 300 && quota.five.window_min < 1440) {
                        quota.five.window_min = 300;   // 非 300 的短窗仍归「5 小时」一栏，只在说明里写实际分钟
                    }
                    if (const std::string* pl = rl->str_at("plan_type"))
                        // API-key 鉴权的 rate_limits 会给字面 "unknown"——照抄就是卡上一个
                        // 没有信息量的 unknown（11-13 用户实拍），按没写处理。
                        if (!pl->empty() && *pl != "unknown") quota.plan = *pl;
                    const Json* cr = rl->at("credits");
                    if (cr) {
                        // 2026-10-19 的 rollout 实测：balance 是字符串 "0"，且同一条带着
                        // has_credits:false / unlimited:false —— 没有积分这一档时它也给 0。
                        // 只有它说「有积分」或余额真大于 0 才摆这一格，避免把「不适用」念成「用光了」。
                        bool hc = false;
                        const Json* h = cr->at("has_credits");
                        if (h && h->kind == Json::BOOL && h->boolean) hc = true;
                        bool gb = false;
                        const long bal = num_at(cr, "balance", &gb);
                        if (gb && (hc || bal > 0)) {
                            quota.credits_balance = (double)bal;
                            quota.credits_known = true;
                        }
                    }
                    quota.known = quota.five.known || quota.week.known || quota.credits_known;
                }
            }
            if (rtype && *rtype == "response_item" && *pt == "message") {
                const std::string* role = p->str_at("role");
                if (role && *role == "user") {
                    std::string text;
                    const Json* content = p->at("content");
                    if (content && content->kind == Json::ARR) {
                        for (const Json& blk : content->arr)
                            if (const std::string* t = blk.str_at("text")) { if (!text.empty()) text += ' '; text += *t; }
                    }
                    std::string h = codex_human_prompt(text);
                    if (!h.empty()) title = h;         // 取最后一条人打的字
                }
            }
        }
        if (last_ts <= 0) continue;

        snap.usage.input += total_parts.input;
        snap.usage.output += total_parts.output;
        snap.usage.cache_read += total_parts.cache_read;
        snap.usage.cache_write += total_parts.cache_write;
        for (int h = 0; h < 24; h++) snap.hourly[h] += hourly[h];
        for (const auto& kv : models) {
            if (kv.second.tokens <= 0) continue;
            bool found = false;
            for (auto& m : snap.models)
                if (m.name == kv.first) {
                    m.tokens += kv.second.tokens;
                    m.parts.input += kv.second.parts.input;
                    m.parts.output += kv.second.parts.output;
                    m.parts.cache_read += kv.second.parts.cache_read;
                    m.parts.cache_write += kv.second.parts.cache_write;
                    m.sessions += 1;
                    m.has_parts = m.has_parts || kv.second.has_parts;
                    found = true;
                    break;
                }
            if (!found) {
                ModelUse mu = kv.second;
                mu.sessions = 1;
                snap.models.push_back(mu);
            }
        }
        if (quota.five.seen_at > snap.quota.five.seen_at) snap.quota.five = quota.five;
        if (quota.week.seen_at > snap.quota.week.seen_at) snap.quota.week = quota.week;
        if (quota.credits_known && !snap.quota.credits_known) {
            snap.quota.credits_balance = quota.credits_balance;
            snap.quota.credits_known = true;
        }
        if (!quota.plan.empty()) snap.quota.plan = quota.plan;


        long best = 0;
        std::string top_model;
        for (const auto& kv : models)
            if (kv.second.tokens > best) { best = kv.second.tokens; top_model = kv.first; }

        Session s;
        s.id = "codex-" + sid;
        s.client = client;
        s.project = project;
        s.title = title.empty() ? project : title;
        s.last_activity = last_ts;
        s.first_activity = (first_ts >= since) ? first_ts : 0;
        s.last_cmd = last_cmd;
        s.state = have_outcome ? outcome : ST_RESTING;
        s.error_text = err;
        s.tokens = total_parts.total();
        s.usage = total_parts;
        s.model = top_model;
        s.credits = 0;                                 // Codex 没有 credits 字段，不编
        auto it = latest.find(s.id);
        if (it == latest.end()) {
            merged_tokens[s.id] = s.tokens;
            merged_usage[s.id] = s.usage;
            latest[s.id] = std::move(s);
        } else {
            // 同一个 session_id 的多次 resume：状态与标题取最新那次，词元取并集。
            merged_tokens[s.id] += s.tokens;
            merged_usage[s.id].input += s.usage.input;
            merged_usage[s.id].output += s.usage.output;
            merged_usage[s.id].cache_read += s.usage.cache_read;
            merged_usage[s.id].cache_write += s.usage.cache_write;
            if (s.last_activity >= it->second.last_activity) {
                s.tokens = 0;
                s.usage = UsageParts{};
                it->second = std::move(s);
            }
        }
    }

    for (auto& kv : latest) {
        kv.second.tokens = merged_tokens[kv.first];
        kv.second.usage = merged_usage[kv.first];
        if (now - kv.second.last_activity > kNeedsUserStale) kv.second.state = ST_RESTING;
        snap.sessions.push_back(kv.second);
    }
    std::sort(snap.models.begin(), snap.models.end(),
              [](const ModelUse& a, const ModelUse& b) { return a.tokens > b.tokens; });
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    return snap;
}

// ── SQLite 两路（ZCode 的会话库、WorkBuddy 的会话库、Qoder 侧栏标题） ─────────
#ifdef AP_HAVE_SQLITE

std::map<std::string, std::string> sidebar_titles(const std::string& db_path) {
    std::map<std::string, std::string> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }
    sqlite3_busy_timeout(db, 250);
    sqlite3_exec(db, "PRAGMA query_only=1;", nullptr, nullptr, nullptr);
    // 归档/删除的不给名字：面板本来就不列它们，取到了也是白取。
    const char* sql =
        "select session_id, title from chat_sessions "
        "where title is not null and trim(title) != '' and coalesce(deleted_at, 0) = 0 limit 500;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char* sid = (const char*)sqlite3_column_text(st, 0);
            const char* t = (const char*)sqlite3_column_text(st, 1);
            if (sid && t) {
                std::string title = trim(t);
                if (!title.empty()) out[sid] = title;
            }
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

/// 桌面 App 会话库里在架的全部 session_id（不看标题）：侧栏那张表里有这一条，
/// 就说明这条会话确实是 App 里开的，界面才敢给它写「IDE」。
/// 与 sidebar_titles 分开查是因为标题为空的会话也在表里，用它判形态才不漏；
/// 上限放宽到 2000：判据漏一条就是把 App 的会话标成没形态，比多读几百行贵。
std::set<std::string> app_session_ids(const std::string& db_path) {
    std::set<std::string> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }
    sqlite3_busy_timeout(db, 250);
    sqlite3_exec(db, "PRAGMA query_only=1;", nullptr, nullptr, nullptr);
    const char* sql =
        "select session_id from chat_sessions "
        "where session_id is not null and coalesce(deleted_at, 0) = 0 limit 2000;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char* sid = (const char*)sqlite3_column_text(st, 0);
            if (sid && *sid) out.insert(trim(sid));
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

std::string col_text(sqlite3_stmt* st, int i) {
    const char* p = (const char*)sqlite3_column_text(st, i);
    return p ? std::string(p) : std::string();
}

/// 问句必须单行：一行里塞进三段换行会把 340pt 宽的弹层顶开。
std::string hook_one_line(const std::string& s, std::size_t max_cp) {
    std::string out, cur;
    std::vector<std::string> lines;
    for (char c : s) {
        if (c == '\n' || c == '\r') { lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    lines.push_back(cur);
    for (const std::string& ln : lines) {
        std::string t = trim(ln);
        if (t.empty()) continue;
        if (!out.empty()) out += " ";
        out += t;
    }
    return utf8_clip(out, max_cp, "");
}

/// 「CLI 亲口问的那一句」的公共形状：
///   {questions:[{question, header, multiSelect, options:[{label, description}]}]}
/// hook 信号把它挂在 details.input 下、ZCode 把它挂在 part.state.input 下，input 实测见过
/// 字典也见过写成 JSON 字符串，两种都接。返回 false = 这一份里没有 questions，
/// 调用方退回用自己的提示原文。
bool parse_question_block(const Json& box, const std::string* fallback, std::string& q,
                          std::vector<std::string>& opts, std::vector<std::string>& desc) {
    const Json* input = box.at("input");
    Json parsed;
    if (input && input->kind == Json::STR) {
        if (json_parse(input->str.c_str(), input->str.size(), parsed)) input = &parsed;
        else input = nullptr;
    } else if (input && input->kind != Json::OBJ) {
        input = nullptr;
    }
    const Json* qs = input ? input->at("questions") : nullptr;
    if (!qs || qs->kind != Json::ARR || qs->arr.empty()) {
        if (fallback) q = hook_one_line(*fallback, 160);
        return false;
    }
    const Json& first = qs->arr.front();
    if (const std::string* fq = first.str_at("question")) q = hook_one_line(*fq, 160);
    if (q.empty() && fallback) q = hook_one_line(*fallback, 160);
    const Json* os = first.at("options");
    if (os && os->kind == Json::ARR)
        for (std::size_t i = 0; i < os->arr.size() && opts.size() < 3; i++) {
            const std::string* lb = os->arr[i].str_at("label");
            if (!lb || lb->empty()) continue;
            opts.push_back(hook_one_line(*lb, 48));
            const std::string* ds = os->arr[i].str_at("description");
            desc.push_back(ds ? hook_one_line(*ds, 120) : std::string());
        }
    return true;
}

// ── 历史窗合计（昨天小时桶 + 账本缺日回填共用）────────────────────────
// 账本（daily.txt）只在 app 跑着的那天积累：上线首日/没开机的那天永远空着，
// 但每家的原始源都带时间戳，历史窗口问得到。柱图的「昨天」与 roll_daily 的缺日
// 回填都从这里取数。codex 不跟：它的会话词元要 resume 分段做差分，历史窗口径
// 不同（本机 codex 用量也≈0）；pi/opencode/qwenwork 同批跳过，如实「无记录」。
namespace {

struct HistSum {
    UsageParts parts;
    double credits = 0;
    std::array<long, 24> hourly{};
};

/// [d0,d1) 本地窗内 root/<proj>/*.jsonl 的用量合计。
/// subtract_cache=true：input 含缓存命中，进账前做差（WorkBuddy，同 1416 行口径）；
/// false = 纯输入（Claude/Qoder，同 402 行口径）。needle 与两处现行扫描一致。
void sum_jsonl_window(const std::string& root, double d0, double d1,
                      bool subtract_cache, HistSum& out) {
    FStat rs = fst(root);
    if (!rs.ok || !rs.dir) return;
    for (const std::string& dir : list_dir(root)) {
        std::string dpath = join(root, dir);
        FStat ds = fst(dpath);
        if (!ds.ok || !ds.dir) continue;
        for (const std::string& f : list_dir(dpath)) {
            if (f.size() < 7 || f.compare(f.size() - 6, 6, ".jsonl") != 0) continue;
            scan_window(join(dpath, f), d0, "\"usage\"", [&](const Json& rec, double ts) {
                if (!(ts >= d0 && ts < d1)) return;   // scan_window 只兜下界，上界自己滤
                const Json* msg = rec.at("message");
                const Json* u = msg ? msg->at("usage") : nullptr;
                if (!u) return;
                auto num = [&](const char* k) -> long {
                    const Json* v = u->at(k);
                    return (v && v->kind == Json::NUM) ? (long)v->num : 0;
                };
                UsageParts p;
                p.output = num("output_tokens");
                if (subtract_cache) {
                    p.cache_read = num("cache_read_input_tokens");
                    const long raw = num("input_tokens");
                    p.input = raw > p.cache_read ? raw - p.cache_read : 0;
                    p.cache_write = num("cache_write_input_tokens");
                } else {
                    p.input = num("input_tokens");
                    p.cache_read = num("cache_read_input_tokens");
                    p.cache_write = num("cache_creation_input_tokens");
                }
                const Json* c = u->at("credits");
                if (c && c->kind == Json::NUM) out.credits += c->num;
                out.parts.input += p.input;
                out.parts.output += p.output;
                out.parts.cache_read += p.cache_read;
                out.parts.cache_write += p.cache_write;
                std::time_t tt = (std::time_t)ts;
                std::tm tv;
                localtime_r(&tt, &tv);
                if (tv.tm_hour >= 0 && tv.tm_hour < 24) out.hourly[tv.tm_hour] += p.total();
            });
        }
    }
}

/// zcode 某本地窗 [d0,d1) 的四档+小时桶。tokens.input 实测含缓存命中，做差同
/// scan_zcode 今日两查；只算主会话（子代理完工会并回主会话，口径一致）。
bool zcode_day_sum(const std::string& db_path, double d0, double d1, HistSum& out) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 250);
    sqlite3_exec(db, "PRAGMA query_only=1;", nullptr, nullptr, nullptr);
    bool ok = false;
    {
        std::ostringstream q;
        q << "select cast(strftime('%H', m.time_created/1000,'unixepoch','localtime') as integer) h, "
             "sum(max(coalesce(json_extract(data,'$.tokens.input'),0)"
             " - coalesce(json_extract(data,'$.tokens.cache.read'),0), 0)), "
             "sum(coalesce(json_extract(data,'$.tokens.output'),0)), "
             "sum(coalesce(json_extract(data,'$.tokens.cache.read'),0)), "
             "sum(coalesce(json_extract(data,'$.tokens.cache.write'),0)), "
             "sum(max(coalesce(json_extract(data,'$.tokens.input'),0)"
             " - coalesce(json_extract(data,'$.tokens.cache.read'),0), 0)"
             " + coalesce(json_extract(data,'$.tokens.output'),0)"
             " + coalesce(json_extract(data,'$.tokens.cache.read'),0)"
             " + coalesce(json_extract(data,'$.tokens.cache.write'),0)) "
             "from message m join session s on m.session_id = s.id"
             " where coalesce(s.task_type,'') != 'subagent_child'"
             " and m.time_created >= " << (long long)(d0 * 1000)
          << " and m.time_created < " << (long long)(d1 * 1000) << " group by h;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK) {
            ok = true;
            while (sqlite3_step(st) == SQLITE_ROW) {
                const int h = (int)sqlite3_column_int64(st, 0);
                out.parts.input += (long)sqlite3_column_double(st, 1);
                out.parts.output += (long)sqlite3_column_double(st, 2);
                out.parts.cache_read += (long)sqlite3_column_double(st, 3);
                out.parts.cache_write += (long)sqlite3_column_double(st, 4);
                if (h >= 0 && h < 24) out.hourly[h] += (long)sqlite3_column_double(st, 5);
            }
            sqlite3_finalize(st);
        }
    }
    sqlite3_close(db);
    return ok;
}

/// 本地「今天零点 − i 天」的 [起点,终点) 与日期键。用 tm_mday 递减过 DST，不拿 86400 硬减。
void local_day_before(double now, int i, long& key, double& d0, double& d1) {
    std::time_t tt = (std::time_t)now;
    std::tm tv;
    localtime_r(&tt, &tv);
    tv.tm_hour = 0;
    tv.tm_min = 0;
    tv.tm_sec = 0;
    tv.tm_mday -= i;
    const std::time_t start = mktime(&tv);
    d0 = (double)start;
    d1 = d0 + 86400;
    // 与下方 day_key 同式（tm_year 是 126 这类偏移年，不 +1900）；day_key 定义在
    // roll_daily 附近，这里不能引用，只能同式复算。
    key = (long)tv.tm_year * 10000 + (long)(tv.tm_mon + 1) * 100 + (long)tv.tm_mday;
}

}  // namespace

// ── ZCode 5 小时/周双窗：App 的 usage-stats RPC 响应不落盘，只能走它自己的额度
// 接口（open.bigmodel.cn/api/monitor/usage/quota/limit）。鉴权用的 api-key 在
// credentials.json 里是 enc:v2:os: 加密的，密钥在登录钥匙串（条目「ZCode Credential
// Encryption」，账户=凭证路径指纹）——辅助脚本解开它发一次只读 GET。这是元数据接口，
// 不是模型调用，不耗额度。脚本自缓存 120 秒；失败打印 {} 且不写缓存，卡面如实。
namespace {
const char kZQuotaPy[] = R"PY(import json, os, time, base64, subprocess, sys, urllib.request
CACHE = '/tmp/agentping-zquota.json'
now = time.time()
try:
    c = json.load(open(CACHE))
    if now - c.get('ts', 0) < 120:
        c.pop('ts', None)
        print(json.dumps(c))
        sys.exit(0)
except Exception:
    pass
try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except Exception:
    print('{}')
    sys.exit(0)
CRED = os.path.expanduser('~/.zcode/v2/credentials.json')
ACCT = '07b75b561f6227d31d5fc0980b10d60b4c54c32de50e7bc0deb7e1934a4e8be1'
def kc():
    r = subprocess.run(['security', 'find-generic-password', '-s',
                        'ZCode Credential Encryption', '-a', ACCT, '-w'],
                       capture_output=True, text=True, timeout=10)
    if r.returncode != 0:
        raise RuntimeError('keychain')
    sec = r.stdout.strip()
    return base64.urlsafe_b64decode(sec + '=' * (-len(sec) % 4))
def dec(val, key):
    body = val[len('enc:v2:os:'):]
    fp, rest = body.split(':', 1)
    nb, tb, cb = rest.split('.')
    dec1 = lambda x: base64.urlsafe_b64decode(x + '=' * (-len(x) % 4))
    return AESGCM(key).decrypt(dec1(nb), dec1(cb) + dec1(tb), None).decode('utf8')
d = json.load(open(CRED))
aks = [k for k in d if 'api-key' in k]
if not aks:
    print('{}')
    sys.exit(0)
key = kc()
ak = dec(d[aks[0]], key)
req = urllib.request.Request('https://open.bigmodel.cn/api/monitor/usage/quota/limit',
                             headers={'Authorization': 'Bearer ' + ak,
                                      'Accept': 'application/json'})
j = json.loads(urllib.request.urlopen(req, timeout=12).read().decode('utf8'))
out = {}
for lim in (j.get('data') or {}).get('limits') or []:
    t, u, n = lim.get('type'), lim.get('unit'), lim.get('number')
    if t == 'CREDIT_LIMIT' and u == 3 and n == 5:
        out['five'] = {'pct': lim.get('percentage'),
                       'resets': (lim.get('nextResetTime') or 0) // 1000}
    elif t == 'CREDIT_LIMIT' and u == 6 and n == 1:
        out['week'] = {'pct': lim.get('percentage'),
                       'resets': (lim.get('nextResetTime') or 0) // 1000}
if not out:
    print('{}')
    sys.exit(0)
out['ts'] = now
json.dump(out, open(CACHE, 'w'))
out.pop('ts')
print(json.dumps(out))
)PY";
/// 脚本缺了/内容不同就重写（装到 /tmp：无持久副作用；失败轮重试成本低）。
void ensure_zquota_script() {
    const char* path = "/tmp/agentping-zquota.py";
    FILE* f = fopen(path, "rb");
    char probe[16] = {0};
    size_t got = f ? fread(probe, 1, sizeof probe - 1, f) : 0;
    if (f) fclose(f);
    if (got >= 15 && strncmp(probe, "import json, os", 15) == 0) return;
    f = fopen(path, "w");
    if (!f) return;
    fputs(kZQuotaPy, f);
    fclose(f);
    chmod(path, 0700);
}
}  // namespace

AgentSnap scan_zcode(const std::string& name, const std::string& db_path, double now, double since) {
    AgentSnap snap;
    snap.id = "zcode";
    snap.name = name;
    if (!fst(db_path).ok) return snap;
    snap.available = true;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        snap.scan_error = "打不开 db.sqlite（只读）";
        return snap;
    }
    sqlite3_busy_timeout(db, 250);
    sqlite3_exec(db, "PRAGMA query_only=1;", nullptr, nullptr, nullptr);

    // 窗内词元与写入时刻按 message 聚合；只看「最新一条 message」会把一整天的用量漏掉。
    struct Win { UsageParts parts; };
    std::map<std::string, Win> wins;
    {
        std::ostringstream q;
        // tokens.input 实测已含缓存命中（11-13 全窗抽查：没有任何一条 input < cache.read，
        // 与 Codex 同款口径，UsageParts 契约要求做差后进账——不差就把缓存读加了两遍，
        // 当日总量直接翻倍：2.09亿 vs Tokei 1.1亿）。max(...,0) 防个别行口径倒挂。
        q << "select session_id, "
             "sum(max(coalesce(json_extract(data,'$.tokens.input'), 0) "
             "- coalesce(json_extract(data,'$.tokens.cache.read'), 0), 0)), "
             "sum(coalesce(json_extract(data,'$.tokens.output'), 0)), "
             "sum(coalesce(json_extract(data,'$.tokens.cache.read'), 0)), "
             "sum(coalesce(json_extract(data,'$.tokens.cache.write'), 0)) "
             "from message where time_created >= " << (long long)(since * 1000) << " group by session_id;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK)
            while (sqlite3_step(st) == SQLITE_ROW) {
                Win w;
                w.parts.input = (long)sqlite3_column_double(st, 1);
                w.parts.output = (long)sqlite3_column_double(st, 2);
                w.parts.cache_read = (long)sqlite3_column_double(st, 3);
                w.parts.cache_write = (long)sqlite3_column_double(st, 4);
                wins[col_text(st, 0)] = w;
            }
        sqlite3_finalize(st);
    }
    // 按模型聚合（M16 模型视图的 zcode 份）：$.model 是对象 {providerID, modelID,
    // variant}，取 modelID；窗口、主会话、input 做差都同上面的口径。只留前 8。
    {
        std::ostringstream q;
        // 字段名两代并存：新行 modelId、旧行 modelID（11-13 实测），coalesce 兜住
        q << "select coalesce(json_extract(data,'$.modelId'), json_extract(data,'$.modelID'),'?') m, "
             "sum(max(coalesce(json_extract(data,'$.tokens.input'), 0) "
             "- coalesce(json_extract(data,'$.tokens.cache.read'), 0), 0)"
             " + coalesce(json_extract(data,'$.tokens.output'), 0)"
             " + coalesce(json_extract(data,'$.tokens.cache.read'),0)"
             " + coalesce(json_extract(data,'$.tokens.cache.write'),0)) t "
             "from message mj join session s on mj.session_id = s.id"
             " where coalesce(s.task_type,'') != 'subagent_child'"
             " and json_extract(mj.data,'$.role') = 'assistant'"   // user 行也带 tokens 但没有 modelID
             " and mj.time_created >= " << (long long)(since * 1000)
          << " group by m having t > 0 order by t desc limit 8;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const std::string m = col_text(st, 0);
                const long t = (long)sqlite3_column_double(st, 1);
                if (m.empty() || t <= 0) continue;
                snap.models.push_back({m, t});
            }
            sqlite3_finalize(st);
        }
    }
    // 按本机小时分桶：一条 group by 就够了，别为 24 格再读一遍表。
    {
        std::ostringstream q;
        // input 做差同上（含缓存命中）；小时桶加的必须是 UsageParts::total() 同一口径。
        q << "select cast(strftime('%H', time_created/1000, 'unixepoch', 'localtime') as integer) h, "
             "sum(max(coalesce(json_extract(data,'$.tokens.input'), 0) "
             "- coalesce(json_extract(data,'$.tokens.cache.read'), 0), 0) "
             "+ coalesce(json_extract(data,'$.tokens.output'), 0) "
             "+ coalesce(json_extract(data,'$.tokens.cache.read'), 0) "
             "+ coalesce(json_extract(data,'$.tokens.cache.write'), 0)) "
             "from message where time_created >= " << (long long)(since * 1000) << " group by h;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK)
            while (sqlite3_step(st) == SQLITE_ROW) {
                const int h = (int)sqlite3_column_int64(st, 0);
                if (h >= 0 && h < 24) snap.hourly[h] += (long)sqlite3_column_double(st, 1);
            }
        sqlite3_finalize(st);
    }

    // 轮次 = 这个会话真发出去的 user_prompt 条数（与 qoder 数「不带 toolUseResult 的
    // user 记录」同一口径）。App 的标题列不数这个，界面上的「第 N 轮」全靠它。
    std::map<std::string, int> turns;
    {
        std::ostringstream q;
        q << "select session_id, count(*) from message "
             "where json_extract(data,'$.semantics.kind') = 'user_prompt' group by session_id;";
        sqlite3_stmt* tst = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &tst, nullptr) == SQLITE_OK) {
            while (sqlite3_step(tst) == SQLITE_ROW) turns[col_text(tst, 0)] = sqlite3_column_int(tst, 1);
            sqlite3_finalize(tst);
        }
    }

    // sess_subagent_agent_* 是主会话派生的子代理，完工会并回主会话，单独列会把一张卡占满。
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "select s.id, s.title, s.directory, s.time_updated, s.time_created, "
        "(select m.data from message m where m.session_id = s.id "
        " and json_extract(m.data,'$.semantics.kind') = 'assistant_response' "
        " order by m.time_created desc, m.sequence desc limit 1) "
        "from session s where coalesce(s.task_type, '') != 'subagent_child' "
        "order by s.time_updated desc limit 60;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        snap.scan_error = std::string("查询失败：") + sqlite3_errmsg(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return snap;
    }
    const double cutoff_ms = (now - kDaysBack * 86400) * 1000;
    // 过窗作废要连同问句与候选一起撤：面板那排按钮只看「有没有候选」，
    // 留着就等于在一行「静置」底下摆出一排还能点的选项。
    auto stale_off = [&](Session& s) {
        if (now - s.last_activity <= kNeedsUserStale) return;
        if (s.state == ST_AWAIT) {
            s.question.clear(); s.options.clear(); s.option_desc.clear();
            s.tool.clear(); s.await_since = 0;
        }
        s.state = ST_RESTING;
    };
    // 回合未完的会话（snap.sessions 下标 + 未加前缀的 sid）：留到第二段去 part 表查
    // 「有没有一颗阻断工具正挂着」。
    std::vector<std::pair<std::size_t, std::string>> open_rows;
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::string sid = col_text(st, 0);
        std::string title = col_text(st, 1);
        std::string dir = col_text(st, 2);
        long long upd = sqlite3_column_int64(st, 3);
        long long cre = sqlite3_column_int64(st, 4);
        std::string data = col_text(st, 5);
        if ((double)upd < cutoff_ms) continue;

        Session s;
        s.id = "zcode-" + sid;
        s.await_src = QS_DB;
        s.title = title;
        s.turns = turns.count(sid) ? turns[sid] : 0;
        // 没绑项目的会话落在 ~/.zcode/workspace/default，取末级目录只会印出 "default"。
        s.project = (dir.empty() || dir.find("/.zcode/workspace/") != std::string::npos)
                        ? "未绑定项目" : basename_last(dir);
        if (s.title.empty()) s.title = s.project;
        s.cwd = dir;
        s.last_activity = (double)upd / 1000.0;
        s.created = (double)cre / 1000.0;
        s.state = ST_RESTING;
        auto w = wins.find(sid);
        if (w != wins.end()) {
            s.usage = w->second.parts;
            s.tokens = s.usage.total();
            snap.usage.input += s.usage.input;
            snap.usage.output += s.usage.output;
            snap.usage.cache_read += s.usage.cache_read;
            snap.usage.cache_write += s.usage.cache_write;
        }

        Json j;
        if (!data.empty() && json_parse(data.data(), data.size(), j)) {
            const Json* time = j.at("time");
            const Json* completed = time ? time->at("completed") : nullptr;
            // 只有 completed 会空（正在往屏幕上流的那条就是空），time 这个对象本身一直在，
            // 所以判活不许碰 time 整体。
            if (completed && completed->kind == Json::NUM && completed->num > 0)
                s.last_activity = completed->num / 1000.0;
            const Json* err = j.at("error");
            bool errored = err && err->kind == Json::OBJ;
            if (errored) { s.state = ST_FAILED; s.error_text = brief_error(*err); }
            else if (completed && completed->kind == Json::NUM && completed->num > 0) s.state = ST_NEEDS_USER;
            else s.state = ST_WORKING;
        }
        stale_off(s);
        // 回合未完的那些要再去 part 表查一颗（下面第二段）：那是「等待确认」唯一的正证。
        if (s.state == ST_WORKING) open_rows.emplace_back(snap.sessions.size(), sid);
        snap.sessions.push_back(std::move(s));
    }
    sqlite3_finalize(st);

    // 回合没完不等于「还在跑」：AskUserQuestion / ExitPlanMode 挂起时那颗 part 的
    // state.status 停在 running，而回合的 assistant_response 还没有 time.completed ——
    // 只看回合就把这一格读成「还在跑」，ZCode 侧栏写着「等待确认」我们却不响
    // （2026-11-04 用户实拍：真库里那条 10:28:38 落地的问题，面板判成 working）。
    // 归 ST_AWAIT 与 hook 那一路同判据（apply_hook_signals）：只有这一档才画引用块与候选项，
    // 界面把它念成「等待确认」，正好是 ZCode 侧栏那几个字。
    // 阻断工具按 ZCode 自己的分法取两类（AskUserQuestion / ExitPlanMode，后者在 zcode.cjs 里
    // 标了 needsApproval）；Bash 之类「在跑」与「等授权」在盘上同为 running，分不出来，
    // 所以不碰 —— 宁可少判，不拿不确定的形状去响铃。
    // 分两段而不是在主查询里挂相关子查询：后者 60 条会话实测 0.357s（冷扫 0.17s→0.45s），
    // 而这一刻回合未完的通常只有 0~2 条，按 IN 列表查同一张索引实测 10ms/1 条、0 条不查。
    if (!open_rows.empty()) {
        std::ostringstream pq;
        pq << "select session_id, time_created, data from part "
              "where json_extract(data,'$.type') = 'tool' "
              "and json_extract(data,'$.state.status') = 'running' "
              "and json_extract(data,'$.tool') in ('AskUserQuestion','ExitPlanMode') "
              "and session_id in (";
        for (std::size_t i = 0; i < open_rows.size(); i++) pq << (i ? ",?" : "?");
        pq << ") order by session_id, time_created desc;";
        sqlite3_stmt* pst = nullptr;
        if (sqlite3_prepare_v2(db, pq.str().c_str(), -1, &pst, nullptr) == SQLITE_OK) {
            for (std::size_t i = 0; i < open_rows.size(); i++)
                sqlite3_bind_text(pst, (int)i + 1, open_rows[i].second.c_str(), -1, SQLITE_TRANSIENT);
            std::set<std::string> seen;   // 每个会话只取最新那颗（order 已把最新排在前面）
            while (sqlite3_step(pst) == SQLITE_ROW) {
                const std::string sid2 = col_text(pst, 0);
                if (!seen.insert(sid2).second) continue;
                std::size_t idx = 0;
                bool hit = false;
                for (const auto& pr : open_rows)
                    if (pr.second == sid2) { idx = pr.first; hit = true; break; }
                if (!hit) continue;
                Session& s = snap.sessions[idx];
                Json part;
                const std::string data2 = col_text(pst, 2);
                if (!json_parse(data2.data(), data2.size(), part)) continue;
                const std::string* tn = part.str_at("tool");
                s.tool = tn ? *tn : std::string();
                const Json* st = part.at("state");
                if (st && st->kind == Json::OBJ)
                    parse_question_block(*st, nullptr, s.question, s.options, s.option_desc);
                s.await_since = sqlite3_column_int64(pst, 1) / 1000.0;
                s.state = ST_AWAIT;
                // 等待起点比会话行最后写入更晚时以它为准：stale_off 量的是 last_activity，
                // 用问题落地这一刻才不会刚问出口就判陈旧。
                if (s.await_since > s.last_activity) s.last_activity = s.await_since;
                stale_off(s);
            }
            sqlite3_finalize(pst);
        }
    }

    // 子代理对话（subagent_child）：主任务派出去的调研员/执行者，挂在 parent 下。
    // 上面主列表把它们滤掉了（单独列会占满面板），这里按归属收回——每个父会话最多
    // 带 8 条，够面板展开用。判态与主列表同一套：末条响应 completed 为空 = 还在跑。
    {
        const char* csql =
            "select s.parent_id, s.id, s.title, s.time_updated, "
            "(select m.data from message m where m.session_id = s.id "
            " and json_extract(m.data,'$.semantics.kind') = 'assistant_response' "
            " order by m.time_created desc, m.sequence desc limit 1) "
            "from session s where s.task_type = 'subagent_child' and s.time_deleted is null "
            "order by s.time_updated desc limit 300;";
        sqlite3_stmt* cst = nullptr;
        std::map<std::string, int> by_parent;   // 父 sid → snap.sessions 下标
        for (size_t i = 0; i < snap.sessions.size(); i++)
            if (snap.sessions[i].id.rfind("zcode-", 0) == 0)
                by_parent[snap.sessions[i].id.substr(6)] = (int)i;
        if (sqlite3_prepare_v2(db, csql, -1, &cst, nullptr) == SQLITE_OK) {
            while (sqlite3_step(cst) == SQLITE_ROW) {
                auto it = by_parent.find(col_text(cst, 0));
                if (it == by_parent.end()) continue;
                Session& parent = snap.sessions[(size_t)it->second];
                if (parent.children.size() >= 8) continue;
                Session::Child c;
                c.id = col_text(cst, 1);
                c.title = col_text(cst, 2);
                c.updated = sqlite3_column_int64(cst, 3) / 1000.0;
                Json j;
                const std::string data = col_text(cst, 4);
                c.running = true;
                if (!data.empty() && json_parse(data.data(), data.size(), j)) {
                    const Json* time = j.at("time");
                    const Json* completed = time ? time->at("completed") : nullptr;
                    if (completed && completed->kind == Json::NUM && completed->num > 0) {
                        c.running = false;
                        c.updated = completed->num / 1000.0;
                    }
                }
                if (now - c.updated > 1800.0) c.running = false;   // 半小时没动静就别装活跃
                if (c.title.empty()) c.title = "(子任务)";
                parent.children.push_back(std::move(c));
            }
            sqlite3_finalize(cst);
        }
        if (getenv("AGENTPING_SCAN_TRACE")) {
            int n = 0;
            for (const auto& se : snap.sessions) n += (int)se.children.size();
            fprintf(stderr, "[scan] zcode 会话 %zu 条，挂上子对话 %d 条\n", snap.sessions.size(), n);
        }
    }
    sqlite3_close(db);
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    // 昨天（本地日）四档与小时桶：「昨天 · 按小时」柱图和昨日小计的 zcode 份。
    // 每轮都查（一条 group by，ms 级）；账本里昨天的行由 roll_daily 的缺日回填写。
    {
        long key = 0;
        double d0 = 0, d1 = 0;
        local_day_before(now, 1, key, d0, d1);
        HistSum ys;
        if (zcode_day_sum(db_path, d0, d1, ys)) {
            snap.yday_tokens = ys.parts.total();
            snap.yday_credits = ys.credits;
            snap.yday_hourly = ys.hourly;
        }
    }
#ifdef __APPLE__
    // 5 小时/周双窗（v8 补）：App 面板的这两窗走额度接口，RPC 响应不落盘——
    // 这里用 credentials.json 里加密的 api-key（钥匙串密钥解开）发只读 GET。
    // 读不到就不动 five/week，卡面上这两格如实不出现。
    ensure_zquota_script();
    {
        FILE* qp = popen("python3 /tmp/agentping-zquota.py 2>/dev/null", "r");
        if (qp) {
            char qb[512] = {0};
            size_t qn = fread(qb, 1, sizeof qb - 1, qp);
            pclose(qp);
            Json qj;
            if (qn > 0 && json_parse(qb, (int)qn, qj)) {
                auto fill = [&](const char* key, QuotaWindow& w, int wmin) {
                    const Json* p = qj.at(key);
                    if (!p) return;
                    const Json* pct = p->at("pct");
                    const Json* rs = p->at("resets");
                    if (!pct || pct->kind != Json::NUM || !rs || rs->kind != Json::NUM) return;
                    w.known = true;
                    w.used = std::fmax(0.0, std::fmin(1.0, (100.0 - pct->num) / 100.0));
                    w.resets_at = rs->num;
                    w.window_min = wmin;
                    w.seen_at = now;
                };
                fill("five", snap.quota.five, 300);
                fill("week", snap.quota.week, 7 * 1440);
            }
        }
    }
#endif

    return snap;
}

/// 日志尾部 → 状态。返回 false = 尾部读不出「谁的话」，调用方保留库里的判据。
bool workbuddy_judge(const std::vector<Json>& tail, double now, Session& s) {
    std::vector<const Json*> recs;
    for (const Json& r : tail) if (r.str_at("type")) recs.push_back(&r);
    if (recs.empty()) return false;

    std::vector<std::string> open_calls;               // 出现过、还没配上回执的工具调用
    for (const Json* r : recs) {
        const std::string* type = r->str_at("type");
        const std::string* cid = r->str_at("callId");
        if (!type || !cid || cid->empty()) continue;
        if (*type == "function_call") open_calls.push_back(*cid);
        else if (*type == "function_call_result")
            open_calls.erase(std::remove(open_calls.begin(), open_calls.end(), *cid), open_calls.end());
    }
    const Json& last = *recs.back();
    const std::string* type = last.str_at("type");
    double stamp = json_date_at(last, "timestamp");
    double age = stamp > 0 ? now - stamp : 1e18;

    const Json* err = last.at("error");
    if (err && !err->is_null()) { s.state = ST_FAILED; s.error_text = brief_error(*err); return true; }
    if (type && *type == "function_call" && !open_calls.empty()) {
        // 这一档的等待证据是日志尾部的 stalled tool_use，不是库里的会话行；不改来源
        // 标注，详情屏就会把「反推出来的」念成「会话库 · 本机」。
        if (age >= kStalledToolUse) { s.state = ST_AWAIT; s.inferred_wait = true; s.await_src = QS_LOG; s.await_since = stamp; }
        else s.state = ST_WORKING;
        return true;
    }
    if (type && *type == "message") {
        const std::string* role = last.str_at("role");
        const std::string* status = last.str_at("status");
        s.state = (role && *role == "assistant" && status && *status == "completed") ? ST_NEEDS_USER : ST_WORKING;
        return true;
    }
    if (type && (*type == "reasoning" || *type == "function_call_result" || *type == "file-history-snapshot")) {
        s.state = ST_WORKING;
        return true;
    }
    return false;
}

AgentSnap scan_workbuddy(const std::string& name, const std::string& db_path,
                         const std::string& projects_root, double now, double since) {
    AgentSnap snap;
    snap.id = "workbuddy";
    snap.name = name;
    if (!fst(db_path).ok) return snap;
    snap.available = true;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        snap.scan_error = "打不开 workbuddy.db（只读）";
        return snap;
    }
    sqlite3_busy_timeout(db, 250);
    sqlite3_exec(db, "PRAGMA query_only=1;", nullptr, nullptr, nullptr);

    // sessionId → 日志文件。目录名是 cwd 把 / 换成 -，但不靠这个反推：直接按文件名认。
    std::map<std::string, std::string> files;
    for (const std::string& dir : list_dir(projects_root)) {
        std::string dpath = join(projects_root, dir);
        if (!fst(dpath).dir) continue;
        for (const std::string& f : list_dir(dpath)) {
            if (f.size() < 7 || f.compare(f.size() - 6, 6, ".jsonl") != 0) continue;
            files[strip_ext(f)] = join(dpath, f);
        }
    }

    std::ostringstream q;
    q << "select id, coalesce(custom_title, title, ''), coalesce(cwd, ''), status, "
         "max(coalesce(last_activity_at, 0), coalesce(updated_at, 0)) from sessions "
         "where coalesce(deleted_at, 0) = 0 and max(coalesce(last_activity_at, 0), "
         "coalesce(updated_at, 0)) >= " << (long long)((now - kDaysBack * 86400) * 1000)
      << " order by 5 desc limit 60;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) != SQLITE_OK) {
        snap.scan_error = std::string("查询失败：") + sqlite3_errmsg(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return snap;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::string sid = col_text(st, 0);
        std::string title = trim(col_text(st, 1));
        std::string cwd = col_text(st, 2);
        std::string status = col_text(st, 3);
        long long ms = sqlite3_column_int64(st, 4);
        if (ms <= 0) continue;

        Session s;
        s.id = "workbuddy-" + sid;
        s.await_src = QS_DB;
        s.cwd = cwd;
        // 工作目录名就是项目名；但「未绑定项目」那类是它自己按时间戳建的临时空间，
        // 把时间戳摆在行上等于没说话，所以这类只报父目录。
        std::string base = basename_last(cwd);
        bool generated = false;
        {
            auto digit = [](char c) { return c >= '0' && c <= '9'; };
            generated = base.size() >= 5 && digit(base[0]) && digit(base[1]) && digit(base[2]) && digit(base[3]);
            if (!generated) generated = base.rfind("automation-20", 0) == 0;
        }
        s.project = generated ? basename_last(cwd.substr(0, cwd.size() - base.size() - 1)) : base;
        if (s.project.empty()) s.project = "未绑定项目";
        s.title = title.empty() ? (s.project.empty() ? name : s.project) : title;
        s.last_activity = (double)ms / 1000.0;
        bool archived = status == "archived";
        s.state = archived ? ST_RESTING : ST_NEEDS_USER;

        auto it = files.find(sid);
        if (it != files.end()) {
            auto tail = tail_records(it->second);
            for (const Json& r : tail) {
                double d = json_date_at(r, "timestamp");
                if (d > s.last_activity) s.last_activity = d;
            }
            if (!archived) workbuddy_judge(tail, now, s);
            if (s.last_activity >= since) {
                scan_window(it->second, since, "\"usage\"", [&](const Json& rec, double ts) {
                    // 读数在 message.usage（snake_case）。providerData.usage 是同一份数的
                    // camelCase 镜像（inputTokens…），拿 snake_case 的键去读它只会全零。
                    const Json* msg = rec.at("message");
                    const Json* u = msg ? msg->at("usage") : nullptr;
                    if (!u) return;
                    const Json* pd = rec.at("providerData");
                    auto num = [&](const Json* o, const char* k) -> long {
                        const Json* v = o ? o->at(k) : nullptr;
                        return (v && v->kind == Json::NUM) ? (long)v->num : 0;
                    };
                    UsageParts p;
                    // input_tokens 实测已含缓存命中，不能再加一遍
                    p.cache_read = num(u, "cache_read_input_tokens");
                    const long raw_in = num(u, "input_tokens");
                    p.input = raw_in > p.cache_read ? raw_in - p.cache_read : 0;
                    p.output = num(u, "output_tokens");
                    p.cache_write = num(u, "cache_write_input_tokens");
                    s.usage.input += p.input;
                    s.usage.output += p.output;
                    s.usage.cache_read += p.cache_read;
                    s.usage.cache_write += p.cache_write;
                    s.tokens += p.total();
                    if (ts > 0) {
                        std::time_t tt = (std::time_t)ts;
                        std::tm tv;
                        localtime_r(&tt, &tv);
                        snap.hourly[tv.tm_hour] += p.total();
                    }
                    if (pd) {
                        if (const std::string* m = pd->str_at("model")) {
                            if (!m->empty()) {
                                bool found = false;
                                for (auto& mu : snap.models)
                                    if (mu.name == *m) { mu.tokens += p.total(); found = true; break; }
                                if (!found) snap.models.push_back({*m, p.total()});
                                if (s.model.empty() || p.total() > 0) s.model = *m;
                            }
                        }
                    }
                });
            }
            if (now - s.last_activity > kNeedsUserStale && s.state != ST_FAILED) {
                s.state = ST_RESTING;
                s.inferred_wait = false;
            }
        }
        snap.usage.input += s.usage.input;
        snap.usage.output += s.usage.output;
        snap.usage.cache_read += s.usage.cache_read;
        snap.usage.cache_write += s.usage.cache_write;
        snap.sessions.push_back(std::move(s));
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    // 昨天窗（本地日）：「昨天 · 按小时」柱图的 workbuddy 份；input 含缓存，做差同上。
    {
        long key = 0;
        double d0 = 0, d1 = 0;
        local_day_before(now, 1, key, d0, d1);
        HistSum ys;
        sum_jsonl_window(projects_root, d0, d1, true, ys);
        snap.yday_tokens = ys.parts.total();
        snap.yday_credits = ys.credits;
        snap.yday_hourly = ys.hourly;
    }
    return snap;
}

/// sqlite_master 里有没有这张表。OpenCode 2 起会话表换成 session_v2/session_message，
/// 旧库还留着 session/message/part；两套并存，不能只按版本号猜。
bool table_exists(sqlite3* db, const char* name) {
    sqlite3_stmt* st = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "select 1 from sqlite_master where type='table' and name=? limit 1",
                           -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        ok = (sqlite3_step(st) == SQLITE_ROW);
    }
    sqlite3_finalize(st);
    return ok;
}

/// OpenCode（开源 CLI，macOS/Linux 同一个路径）：~/.local/share/opencode/opencode.db。
/// 旧版把用量按轮记在 part 的 step-finish 里，窗内词元能从源头按天切；OpenCode 2
/// 换成 session_v2 的累计列 + session_message 消息流，没有逐轮 token 行，只能分清
/// 「今天新建的会话」（累计值全是今天的）与「跨天的会话」（不拿累计冒充今天）。
AgentSnap scan_opencode(const std::string& name, const std::string& db_path, double now, double since) {
    AgentSnap snap;
    snap.id = "opencode";
    snap.name = name;
    if (!fst(db_path).ok) return snap;
    snap.available = true;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        snap.scan_error = "打不开 opencode.db（只读）";
        return snap;
    }
    sqlite3_busy_timeout(db, 250);
    sqlite3_exec(db, "PRAGMA query_only=1;", nullptr, nullptr, nullptr);

    const long long cutoff_ms = (long long)((now - kDaysBack * 86400) * 1000);
    const long long since_ms = (long long)(since * 1000);
    std::map<std::string, Session> merged;   // 裸 session id → 行；两套 schema 同一个 id 只留一条
    // 按本机小时分桶；旧 schema 的用量在 part 上，message 那一侧没有 tokens。
    {
        std::ostringstream q;
        q << "select cast(strftime('%H', p.time_created/1000, 'unixepoch', 'localtime') as integer) h, "
             "sum(coalesce(json_extract(p.data,'$.tokens.input'), 0) "
             "+ coalesce(json_extract(p.data,'$.tokens.output'), 0) "
             "+ coalesce(json_extract(p.data,'$.tokens.cache.read'), 0) "
             "+ coalesce(json_extract(p.data,'$.tokens.cache.write'), 0)) "
             "from part p where p.time_created >= " << since_ms
          << " and json_extract(p.data,'$.type') = 'step-finish' group by h;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK)
            while (sqlite3_step(st) == SQLITE_ROW) {
                const int h = (int)sqlite3_column_int64(st, 0);
                if (h >= 0 && h < 24) snap.hourly[h] += (long)sqlite3_column_double(st, 1);
            }
        sqlite3_finalize(st);
    }
    // 一条语句拿齐一行要的东西：part 有 session_id 索引，60 条相关子查询量不出来。
    {
        std::ostringstream q;
        q << "select s.id, coalesce(s.title, ''), coalesce(s.directory, ''), s.time_updated, "
             "(select count(*) from part p where p.session_id = s.id "
             " and json_extract(p.data,'$.state.status') = 'running'), "
             "(select ifnull(sum(json_extract(p.data,'$.tokens.input') "
             " + json_extract(p.data,'$.tokens.cache.read') "
             " + json_extract(p.data,'$.tokens.cache.write')), 0) from part p "
             " where p.session_id = s.id and json_extract(p.data,'$.type') = 'step-finish' "
             " and p.time_created >= " << since_ms << "), "
             "(select count(*) from message m where m.session_id = s.id "
             " and json_extract(m.data,'$.role') = 'user' and m.time_created >= " << since_ms << "), "
             "(select p.data from part p where p.session_id = s.id "
             " and json_extract(p.data,'$.type') = 'tool' "
             " order by p.time_created desc, p.id desc limit 1), "
             "(select json_object('i', ifnull(sum(json_extract(p.data,'$.tokens.input')), 0), "
             " 'o', ifnull(sum(json_extract(p.data,'$.tokens.output')), 0), "
             " 'r', ifnull(sum(json_extract(p.data,'$.tokens.cache.read')), 0), "
             " 'w', ifnull(sum(json_extract(p.data,'$.tokens.cache.write')), 0)) from part p "
             " where p.session_id = s.id and json_extract(p.data,'$.type') = 'step-finish' "
             " and p.time_created >= " << since_ms << ") "
             "from session s where coalesce(s.time_archived, 0) = 0 "
             "and s.time_updated >= " << cutoff_ms << " order by s.time_updated desc limit 60;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                std::string sid = col_text(st, 0);
                std::string title = trim(col_text(st, 1));
                std::string dir = col_text(st, 2);
                long long upd = sqlite3_column_int64(st, 3);
                int running = (int)sqlite3_column_int64(st, 4);
                if (upd <= 0) continue;

                Session s;
                s.id = "opencode-" + sid;
                s.await_src = QS_DB;
                s.title = title;
                s.project = basename_last(dir);
                if (s.project.empty()) s.project = "未绑定项目";
                if (s.title.empty()) s.title = s.project;
                s.cwd = dir;
                s.last_activity = (double)upd / 1000.0;
                // 四档分项在第 8 列；旧的第 5 列不含 output，只作为解析失败时的兜底。
                std::string parts_json = col_text(st, 8);
                Json pj;
                if (!parts_json.empty() && json_parse(parts_json.data(), parts_json.size(), pj)) {
                    auto pnum = [&](const char* k) -> long {
                        const Json* v = pj.at(k);
                        return (v && v->kind == Json::NUM) ? (long)v->num : 0;
                    };
                    s.usage.input = pnum("i");
                    s.usage.output = pnum("o");
                    s.usage.cache_read = pnum("r");
                    s.usage.cache_write = pnum("w");
                }
                s.tokens = s.usage.total();
                if (s.tokens == 0) s.tokens = (long)sqlite3_column_int64(st, 5);
                s.turns = (int)sqlite3_column_int64(st, 6);
                // 库里没有 credits 这一档（cost 是美元），而表头那格是 cr，不混。
                s.credits = 0;

                // 判活：它自己把在飞的工具调用写成 state.status='running'，这是本机唯一能
                // 直接读到的「正在跑」证据；没有 running 件就看最后一颗工具调用的回执。
                s.state = running > 0 ? ST_WORKING : ST_NEEDS_USER;
                std::string data = col_text(st, 7);
                Json j;
                if (!data.empty() && json_parse(data.data(), data.size(), j)) {
                    const Json* state = j.at("state");
                    const std::string* status = state ? state->str_at("status") : nullptr;
                    if (status && *status == "error") {
                        s.state = ST_FAILED;
                        const Json* err = state->at("error");
                        s.error_text = err ? brief_error(*err) : "工具调用报错";
                    } else if (status && *status == "running") {
                        s.state = ST_WORKING;
                    }
                }
                merged[sid] = std::move(s);
            }
        }
        sqlite3_finalize(st);
    }
    // OpenCode 2：session_v2 的 tokens_* 是整段会话累计值，session_message 是消息流。
    // 同一个 id 在旧表里也有时，优先新表（时间戳/状态更新），但旧表的窗内四档更精确，
    // 新表算不出今天这一段就沿用旧表那份，绝不把整段累计冒充今天。
    if (table_exists(db, "session_v2") && table_exists(db, "session_message")) {
        std::ostringstream q;
        q << "select s.id, coalesce(s.title, ''), coalesce(s.directory, ''), coalesce(s.path, ''), "
             "s.time_updated, s.time_created, "
             "s.tokens_input, s.tokens_output, s.tokens_reasoning, "
             "s.tokens_cache_read, s.tokens_cache_write, coalesce(s.model, ''), "
             "(select count(*) from session_message m where m.session_id = s.id "
             " and m.type = 'user' and m.time_created >= " << since_ms << "), "
             "(select m.data from session_message m where m.session_id = s.id "
             " and m.type = 'assistant' order by m.seq desc limit 1), "
             "(select m.type from session_message m where m.session_id = s.id "
             " order by m.seq desc limit 1) "
             "from session_v2 s where coalesce(s.time_archived, 0) = 0 "
             "and s.time_updated >= " << cutoff_ms << " order by s.time_updated desc limit 60;";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const std::string sid = col_text(st, 0);
                const long long upd = sqlite3_column_int64(st, 4);
                const long long created = sqlite3_column_int64(st, 5);
                if (sid.empty() || upd <= 0) continue;

                Session s;
                s.id = "opencode-" + sid;
                s.await_src = QS_DB;
                s.title = trim(col_text(st, 1));
                std::string dir = col_text(st, 2);
                if (dir.empty()) dir = col_text(st, 3);
                s.cwd = dir;
                s.project = basename_last(dir);
                if (s.project.empty()) s.project = "未绑定项目";
                if (s.title.empty()) s.title = s.project;
                s.last_activity = (double)upd / 1000.0;
                s.created = (double)created / 1000.0;
                s.turns = (int)sqlite3_column_int64(st, 12);
                s.credits = 0;

                std::string model_json = col_text(st, 11);
                Json mj;
                if (!model_json.empty() && json_parse(model_json.data(), model_json.size(), mj))
                    if (const std::string* mid = mj.str_at("id")) s.model = *mid;

                // 只有今天新建的会话，累计值才全是今天的；跨天会话的今日增量新库里
                // 没有逐轮时间戳可切（part 也没有），留 0 比拿累计冒充诚实。
                if (created >= since_ms) {
                    s.usage.input = (long)sqlite3_column_int64(st, 6);
                    s.usage.output = (long)sqlite3_column_int64(st, 7)
                                   + (long)sqlite3_column_int64(st, 8);   // reasoning 归入输出档
                    s.usage.cache_read = (long)sqlite3_column_int64(st, 9);
                    s.usage.cache_write = (long)sqlite3_column_int64(st, 10);
                    s.tokens = s.usage.total();
                }

                const std::string assistant = col_text(st, 13);
                const std::string last_type = col_text(st, 14);
                bool running = false, streaming = false, tool_error = false;
                Json aj;
                if (!assistant.empty() && json_parse(assistant.data(), assistant.size(), aj)) {
                    const Json* content = aj.at("content");
                    if (content && content->kind == Json::ARR) {
                        for (const Json& item : content->arr) {
                            const std::string* ty = item.str_at("type");
                            if (!ty || *ty != "tool") continue;
                            const Json* state = item.at("state");
                            const std::string* status = state ? state->str_at("status") : nullptr;
                            if (status && *status == "running") running = true;
                            if (status && *status == "error") tool_error = true;
                        }
                    }
                    const Json* tj = aj.at("time");
                    if (tj && !tj->has("completed")) streaming = true;
                }
                if (last_type == "user") s.state = ST_WORKING;
                else if (last_type == "idle") s.state = ST_NEEDS_USER;
                else if (last_type == "assistant")
                    s.state = (running || streaming) ? ST_WORKING
                            : (tool_error ? ST_FAILED : ST_NEEDS_USER);
                else s.state = ST_NEEDS_USER;
                if (!assistant.empty() && s.state == ST_FAILED)
                    s.error_text = "工具调用报错";

                // 迁移期的老会话：新表算不出今日增量时，沿用旧表那份精确的四档。
                auto old = merged.find(sid);
                if (old != merged.end()) {
                    if (s.usage.total() == 0 && old->second.usage.total() > 0)
                        s.usage = old->second.usage;
                    if (s.turns == 0) s.turns = old->second.turns;
                    if (s.title.empty()) s.title = old->second.title;
                }
                merged[sid] = std::move(s);
            }
        }
        sqlite3_finalize(st);
    }
    for (auto& [sid, s] : merged) {
        (void)sid;
        if (now - s.last_activity > kNeedsUserStale && s.state != ST_FAILED) s.state = ST_RESTING;
        s.tokens = s.usage.total();
        snap.usage.input += s.usage.input;
        snap.usage.output += s.usage.output;
        snap.usage.cache_read += s.usage.cache_read;
        snap.usage.cache_write += s.usage.cache_write;
        snap.sessions.push_back(std::move(s));
    }
    sqlite3_close(db);
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    return snap;
}

#endif  // AP_HAVE_SQLITE

const char* state_name(State s) {
    switch (s) {
        case ST_WORKING: return "working";
        case ST_NEEDS_USER: return "needs_user";
        case ST_AWAIT: return "awaiting";
        case ST_QUEUED: return "queued";
        case ST_FAILED: return "failed";
        default: return "resting";
    }
}

}  // namespace

// ── Notification hook 收件箱 ────────────────────────────────────────────────
// CLI 停在「允许吗」上时，transcript 里最后一条就是一条没回的 tool_use，只看文件必然判成
// 「在跑」—— 最该打断你的那一档被判反。hook 是唯一能把它从「猜」变成「事实」的来源，
// 而且带来说是哪个工具、问的是什么、有哪几个候选。口径逐条对齐 Swift 侧 HookInbox.swift。
namespace {

const double kHookSkew = 2;    // CLI 先写 tool_use 再触发 hook，两个时间戳常落在同一秒

std::string hook_dir() {
    const char* over = getenv("AGENTPING_HOOK_DIR");
    if (over && *over) return over;
#ifdef __APPLE__
    return home() + "/Library/Application Support/AgentPing/hooks";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    return std::string(xdg && *xdg ? xdg : (home() + "/.config").c_str()) + "/AgentPing/hooks";
#endif
}

std::string slurp(const std::string& path, std::size_t max_bytes = 64 * 1024) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::string out(max_bytes + 1, '\0');
    f.read(&out[0], (std::streamsize)max_bytes + 1);
    std::streamsize got = f.gcount();
    out.resize(got < 0 ? 0 : (std::size_t)got);
    return out;
}

/// 文件名形状 `<agent>-<epoch>-<pid>.json` → epoch；0 = 形状不对
double hook_stamp(const std::string& name) {
    std::string base = name.size() > 5 ? name.substr(0, name.size() - 5) : "";
    std::vector<std::string> parts;
    std::string cur;
    for (char c : base) {
        if (c == '-') { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    if (parts.size() < 2) return 0;
    return (double)strtoll(parts[parts.size() - 2].c_str(), nullptr, 10);
}

/// 问句与候选项。实测形状：
///   details.message            —— 「Allow Bash to run: …」这类短提示
///   details.input.questions[]  —— AskUserQuestion
/// 没有 details 的对象只剩顶层 message。解析本体与 ZCode 共用 parse_question_block。
void hook_question(const Json& obj, std::string& q, std::vector<std::string>& opts,
                   std::vector<std::string>& desc) {
    const Json* det = obj.at("details");
    const std::string* top_msg = obj.str_at("message");
    if (!det || det->kind != Json::OBJ) {
        if (top_msg) q = hook_one_line(*top_msg, 160);
        return;
    }
    const std::string* det_msg = det->str_at("message");
    parse_question_block(*det, det_msg ? det_msg : top_msg, q, opts, desc);
}

struct HookSig {
    std::string agent, session, question, tool, reason;
    std::vector<std::string> options, option_desc;
    double at = 0;
    bool needs_approval = false;
};

}  // namespace

void apply_hook_signals(Snapshot& snap, double now) {
    const std::string dir = hook_dir();
    std::vector<std::string> names = list_dir(dir);
    std::vector<HookSig> sigs;
    int dropped = 0;
    for (const std::string& name : names) {
        if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        double at = hook_stamp(name);
        if (at <= 0) { dropped++; continue; }
        if (now - at > kNeedsUserStale) continue;      // 过窗作废，不然上周那次授权一直挂着红点
        std::string body = slurp(join(dir, name));
        Json obj;
        if (body.empty() || !json_parse(body.c_str(), body.size(), obj)) { dropped++; continue; }
        HookSig g;
        g.at = at;
        const std::string* a = obj.str_at("agent");
        if (a && !a->empty()) g.agent = *a;
        else { std::size_t d = name.find('-'); g.agent = d == std::string::npos ? name : name.substr(0, d); }
        if (const std::string* s = obj.str_at("session_id")) g.session = *s;
        // 事件名没在真机文档里钉死，所以按「含 permission 就算要授权」宽着认；
        // 认不出的一律当「只是等你输入」—— 宁可少打断，也不拿不确定的事件去响铃。
        for (const char* k : {"notification_type", "notificationType", "type", "reason"}) {
            const std::string* v = obj.str_at(k);
            if (v && v->find("permission") != std::string::npos) g.needs_approval = true;
        }
        // toolName 实测在 details 里，不在顶层：只读顶层就永远是空，「是哪个工具在问」就没了。
        const Json* det = obj.at("details");
        if (det) if (const std::string* t = det->str_at("toolName")) g.tool = *t;
        if (g.tool.empty()) {
            if (const std::string* t = obj.str_at("tool_name")) g.tool = *t;
            else if (const std::string* t = obj.str_at("toolName")) g.tool = *t;
        }
        hook_question(obj, g.question, g.options, g.option_desc);
        // 「触发说明」只认批准类信号自带的说明原文；问句不算说明，别把问句复读一遍。
        if (g.needs_approval) {
            const std::string* m = obj.str_at("message");
            if (!m && det) m = det->str_at("message");
            if (!m) m = obj.str_at("reason");
            if (m && !m->empty()) g.reason = hook_one_line(*m, 90);
        }
        sigs.push_back(std::move(g));
    }
    std::sort(sigs.begin(), sigs.end(), [](const HookSig& a, const HookSig& b) { return a.at > b.at; });
    snap.hook_live = (int)sigs.size();
    snap.hook_dropped = dropped;

    int landed = 0;
    for (const HookSig& g : sigs) {
        auto ai = std::find_if(snap.agents.begin(), snap.agents.end(),
                               [&](const AgentSnap& a) { return a.id == g.agent; });
        if (ai == snap.agents.end()) {
            fprintf(stderr, "hook 信号未落地（%s）：收件箱说这是 %s，但本机没有这个 agent 的卡\n",
                    g.agent.c_str(), g.agent.c_str());
            continue;
        }
        std::vector<Session>& ss = ai->sessions;
        std::size_t j = ss.size();
        if (!g.session.empty()) {
            auto si = std::find_if(ss.begin(), ss.end(), [&](const Session& s) { return s.id == g.session; });
            if (si != ss.end()) j = (std::size_t)(si - ss.begin());
        }
        if (j == ss.size() && !ss.empty())
            j = (std::size_t)(std::max_element(ss.begin(), ss.end(),
                    [](const Session& a, const Session& b) { return a.last_activity < b.last_activity; }) - ss.begin());
        if (j == ss.size()) {
            fprintf(stderr, "hook 信号未落地（%s）：%s 一条会话都没有，没地方落\n",
                    g.agent.c_str(), g.agent.c_str());
            continue;
        }
        Session& s = ss[j];
        if (g.at < s.last_activity - kHookSkew) {
            fprintf(stderr, "hook 信号未落地（%s）：落到「%s」但那条日志比信号新 %.1f 秒，判为已处理\n",
                    g.agent.c_str(), utf8_clip(s.title, 18, "").c_str(), s.last_activity - g.at);
            continue;
        }
        // 带问句或候选项的信号就是「它停下来在问你」——归介入组；纯 idle 提示仍按
        // 「这一轮无错收尾、球回到你这边」计入已完成。不这么分，AskUserQuestion 会掉进
        // 「今日已结束」，问题原文和选项在屏上却没有一条能点进去的详情。
        const bool asks = g.needs_approval || !g.question.empty() || !g.options.empty();
        s.state = asks ? ST_AWAIT : ST_NEEDS_USER;
        if (asks) { s.await_since = g.at; s.await_src = QS_HOOK; }
        s.inferred_wait = false;              // CLI 亲口说的，就不再是推断
        if (!g.question.empty()) s.question = g.question;
        if (!g.options.empty()) { s.options = g.options; s.option_desc = g.option_desc; }
        if (!g.tool.empty()) s.tool = g.tool;
        if (!g.reason.empty()) s.trigger = g.reason;
        landed++;
    }
    snap.hook_landed = landed;
    if (dropped) fprintf(stderr, "hook 收件箱有 %d 个文件解析不了，已跳过\n", dropped);
}

/// ZCode 的计划余额：~/.zcode/v2/logs/<YYYY-MM-DD>.log 里
/// `billing/balance 请求完成 {…}` 这一行带整个响应体（balances[].total_units /
/// remaining_units / unit_type，payload.data.plans[].ends_at）。今天那份没有就退到昨天：
/// 本期常跨零点，过期那一刻也要读得到 ends_at，界面才说得出「已到期」。
Balance read_zcode_balance() {
    Balance b;
    const std::string dir = home() + "/.zcode/v2/logs";
    std::vector<std::string> names = list_dir(dir);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& c) { return a > c; });
    const double now = (double)std::time(nullptr);
    // ① 官方 MCP 额度（zcode.z.ai/api/v1/mcp/usage 的落日志响应）＝当前订阅的真额度：
    //    total_usage used/limit/remaining，日刷新（next_refresh_at）。billing/balances
    //    那路是「领的卡」清单——卡过期后该接口返回空清单，继续往旧日志捞只会捞到
    //    过期快照（11-13 用户实拍：显示了已过期卡的 1.7亿/3亿）。MCP 有数就以它为准。
    for (std::size_t i = 0; i < names.size() && i < 2; i++) {
        const std::string mp = dir + "/" + names[i];
        for (const std::string& ln : tail_lines(mp, "官方 MCP 额度响应", 3, 4ull << 20)) {
            const std::size_t brace = ln.find('{');
            if (brace == std::string::npos) continue;
            Json rec;
            if (!json_parse(ln.c_str() + brace, ln.size() - brace, rec)) continue;
            const Json* body = rec.at("body");
            if (!body || body->kind != Json::STR) continue;   // 响应体是二次编码的字符串
            Json inner;
            if (!json_parse(body->str.c_str(), body->str.size(), inner)) continue;
            const Json* data = inner.at("data");
            const Json* usage = data ? data->at("total_usage") : nullptr;
            if (!usage || usage->kind != Json::OBJ) continue;
            bool gr = false, gl = false, gn = false;
            const long remaining = num_at(usage, "remaining", &gr);
            const long limit = num_at(usage, "limit", &gl);
            if (!gr || remaining < 0 || !gl || limit <= 0) continue;
            b.known = true;
            b.remaining = (double)remaining;
            b.total = (double)limit;
            b.unit = "";   // 不标 credits：这是套餐的每日额度计数，不是 credits 池
            if (const Json* lv = data->at("level"))
                if (lv->kind == Json::STR) b.label = lv->str;
            const long nref = num_at(data, "next_refresh_at", &gn);
            if (gn && nref > 0) b.period_end = (double)nref;
            b.seen_at = parse_bracket_local(ln);
            b.exceeded = remaining <= 0;
            b.source = mp;
            return b;
        }
    }
    // ② 领的卡（billing/balances）兜底：过期的卡不再顶数——到期时刻已过就整行作废，
    //    继续往更旧的日志找也是同一张卡的更旧快照，直接两份日志都不认。
    for (std::size_t i = 0; i < names.size() && i < 2; i++) {
        const std::string p = dir + "/" + names[i];
        const FStat st = fst(p);
        if (!st.ok) continue;
        BalanceCache& c = balance_cache()[p];
        if (c.mtime == st.mtime && c.size == st.size) {
            if (c.bal.known) return c.bal;
            continue;
        }
        Balance got;
        for (const std::string& ln : tail_lines(p, "billing/balance 请求完成", 4, 4ull << 20)) {
            const std::size_t brace = ln.find('{');
            if (brace == std::string::npos) continue;
            Json rec;
            if (!json_parse(ln.c_str() + brace, ln.size() - brace, rec)) continue;
            const Json* pay = rec.at("payload");
            const Json* data = pay ? pay->at("data") : nullptr;
            // 顶层的 balances[] 只有数字，没有 unit_type；带单位的完整清单在
            // payload.data.balances[]。两份数一样，优先读带单位的那一份。
            const Json* bals = data ? data->at("balances") : nullptr;
            if (!bals || bals->kind != Json::ARR || bals->arr.empty()) bals = rec.at("balances");
            if (!bals || bals->kind != Json::ARR || bals->arr.empty()) continue;
            std::string unit, label;
            long total = 0, remaining = 0;
            bool any = false;
            for (const Json& e : bals->arr) {
                const std::string* ut = e.str_at("unit_type");
                const std::string u = balance_unit(ut ? *ut : std::string());
                if (unit.empty()) unit = u;
                else if (!u.empty() && u != unit) continue;    // 不同单位不相加
                bool gt = false, gr = false;
                const long t = num_at(&e, "total_units", &gt);
                const long r = num_at(&e, "remaining_units", &gr);
                if (!gr || r < 0) continue;
                if (gt && t > 0) total += t;
                remaining += r;
                any = true;
                if (label.empty())
                    if (const std::string* sn = e.str_at("show_name")) label = *sn;
            }
            if (!any) continue;
            got.known = true;
            got.total = (double)total;
            got.remaining = (double)remaining;
            got.unit = unit;
            got.label = label;
            got.seen_at = parse_bracket_local(ln);
            got.exceeded = total > 0 && remaining <= 0;
            got.source = p;
            const Json* plans = data ? data->at("plans") : nullptr;
            if (plans && plans->kind == Json::ARR && !plans->arr.empty()) {
                bool ge = false;
                const long e = num_at(&plans->arr.front(), "ends_at", &ge);
                if (ge && e > 0) got.period_end = (double)e;
            }
            break;    // tail_lines 新的在前：第一条能给余额的就是当前读数
        }
        if (got.known && got.period_end > 0 && got.period_end < now) {
            got = Balance{};   // 过期卡：不算数（11-13 实拍：09:00 到期的卡被顶成了当前余额）
        }
        c.mtime = st.mtime;
        c.size = st.size;
        c.bal = got;
        if (got.known) return got;
    }
    return b;
}

/// Qoder 的账户余额：App 自己的 main.log 里
/// `[Auth] [AccountUsage] 用量响应摘要 {…}` —— 它调 account.getQuotaUsage 之后把
/// 响应体原样写盘。userQuota 与 addOnQuota 是同一单位的两个池子，相加才是账户全貌。
/// expiresAt 是毫秒；实测 253402214400000（9999 年）＝它根本没给到期时刻，按 0 处理。
Balance read_qoder_balance() {
    Balance b;
    const std::string root = home() + "/Library/Application Support/com.qoder.app.stable/logs";
    std::vector<std::pair<double, std::string>> dirs;
    for (const std::string& n : list_dir(root)) {
        const std::string p = root + "/" + n;
        const FStat st = fst(p);
        if (st.ok && st.dir) dirs.emplace_back(st.mtime, n);
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const std::pair<double, std::string>& a, const std::pair<double, std::string>& c) {
                  return a.first > c.first;
              });
    for (const auto& d : dirs) {
        const std::string p = root + "/" + d.second + "/main.log";
        const FStat st = fst(p);
        if (!st.ok) continue;
        BalanceCache& c = balance_cache()[p];
        if (c.mtime == st.mtime && c.size == st.size) {
            if (c.bal.known) return c.bal;
            continue;
        }
        Balance got;
        for (const std::string& ln : tail_lines(p, "用量响应摘要", 1, 8ull << 20)) {
            const std::size_t brace = ln.find('{');
            if (brace == std::string::npos) continue;
            Json rec;
            if (!json_parse(ln.c_str() + brace, ln.size() - brace, rec)) continue;
            const Json* qu = rec.at("qoderUsage");
            if (!qu || qu->kind != Json::OBJ) continue;
            long total = 0, remaining = 0;
            std::string unit;
            auto pool = [&](const char* k) {
                const Json* q = qu->at(k);
                if (!q || q->kind != Json::OBJ) return;
                bool gt = false, gr = false;
                const long t = num_at(q, "total", &gt);
                const long r = num_at(q, "remaining", &gr);
                if (gt && t > 0) total += t;
                if (gr && r > 0) remaining += r;
                if (unit.empty())
                    if (const std::string* u = q->str_at("unit")) unit = balance_unit(*u);
            };
            pool("userQuota");
            pool("addOnQuota");
            if (total <= 0 && remaining <= 0) continue;
            got.known = true;
            got.total = (double)total;
            got.remaining = (double)(remaining > 0 ? remaining : 0);
            got.unit = unit.empty() ? std::string("Credits") : unit;
            got.seen_at = parse_bracket_iso(ln);
            bool gx = false;
            const Json* ex = qu->at("isQuotaExceeded");
            if (ex && ex->kind == Json::BOOL && ex->boolean) gx = true;
            got.exceeded = gx || (total > 0 && remaining <= 0);
            bool ge = false;
            const long ms = num_at(qu, "expiresAt", &ge);
            // 9999 年那种「没有到期日」不当地址用：只认 2100 年以前的
            if (ge && ms > 0 && ms < 4102444800000LL) got.period_end = (double)ms / 1000.0;
            got.source = p;
            break;
        }
        c.mtime = st.mtime;
        c.size = st.size;
        c.bal = got;
        if (got.known) return got;
    }
    return b;
}

// ── Qoder 桌面 App 的运行状态 ────────────────────────────────────────────────
/// 一行 control state 长这样（11-02 从他本机 main.log 原样抄的，探针就拿它当夹具）：
/// `[2026-11-02T08:02:39.892Z] [INFO] [main] [ChatSession] Main ChatSession control state
///  committed {"sessionId":"d326da19-…","runtimeState":"running","pendingInteractionCount":0,…}`
bool parse_qoder_control(const std::string& line, std::string& sid, QoderLive& out) {
    const std::size_t brace = line.find('{');
    if (brace == std::string::npos) return false;
    Json rec;
    if (!json_parse(line.c_str() + brace, line.size() - brace, rec)) return false;
    const std::string* s = rec.str_at("sessionId");
    const std::string* rt = rec.str_at("runtimeState");
    if (!s || s->empty() || !rt || rt->empty()) return false;
    sid = *s;
    out.state = *rt;
    out.ts = parse_bracket_iso(line);
    const Json* p = rec.at("pendingInteractionCount");
    out.pending = (p && p->kind == Json::NUM) ? (int)p->num : 0;
    return true;
}

struct LiveCache {
    double mtime = -1, size = -1;
    std::map<std::string, QoderLive> live;
};
std::map<std::string, LiveCache>& live_cache() {
    static std::map<std::string, LiveCache> m;
    return m;
}

/// 只往最新那一次运行目录里读：App 重启后上一轮的 running 那句已经不作数（那一路进程早没了），
/// 所以这里刻意不回退到旧目录 —— 与 read_qoder_balance「旧目录也可能还写着余额」的取法相反。
/// main.log 刚轮转过的一瞬可能一条都没有，顺带读一眼 main.1.log 补上还没见过的会话。
std::map<std::string, QoderLive> read_qoder_live() {
    std::map<std::string, QoderLive> empty;
    const std::string root = home() + "/Library/Application Support/com.qoder.app.stable/logs";
    std::vector<std::pair<double, std::string>> dirs;
    for (const std::string& n : list_dir(root)) {
        const std::string p = root + "/" + n;
        const FStat st = fst(p);
        if (st.ok && st.dir) dirs.emplace_back(st.mtime, n);
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const std::pair<double, std::string>& a, const std::pair<double, std::string>& c) {
                  return a.first > c.first;
              });
    if (dirs.empty()) return empty;
    const std::string dir = root + "/" + dirs.front().second;
    std::map<std::string, QoderLive> out;
    for (const char* leaf : {"main.log", "main.1.log"}) {
        const bool primary = leaf[4] == '.';           // main.log 正本；main.1.log 里那个位置是「1」
        const std::string p = dir + "/" + leaf;
        const FStat st = fst(p);
        if (!st.ok) continue;
        LiveCache& c = live_cache()[p];
        if (c.mtime != st.mtime || c.size != st.size) {
            c.mtime = st.mtime;
            c.size = st.size;
            c.live.clear();
            // 一轮 3 秒扫一次，这条 needle 在 4 MB 日志里只出现几十次：cap 2 MB / 16 条足够
            // 覆盖最近十几轮，代价是每轮最多回扫 2 MB（实测 <2 ms）。
            for (const std::string& ln : tail_lines(p, "control state committed", 16, 2ull << 20)) {
                std::string sid;
                QoderLive q;
                // tail_lines 是「新在前」返回的，所以每个会话只认第一条：emplace 才是最新，
                //  plain assign 会留下窗口里最旧的一条。2026-11-02 实测踩过：本轮已经写完
                //  （最新一条 cold@20:06），窗口里最旧那条 opening@18:29 把状态钉死成「运行中」。
                if (parse_qoder_control(ln, sid, q) && q.ts > 0) c.live.emplace(sid, q);
            }
        }
        for (const auto& kv : c.live)
            if (primary) out[kv.first] = kv.second;    // 正本直接占位
            else out.emplace(kv.first, kv.second);     // 补位：正本这轮还没写过的会话
    }
    return out;
}

/// 盖上几条。判据三条，都能被 main.log 原文反驳：
/// ① running/opening ＝ 它自己说这一轮还在跑。转轮中每条 tool_use／tool_result 都在写日志
///    而 runtimeState 不变，所以「这行比日志旧」不构成反驳，只有 20 分钟两头都静才算它过期。
/// ② waiting-user（或 pendingInteractionCount>0）＝ 真等你点，算正证（inferred_wait 清掉，许响铃）。
///    这一档要求它不比日志旧：你答完话日志就往前写了，那一句已经作废。
/// ③ interrupted ＝ 你按了停止，球回到你这边；同样要求不比日志旧。
/// 异常那条不动：日志报了错的优先，不能被一句旧的 running 抹平。
int apply_qoder_live(Snapshot& snap, const std::map<std::string, QoderLive>& live, double now) {
    if (live.empty()) return 0;
    int n = 0;
    for (AgentSnap& a : snap.agents) {
        if (a.id != "qoder") continue;
        for (Session& s : a.sessions) {
            auto it = live.find(s.id);
            if (it == live.end()) continue;         // 这一条桌面 App 没提交过状态（CLI 会话就是这样）
            const QoderLive& q = it->second;
            if (now - std::max(q.ts, s.last_activity) > kNeedsUserStale) continue;
            const bool fresh = q.ts + kHookSkew >= s.last_activity;   // 之后日志又写了东西就以日志为准
            if ((q.state == "running" || q.state == "opening") && s.state != ST_FAILED) {
                s.state = ST_WORKING;
                s.inferred_wait = false;
            } else if (fresh && (q.state == "waiting-user" || q.pending > 0)) {
                s.state = ST_AWAIT;
                s.inferred_wait = false;            // 它亲口说的，不再是反推
                s.await_since = q.ts;
            } else if (fresh && q.state == "interrupted" &&
                       (s.state == ST_AWAIT || s.state == ST_WORKING)) {
                s.state = ST_NEEDS_USER;
                s.inferred_wait = false;
            } else {
                continue;                            // cold / ready：日志那一侧已经够准，不盖
            }
            n++;
        }
    }
    return n;
}

Snapshot scan_all(double now_epoch) {
    double now = now_epoch > 0 ? now_epoch : (double)time(nullptr);
    double since = window_start(now);
    Snapshot snap;
    snap.scanned_at = now;
    snap.window_start = since;

    std::string h = home();
#ifdef AP_HAVE_SQLITE
    const std::string qdb =
        join(h, "Library/Application Support/com.qoder.app.stable/main.sqlite");
    std::map<std::string, std::string> titled = sidebar_titles(qdb);
    // 同一张表再取一次「在架 id」：标题只用来起名，判形态要看 id 在不在，与有没有标题无关。
    std::set<std::string> qids = app_session_ids(qdb);
#else
    std::map<std::string, std::string> titled;
    std::set<std::string> qids;
#endif
    {
        AgentSnap qoder = scan_transcript("qoder", "Qoder CLI", join(h, ".qoder/projects"), now, since,
                                          titled, qids, &snap.yesterday_tokens, &snap.yesterday_credits);
        qoder.quota.balance = read_qoder_balance();
        if (qoder.quota.balance.known) qoder.quota.known = true;
        snap.agents.push_back(std::move(qoder));
    }
    // Claude 的标题写在日志里（customTitle/agentName），不借 Qoder 侧栏那张表。
    snap.agents.push_back(scan_transcript("claude", "Claude Code", join(h, ".claude/projects"), now, since, {},
                                          {}, &snap.yesterday_tokens, &snap.yesterday_credits));
    snap.agents.push_back(scan_codex("Codex", join(h, ".codex/sessions"), now, since));
#ifdef AP_HAVE_SQLITE
    {
        AgentSnap zc = scan_zcode("ZCode", join(h, ".zcode/cli/db/db.sqlite"), now, since);
        zc.quota.balance = read_zcode_balance();
        if (zc.quota.balance.known) zc.quota.known = true;
        snap.agents.push_back(std::move(zc));
    }
    snap.agents.push_back(scan_workbuddy("WorkBuddy", join(h, ".workbuddy/workbuddy.db"),
                                         join(h, ".workbuddy/projects"), now, since));
    // OpenCode 走 XDG 数据目录（macOS 上它自己也落在这条路径），没设环境变量就按默认。
    {
        const char* xd = getenv("XDG_DATA_HOME");
        std::string root = (xd && *xd) ? std::string(xd) : join(h, ".local/share");
        snap.agents.push_back(scan_opencode("OpenCode", join(root, "opencode/opencode.db"), now, since));
    snap.agents.push_back(scan_pi("Pi", join(h, ".pi/agent/sessions"), now, since));
    snap.agents.push_back(scan_qwenwork("QwenWork", join(h, ".qwenworkcn/projects"), now, since));
    }
#endif

    for (const AgentSnap& a : snap.agents)
        for (const Session& s : a.sessions)
            if (s.last_activity >= since) { snap.today_tokens += s.tokens; snap.today_credits += s.credits; }
    // 昨日小计：claude/qoder 由 scan_transcript 出参已累；zcode/workbuddy 的份在 AgentSnap。
    for (const AgentSnap& a : snap.agents) {
        snap.yesterday_tokens += a.yday_tokens;
        snap.yesterday_credits += a.yday_credits;
    }
    // 先盖 App 亲口报的运行状态，再让 hook 收件箱盖它：两家都是「它自己写的」，
    // 但 hook 那条带问句原文与落点更具体。
    snap.live_landed = apply_qoder_live(snap, read_qoder_live(), now);
    apply_hook_signals(snap, now);
    apply_proc_sentinel(snap);
    roll_daily(snap);
    compute_trends(snap);
    roll_await(snap);
    return snap;
}

std::string snapshot_dump(const Snapshot& s) {
    char buf[512];
    std::string out;
    snprintf(buf, sizeof buf,
             "scanned_at=%.0f window_start=%.0f today_credits=%.2f today_tokens=%ld "
             "| yesterday_credits=%.2f yesterday_tokens=%ld | hook 落地%d/有效%d/解析不了%d "
             "| await 本周 n=%ld avg=%.0f max=%.0f "
             "| App 运行状态落地%d\n",
             s.scanned_at, s.window_start, s.today_credits, s.today_tokens,
             s.yesterday_credits, s.yesterday_tokens, s.hook_landed, s.hook_live, s.hook_dropped,
             s.await_n, s.await_avg, s.await_max, s.live_landed);
    out += buf;
    for (const AgentSnap& a : s.agents) {
        snprintf(buf, sizeof buf, "[%s] %s available=%d sessions=%zu%s%s\n", a.id.c_str(), a.name.c_str(),
                 (int)a.available, a.sessions.size(),
                 a.scan_error.empty() ? "" : " scanError=", a.scan_error.c_str());
        out += buf;
        if (a.usage.total() > 0 || a.quota.known) {
            std::string top = a.models.empty() ? "" : a.models.front().name;
            snprintf(buf, sizeof buf, "  usage in=%ld out=%ld cr=%ld cw=%ld total=%ld model=%s",
                     a.usage.input, a.usage.output, a.usage.cache_read, a.usage.cache_write,
                     a.usage.total(), top.c_str());
            out += buf;
            if (a.quota.known) {
                snprintf(buf, sizeof buf, " | 5h=%.1f%%(reset %.0f, %dmin) wk=%.1f%%(reset %.0f) plan=%s",
                         a.quota.five.used * 100, a.quota.five.resets_at, a.quota.five.window_min,
                         a.quota.week.used * 100, a.quota.week.resets_at, a.quota.plan.c_str());
                out += buf;
            }
            if (a.quota.balance.known) {
                const Balance& b = a.quota.balance;
                snprintf(buf, sizeof buf,
                         " | bal=%.0f/%.0f %s(ends %.0f, seen %.0f)%s%s%s",
                         b.remaining, b.total, b.unit.c_str(), b.period_end, b.seen_at,
                         b.exceeded ? " used-up" : "", b.label.empty() ? "" : " label=",
                         b.label.c_str());
                out += buf;
            }
            out += "\n";
        }
        for (const Session& x : a.sessions) {
            // 按字节截（%.70s）会把 UTF-8 砍成半个字，这份 dump 是对账用的，必须可读
            snprintf(buf, sizeof buf,
                     "  %-10s %-14s %.0f  tok=%ld cr=%.2f turn=%d q=%d loop=%d cl=%-4s %s%s%s%s%s",
                     state_name(x.state), utf8_clip(x.project, 14, "").c_str(), x.last_activity,
                     x.tokens, x.credits, x.turns, x.queue_pos, x.loop_count,
                     x.client.empty() ? "-" : x.client.c_str(),
                     utf8_clip(x.title, 56, "…").c_str(),
                     x.inferred_wait ? " (推断)" : "", x.proc_dead ? " (进程已死)" : "",
                     x.question.empty() ? "" : "  ⟵", x.question.empty() ? "" : utf8_clip(x.question, 40, "…").c_str());
            out += buf;
            // 面板上那句问话底下还有候选项和「是哪个工具在问」，这两个数 + 等待起点 + 证据
            // 出自哪条通道，不写进对账行的话，「面板显示了候选」就成了屏幕上信不过的一主张。
            // src 还是详情屏「来源」那一格的唯一依据：ZCode 这类没有 hook 通道的，念「hook
            // 收件箱」就是界面在编一个本机不存在的文件。
            // 每条「等待确认」都必须带来源，包括 WorkBuddy 那种没有问句、只有反推的：
            // 详情屏那一格对整档都渲染，对账行里没有 src 就等于这一格没被测过。
            if (!x.tool.empty() || !x.options.empty() || x.state == ST_AWAIT) {
                snprintf(buf, sizeof buf, "  ⟦tool=%s opt=%zu wait=%.0fs src=%s⟧", x.tool.c_str(),
                         x.options.size(), x.await_since > 0 ? s.scanned_at - x.await_since : 0.0,
                         x.await_src == QS_HOOK ? "hook" : x.await_src == QS_DB ? "db" : "log");
                out += buf;
            }
            // N1–N3 对账行：循环证据条数与今日操作聚合（详情屏/接力简报/导出的同一数据源）。
            if (x.loop_count >= 3 || !x.op_files.empty() || !x.op_cmds.empty()) {
                snprintf(buf, sizeof buf,
                         "\n   [N1-N3] 循环证据 %zu 条·%s | 文件 %zu(+%d) | 命令 %zu(+%d) 最近:%s",
                         x.loop_events.size(), x.loop_count >= 3 ? "成环" : "未成环",
                         x.op_files.size(), x.op_files_more, x.op_cmds.size(), x.op_cmds_more,
                         x.op_cmds.empty() ? "-" : utf8_clip(x.op_cmds.back(), 36, "…").c_str());
                out += buf;
            }
            out += "\n";
        }
    }
    return out;
}

// ── pi 与千问办公：message 信封式 JSONL（2026-11-12 新增两路）──────────────
// pi     ~/.pi/agent/sessions/<工作区slug>/*.jsonl
//        记录 {"type":"session|model_change|message|...","timestamp":ISO}；
//        message.message = {role, content:[{type:text|toolCall}], usage:{input,output,
//        cacheRead,cacheWrite}, stopReason: stop|length|aborted|error|toolUse}
// 千问办公 ~/.qwenworkcn/projects/<工作区slug>/*.jsonl
//        记录 {"type":"workspace-directories|runtime-config|user|assistant|..."}；
//        user/assistant 带 message{role,stop_reason,...}，无 token 字段——
//        没有的格整格不画，不拿 0 冒充（家规）。
// 共用一条判态：最后一声是人＝在跑；assistant 收尾值（end_turn/stop/length/aborted）＝
// 该你读了；toolUse 类挂起 90s＝疑似等授权（推断不响铃）；error＝失败；
// 20 分钟没动静收成 resting。都用 ISO 时间戳（json_date_at 直读）。
namespace {
State derive_envelope(bool last_voiced_user, const std::string& stop, double last_ts,
                      double now, bool& inferred, const char* tool_word,
                      bool errored = false) {
    inferred = false;
    State st = ST_RESTING;
    if (last_ts > 0) {
        if (errored) st = ST_FAILED;
        else if (last_voiced_user) st = ST_WORKING;
        else if (stop == "end_turn" || stop == "stop" || stop == "stop_sequence" ||
                 stop == "length" || stop == "aborted") st = ST_NEEDS_USER;
        else if (stop == tool_word && now - last_ts >= kStalledToolUse) {
            st = ST_AWAIT;
            inferred = true;
        } else st = ST_WORKING;
        if (st == ST_WORKING && now - last_ts > kNeedsUserStale) st = ST_RESTING;
    }
    return st;
}
}

AgentSnap scan_pi(const std::string& name, const std::string& root, double now, double since) {
    AgentSnap snap;
    snap.id = "pi"; snap.name = name; snap.client = "CLI";
    FStat rs = fst(root);
    if (!rs.ok || !rs.dir) return snap;      // 本机没有这一路：不算「今天没活动」
    snap.available = true;
    const double cutoff = now - kDaysBack * 86400;
    for (const std::string& dir : list_dir(root)) {
        std::string dpath = join(root, dir);
        if (!fst(dpath).dir) continue;
        for (const std::string& f : list_dir(dpath)) {
            if (f.size() < 7 || f.compare(f.size() - 6, 6, ".jsonl") != 0) continue;
            std::string path = join(dpath, f);
            FStat st = fst(path);
            if (!st.ok || st.mtime < cutoff) continue;
            auto tail = tail_records(path);
            if (tail.empty()) continue;
            Session s;
            s.id = "pi-" + strip_ext(f);
            double last_ts = 0, first_ts = 0;
            bool last_voiced_user = false, errored = false, title_set = false;
            auto stamp = [&](const Json& rec) {
                double d = json_date_at(rec, "timestamp");
                if (d > 1e12) d /= 1000.0;            // runtime-config 这类给毫秒，归一成秒
                if (d > last_ts) last_ts = d;
                if (d > 0 && (first_ts == 0 || d < first_ts)) first_ts = d;
                return d;
            };
            std::string stop, model, cwd, title;
            int turns = 0;
            for (const Json& rec : tail) {
                const double d = stamp(rec);
                const std::string* t = rec.str_at("type");
                if (t && *t == "session") {
                    if (const std::string* c = rec.str_at("cwd")) cwd = *c;
                    continue;
                }
                if (t && *t == "model_change") {
                    if (const std::string* m = rec.str_at("modelId")) model = *m;
                    continue;
                }
                if (!t || *t != "message") continue;
                const Json* m = rec.at("message");
                if (!m) continue;
                const std::string* role = m->str_at("role");
                const std::string* sr = m->str_at("stopReason");
                if (sr && !sr->empty()) stop = *sr;
                if (const Json* u = m->at("usage")) {
                    auto num = [&](const char* k) -> long {
                        const Json* v = u->at(k);
                        return (v && v->kind == Json::NUM) ? (long)v->num : 0;
                    };
                    if (d == 0 || d >= since) {
                        s.usage.input += num("input");
                        s.usage.output += num("output");
                        s.usage.cache_read += num("cacheRead");
                        s.usage.cache_write += num("cacheWrite");
                    }
                }
                if (role && *role == "user") {
                    last_voiced_user = true;
                    if (d > 0 && d >= since) turns++;
                    const Json* content = m->at("content");
                    if (content && content->kind == Json::ARR && !title_set) {
                        for (const Json& blk : content->arr) {
                            const std::string* bt = blk.str_at("type");
                            if (bt && *bt == "text" && !title_set) {
                                if (const std::string* x = blk.str_at("text")) {
                                    // 系统注入的前缀（<system-reminder> 等）不作标题
                                    if (!x->empty() && x->rfind("<", 0) != 0) {   // 凡 < 开头的合成标记（system/user-pasted 等）都不作标题
                                        title = first_line(*x); title_set = true;
                                    }
                                }
                            }
                        }
                    }
                } else if (role && *role == "assistant") {
                    last_voiced_user = false;
                    if (stop == "error") errored = true;
                }
            }
            s.last_activity = last_ts;
            s.first_activity = (first_ts >= since) ? first_ts : 0;
            s.cwd = cwd;
            s.project = cwd.empty() ? dir : basename_last(cwd);
            s.turns = turns;
            s.model = model;
            s.tokens = s.usage.total();
            bool inf;
            s.state = derive_envelope(last_voiced_user, stop, last_ts, now, inf, "toolUse", errored);
            s.inferred_wait = inf && s.state == ST_AWAIT;
            s.title = !title.empty() ? utf8_clip(title, 40, "…") : s.project;
            snap.sessions.push_back(std::move(s));
        }
    }
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    return snap;
}

AgentSnap scan_qwenwork(const std::string& name, const std::string& root, double now, double since) {
    AgentSnap snap;
    snap.id = "qwenworkcn"; snap.name = name; snap.client = "IDE";
    FStat rs = fst(root);
    if (!rs.ok || !rs.dir) return snap;
    snap.available = true;
#ifdef __APPLE__
    // 账户积分：千问办公只在自家侧栏现拉现显，本机不落盘——AX 只读它窗口里的静态文本
    // （读不到＝卡面未接线/上次读取，不编数）。proc/bundle 见 platform/credits.h。
    {
        char buf[2048] = {0};
        if (ap_ax_texts("QwenWorkCN", "cn.qwenwork.desktop.mac", buf, (int)sizeof buf, 512)) {
            std::vector<std::string> segs;
            const char* q = buf;
            while (*q) {
                const char* bar = strchr(q, '|');
                segs.emplace_back(q, bar ? (size_t)(bar - q) : strlen(q));
                if (!bar) break;
                q = bar + 1;
            }
            auto first_num = [](const std::string& s) -> double {
                for (size_t k = 0; k < s.size(); k++) {
                    if (!isdigit((unsigned char)s[k])) continue;
                    std::string num;
                    while (k < s.size() && (isdigit((unsigned char)s[k]) || s[k] == ',' || s[k] == '.')) {
                        num += s[k];
                        k++;
                    }
                    std::string clean;
                    for (char c : num)
                        if (c != ',') clean += c;
                    try {
                        return std::stod(clean);
                    } catch (...) {
                        return -1;
                    }
                }
                return -1;
            };
            for (size_t i = 0; i < segs.size(); i++) {
                const size_t at = segs[i].find("积分余额");
                if (at == std::string::npos) continue;
                double v = first_num(segs[i].substr(at + strlen("积分余额")));
                if (v < 0 && i + 1 < segs.size()) v = first_num(segs[i + 1]);
                if (v < 0) continue;
                snap.quota.known = true;
                snap.quota.balance.known = true;
                snap.quota.balance.remaining = v;
                snap.quota.balance.unit = "积分";
                snap.quota.balance.label = "积分余额";
                snap.quota.balance.seen_at = now;
                break;
            }
        }
    }
#endif
    const double cutoff = now - kDaysBack * 86400;
    for (const std::string& dir : list_dir(root)) {
        std::string dpath = join(root, dir);
        if (!fst(dpath).dir) continue;
        for (const std::string& f : list_dir(dpath)) {
            if (f.size() < 7 || f.compare(f.size() - 6, 6, ".jsonl") != 0) continue;
            std::string path = join(dpath, f);
            FStat st = fst(path);
            if (!st.ok || st.mtime < cutoff) continue;
            auto tail = tail_records(path);
            if (tail.empty()) continue;
            Session s;
            s.id = "qwenworkcn-" + strip_ext(f);
            double last_ts = 0;
            bool last_voiced_user = false, title_set = false;
            std::string stop, model, cwd, title;
            int turns = 0;
            for (const Json& rec : tail) {
                double d = json_date_at(rec, "timestamp");
                if (d > 1e12) d /= 1000.0;                    // 这一路混有毫秒，归一成秒
                if (d > last_ts) last_ts = d;
                const std::string* t = rec.str_at("type");
                if (t && *t == "workspace-directories") {
                    if (const Json* ds = rec.at("directories"))
                        if (ds->kind == Json::ARR && !ds->arr.empty() &&
                            ds->arr[0].kind == Json::STR)
                            cwd = ds->arr[0].str;
                    if (const std::string* sid = rec.str_at("sessionId")) s.id = "qwenworkcn-" + *sid;
                    continue;
                }
                if (t && *t == "runtime-config") {
                    if (const std::string* m = rec.str_at("model")) model = *m;
                    continue;
                }
                if (!t || (*t != "user" && *t != "assistant")) continue;
                const Json* m = rec.at("message");
                if (!m) continue;
                if (t && *t == "user") {
                    last_voiced_user = true;
                    if (last_ts > 0 && last_ts >= since) turns++;
                    const Json* content = m->at("content");
                    if (content && content->kind == Json::ARR && !title_set) {
                        for (const Json& blk : content->arr) {
                            const std::string* bt = blk.str_at("type");
                            if (bt && *bt == "text" && !title_set) {
                                if (const std::string* x = blk.str_at("text")) {
                                    // 系统注入的前缀（<system-reminder> 等）不作标题
                                    if (!x->empty() && x->rfind("<", 0) != 0) {   // 凡 < 开头的合成标记（system/user-pasted 等）都不作标题
                                        title = first_line(*x); title_set = true;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    last_voiced_user = false;
                    if (const std::string* sr = m->str_at("stop_reason")) stop = *sr;
                    if (const std::string* mm = m->str_at("model")) model = *mm;
                }
            }
            s.last_activity = last_ts;
            s.cwd = cwd;
            s.project = cwd.empty() ? dir : basename_last(cwd);
            s.turns = turns;
            s.model = model;
            // 这一路日志没有 token 字段：usage 全零 → 界面 usage 整列不画（不拿 0 冒充）
            s.state = derive_envelope(last_voiced_user, stop, last_ts, now, s.inferred_wait,
                                      "toolUse", false);
            s.inferred_wait = s.inferred_wait && s.state == ST_AWAIT;
            s.title = !title.empty() ? utf8_clip(title, 40, "…") : s.project;
            snap.sessions.push_back(std::move(s));
        }
    }
    std::sort(snap.sessions.begin(), snap.sessions.end(),
              [](const Session& a, const Session& b) { return a.last_activity > b.last_activity; });
    return snap;
}

std::string hook_inbox_dir() { return hook_dir(); }

// ── N7 进程哨兵 ─────────────────────────────────────────────────────────────
// 只读进程名（comm）与 pid，不读 args/env/keychain——与「只读本机日志」同一档侵入性。
// macOS 走 sysctl(KERN_PROC_ALL)；Linux 走 /proc/*/comm。
// 覆盖披露（诚实边界）：claude/qoder 的 CLI 是 node 脚本，comm=node，进程名这一层认不出
// 「这一路还有没有活 CLI」——所以本轮只对 comm 可辨的 codex 判；IDE 会话（一进程多会话）
// 稿子本来就说不判。判据＝会话回合未收尾（ST_WORKING）且该路无同名进程＝事实；只标不响。
namespace {
std::set<std::string> live_proc_comms() {
    std::set<std::string> out;
#ifdef __APPLE__
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t len = 0;
    if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0 || len < sizeof(kinfo_proc)) return out;
    std::vector<char> buf(len);
    if (sysctl(mib, 4, buf.data(), &len, nullptr, 0) != 0) return out;
    for (size_t off = 0; off + sizeof(kinfo_proc) <= len; off += sizeof(kinfo_proc)) {
        const char* c = reinterpret_cast<const kinfo_proc*>(buf.data() + off)->kp_proc.p_comm;
        if (c && *c) out.insert(c);
    }
#else
    DIR* d = opendir("/proc");
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        std::string p = std::string("/proc/") + e->d_name + "/comm";
        std::ifstream f(p);
        std::string line;
        if (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) out.insert(line);
        }
    }
    closedir(d);
#endif
    return out;
}
}  // namespace

// ── N5 等待镜子 ─────────────────────────────────────────────────────────────
// 「让它等了多久」＝从会话进入等人的那一刻，到人接话那一刻。跨扫描的状态机：
// 进入 NEEDS_USER/AWAIT 记起点；转 WORKING（人接话）或 FAILED（等待中失败）闭合入账；
// 转 RESTING（人一直没来，20 分钟自然凉）不闭合不记——混进去就是编数。
namespace {
std::string await_path() {
    if (const char* over = getenv("AGENTPING_STATE_DIR"); over && *over)
        return std::string(over) + "/await.txt";
    const char* h = getenv("HOME");
    if (!h || !*h) return "";
    return std::string(h) + "/Library/Application Support/AgentPing/await.txt";
}
std::string& await_key_of(const AgentSnap& a, const Session& x) {
    static std::string k;
    k = a.id + ":" + x.id;
    return k;
}
}  // namespace

void roll_await(Snapshot& s) {
    static std::map<std::string, std::pair<bool, double>> open_wait;   // key → (记着, 起点)
    const std::string p = await_path();
    std::tm tv0{};
    const std::time_t t00 = (std::time_t)s.window_start;
    localtime_r(&t00, &tv0);
    const long today = (long)tv0.tm_year * 10000L + (long)(tv0.tm_mon + 1) * 100L + (long)tv0.tm_mday;
    for (const auto& a : s.agents) {
        if (!a.available) continue;
        for (const auto& x : a.sessions) {
            const std::string& k = await_key_of(a, x);
            const bool waiting = (x.state == ST_NEEDS_USER || x.state == ST_AWAIT);
            auto it = open_wait.find(k);
            if (waiting) {
                if (it == open_wait.end()) open_wait[k] = {true, s.scanned_at};
            } else if (it != open_wait.end()) {
                const double dt = s.scanned_at - it->second.second;
                open_wait.erase(it);
                // 只有「人接话」和「等待中失败」两种闭合入账；RESTING（人没来）直接丢弃。
                if ((x.state == ST_WORKING || x.state == ST_FAILED) && dt >= 5.0 && !p.empty()) {
                    std::ofstream f(p, std::ios::app);
                    if (f) f << today << " " << (long)dt << "\n";
                }
            }
        }
    }
    // 本周聚合：周一零点起的已闭合间隔。文件一行一条，量级很小，逐轮直读。
    if (p.empty()) return;
    std::tm tv{};
    const std::time_t t0 = (std::time_t)s.window_start;
    localtime_r(&t0, &tv);
    const int wd = tv.tm_wday;
    std::tm mon = tv;
    mon.tm_mday -= (wd == 0 ? 6 : wd - 1);
    mon.tm_hour = 12;
    mktime(&mon);
    const long week_from = (long)mon.tm_year * 10000L + (long)(mon.tm_mon + 1) * 100L + (long)mon.tm_mday;
    long n = 0;
    double sum = 0, mx = 0;
    std::ifstream f(p);
    long d; long sec;
    while (f >> d >> sec) {
        if (d < week_from) continue;
        n++;
        sum += (double)sec;
        if ((double)sec > mx) mx = (double)sec;
    }
    s.await_n = n;
    s.await_avg = n > 0 ? sum / (double)n : 0.0;
    s.await_max = mx;
}

void apply_proc_sentinel(Snapshot& snap) {
    static std::set<std::string> cached;
    static double cached_at = -60;
    // 进程表 3 秒扫一次没意义（进程起落没那么快），30 秒足够新鲜且省一遍 sysctl。
    if (snap.scanned_at - cached_at >= 30) { cached = live_proc_comms(); cached_at = snap.scanned_at; }
    for (auto& a : snap.agents) {
        if (a.id != "codex") continue;          // comm 可辨的路才判（见上）
        if (cached.count("codex") > 0) continue;
        for (auto& s : a.sessions)
            if (s.state == ST_WORKING) s.proc_dead = true;
    }
}

// ── M9 日聚合 ───────────────────────────────────────────────────────────────
// append-only 的 daily.txt：一行一天「YYYYMMDD tokens credits」。今天那行每轮幂等重写
// （当天数字还会涨），历史行永不回改。第一天上线只有一行，柱图就少几根——不回填，不编。
namespace {
std::string daily_path() {
    const char* over = getenv("AGENTPING_STATE_DIR");
    if (over && *over) return std::string(over) + "/daily.txt";
    const char* h = getenv("HOME");
    if (!h || !*h) return "";
    return std::string(h) + "/Library/Application Support/AgentPing/daily.txt";
}
/// v8·M16：分模型日聚合，与 daily.txt 同目录同规矩（append-only、今天幂等重写、不回填）。
std::string models_daily_path() {
    const char* over = getenv("AGENTPING_STATE_DIR");
    if (over && *over) return std::string(over) + "/models_daily.txt";
    const char* h = getenv("HOME");
    if (!h || !*h) return "";
    return std::string(h) + "/Library/Application Support/AgentPing/models_daily.txt";
}
long day_key(double epoch) {
    std::time_t t = (std::time_t)epoch;
    std::tm tv;
    localtime_r(&t, &tv);
    return (long)tv.tm_year * 10000 + (long)(tv.tm_mon + 1) * 100 + (long)tv.tm_mday;
}
}  // namespace

void roll_daily(Snapshot& s) {
    const std::string p = daily_path();
    if (p.empty()) return;
    const long today = day_key(s.window_start);
    // 2026-11-12 起按工具落盘：一行一天一工具「day agent tokens credits in out cr cw 5h% wk%」；
    // 旧格式「day tokens credits」读回时折算成 "*"（全局）行——周/月总量与柱图照旧成立。
    // 后六列是 v5 追加的：四档明细与当天最后一轮额度快照。老行没有就留 0/-1，
    // 界面说「本段没有四档记录」，不按总量编一个明细。
    std::map<long, std::map<std::string, Snapshot::DayUse>> per;   // day → agent → 量
    {
        std::ifstream f(p);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::istringstream is(line);
            long d;
            std::string who;
            Snapshot::DayUse u;
            if (is >> d >> who >> u.tokens >> u.credits) {
                u.day = d;
                long in = 0, out = 0, cr = 0, cw = 0;
                if (is >> in >> out >> cr >> cw) {
                    u.parts.input = in;
                    u.parts.output = out;
                    u.parts.cache_read = cr;
                    u.parts.cache_write = cw;
                }
                int f5 = -1, wk = -1;
                if (!(is >> f5 >> wk)) { f5 = -1; wk = -1; }
                u.five_pct = f5;
                u.week_pct = wk;
                if (d != today) per[d][who] = u;                 // 今天整段重写
        } else {
            // 旧三列格式：day tokens credits → "*" 行
            std::istringstream is2(line);
            long d2; long tk; double cr;
            if (is2 >> d2 >> tk >> cr && d2 != today)
                per[d2]["*"] = {d2, tk, cr};
        }
    }
    // ── 缺日回填：账本当缓存，缺的天从原始源补齐 ─────────────────────
    // 近 30 天（不含今天；更早的本年天仍「自首个记录」如实缺着）。某天缺哪家的行
    // 就只补哪家，补过一次就落在文件里不再碰；回填日全零＝「当天记录为 0」，
    // 不是「无记录」——四家的源本机都在，问过就是答过。codex/pi/opencode/qwenwork
    // 不回填（见文件头注释），它们的行缺着就如实缺着。
    {
        const std::string h = home();
        struct Bk { const char* id; const char* root; bool subtract; };
        const Bk kSrc[3] = {
            {"claude", ".claude/projects", false},
            {"qoder", ".qoder/projects", false},
            {"workbuddy", ".workbuddy/projects", true},
        };
        const std::string zdb = join(h, ".zcode/cli/db/db.sqlite");
        for (int i = 1; i <= 30; ++i) {
            long key = 0;
            double d0 = 0, d1 = 0;
            local_day_before((double)s.window_start, i, key, d0, d1);
            auto has = [&](const char* id) {
                auto it = per.find(key);
                return it != per.end() && it->second.count(id) > 0;
            };
            bool need = false;
            for (const Bk& b : kSrc) if (!has(b.id)) { need = true; break; }
            if (!need && !has("zcode")) need = true;
            if (!need) continue;
            bool any = false;
            for (const Bk& b : kSrc) {
                if (has(b.id)) continue;
                FStat rs = fst(join(h, b.root));
                if (!rs.ok || !rs.dir) continue;   // 本机没这个源：不写 0 行，如实「无记录」
                HistSum ys;
                sum_jsonl_window(join(h, b.root), d0, d1, b.subtract, ys);
                Snapshot::DayUse u;
                u.day = key;
                u.tokens = ys.parts.total();
                u.credits = ys.credits;
                u.parts = ys.parts;
                u.five_pct = -1;
                u.week_pct = -1;
                per[key][b.id] = u;
                any = true;
            }
#ifdef AP_HAVE_SQLITE
            if (!has("zcode")) {
                HistSum ys;
                if (zcode_day_sum(zdb, d0, d1, ys)) {
                    Snapshot::DayUse u;
                    u.day = key;
                    u.tokens = ys.parts.total();
                    u.credits = ys.credits;
                    u.parts = ys.parts;
                    u.five_pct = -1;
                    u.week_pct = -1;
                    per[key]["zcode"] = u;
                    any = true;
                }
            }
#endif
            if (any) {
                // 全局行从当天的全部工具行重建（不能只记本次增量：分次补进来的
                // 工具行——昨天先补了 workbuddy、今天补 zcode——都会被漏掉）。
                Snapshot::DayUse g;
                g.day = key;
                for (const auto& [who, u] : per[key]) {
                    if (who == "*") continue;
                    g.tokens += u.tokens;
                    g.credits += u.credits;
                    g.parts.input += u.parts.input;
                    g.parts.output += u.parts.output;
                    g.parts.cache_read += u.parts.cache_read;
                    g.parts.cache_write += u.parts.cache_write;
                }
                g.five_pct = -1;
                g.week_pct = -1;
                per[key]["*"] = g;
            }
        }
    }
    }
    auto remain_pct = [](const QuotaWindow& q) -> int {
        if (!q.known) return -1;
        double v = (1.0 - q.used) * 100.0;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        return (int)std::lround(v);
    };
    UsageParts all_parts;
    for (const auto& a : s.agents) {
        Snapshot::DayUse u;
        u.day = today;
        u.tokens = a.usage.total();
        // Qoder 的 JSONL 没有 token 明细、只有 credits：量记在 credits 列，单位各归各。
        u.credits = 0;
        for (const auto& se : a.sessions)
            if (se.last_activity >= s.window_start) u.credits += se.credits;
        u.parts = a.usage;
        u.five_pct = remain_pct(a.quota.five);
        u.week_pct = remain_pct(a.quota.week);
        all_parts.input += u.parts.input;
        all_parts.output += u.parts.output;
        all_parts.cache_read += u.parts.cache_read;
        all_parts.cache_write += u.parts.cache_write;
        per[today][a.id] = u;
    }
    per[today]["*"] = {today, s.today_tokens, s.today_credits, all_parts, -1, -1};
    {
        std::ofstream f(p);
        for (const auto& [d, m] : per)
            for (const auto& [who, u] : m)
                f << d << " " << who << " " << u.tokens << " " << u.credits
                  << " " << u.parts.input << " " << u.parts.output
                  << " " << u.parts.cache_read << " " << u.parts.cache_write
                  << " " << u.five_pct << " " << u.week_pct << "\n";
    }
    // v8·M16：分模型日聚合（models_daily.txt）。一行一天一工具一模型：
    // day agent model tokens credits in out cr cw sessions has_parts。
    // 只从落地日起积累、不回填；旧段没有分模型记录时界面如实说「只有工具级」，不拿工具总量拆假模型。
    {
        const std::string mp = models_daily_path();
        std::map<long, std::map<std::string, std::map<std::string, ModelUse>>> mper;
        if (!mp.empty()) {
            std::ifstream mf(mp);
            std::string line;
            while (std::getline(mf, line)) {
                if (line.empty()) continue;
                std::istringstream is(line);
                long d; std::string who, nm; ModelUse mu; long hp = 0;
                if (is >> d >> who >> nm >> mu.tokens >> mu.credits >> mu.parts.input
                       >> mu.parts.output >> mu.parts.cache_read >> mu.parts.cache_write
                       >> mu.sessions >> hp) {
                    if (d == today) continue;   // 今天整段重写
                    mu.name = nm; mu.has_parts = hp != 0;
                    mper[d][who][nm] = mu;
                }
            }
        }
        for (const auto& a : s.agents)
            for (const auto& mu : a.models)
                if (mu.tokens > 0 || mu.credits > 0) mper[today][a.id][mu.name] = mu;
        if (!mp.empty()) {
            std::ofstream mf(mp);
            for (const auto& [d, m] : mper)
                for (const auto& [who, mm] : m)
                    for (const auto& [nm, mu] : mm)
                        mf << d << " " << who << " " << nm << " " << mu.tokens << " " << mu.credits
                           << " " << mu.parts.input << " " << mu.parts.output
                           << " " << mu.parts.cache_read << " " << mu.parts.cache_write
                           << " " << mu.sessions << " " << (mu.has_parts ? 1 : 0) << "\n";
        }
        s.model_daily = mper;
    }
    // 快照带整份按工具表（时段分工具消耗用）。8 天截断是旧版柱图留下的，v5 的月图
    // 要看到本月一号起，所以这里不截；历史行仍是 append-only，不回改。
    s.daily.clear();
    s.agent_daily = per;
    s.agent_today = per.count(today) ? per[today] : std::map<std::string, Snapshot::DayUse>{};
    for (auto it = per.rbegin(); it != per.rend(); ++it) {
        auto g = it->second.find("*");
        if (g == it->second.end()) continue;
        s.daily.push_back(g->second);
    }
    std::reverse(s.daily.begin(), s.daily.end());
    std::time_t t = (std::time_t)s.window_start;
    std::tm tv;
    localtime_r(&t, &tv);
    tv.tm_hour = 0; tv.tm_min = 0; tv.tm_sec = 0;
    tv.tm_wday = 0;
    const long monday_key = [&]() {
        std::tm m = tv;
        m.tm_mday -= (m.tm_wday == 0 ? 6 : m.tm_wday - 1);
        mktime(&m);
        return day_key(mktime(&m));
    }();
    std::tm m0 = tv;
    m0.tm_mday = 1;
    const long month_key = day_key(mktime(&m0));
    s.week_tokens = s.month_tokens = 0;
    for (const auto& [d, m] : per) {
        auto g = m.find("*");
        if (g == m.end()) continue;
        if (d >= monday_key) s.week_tokens += g->second.tokens;
        if (d >= month_key) s.month_tokens += g->second.tokens;
    }
}

// ── M2 趋势（估算）─────────────────────────────────────────────────────────
// 只做差分：每工具保留最近 ~6 分钟的 (seen_at, 5h窗剩余%) 读数，取首尾差算每小时几个点。
// 标「估算」且不预测耗尽时刻。读数不足两根（刚启动/读数不更新）known=false 整行不画。
namespace {
std::map<std::string, std::deque<std::pair<double, double>>>& trend_buf() {
    static std::map<std::string, std::deque<std::pair<double, double>>> m;
    return m;
}
}

void compute_trends(Snapshot& s) {
    for (const auto& a : s.agents) {
        const ap::QuotaWindow& q = a.quota.five;
        if (!q.known || q.seen_at <= 0 || q.resets_at <= 0) continue;
        const double pct = q.used > 0 && q.used <= 1.0 ? (1.0 - q.used) * 100.0
                        : (q.used > 1.0 ? std::max(0.0, 100.0 - q.used) : 0.0);
        auto& buf = trend_buf()[a.id];
        if (buf.empty() || buf.back().first != q.seen_at)
            buf.emplace_back(q.seen_at, pct);
        while (buf.size() > 140) buf.pop_front();          // ~7 分钟 @3s
        if (buf.size() >= 2) {
            const double dt = buf.back().first - buf.front().first;
            const double dp = buf.back().second - buf.front().second;   // 剩余%的变化（负=在耗）
            if (dt >= 60.0) {                              // 至少一分钟跨度才敢差分
                Snapshot::Trend& t = s.trend5[a.id];
                t.known = true;
                t.pct_per_hour = dp / dt * 3600.0;
                for (const auto& pt : buf) t.recent.push_back(pt.second);
            }
        }
    }
}

}  // namespace ap
