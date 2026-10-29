#include "scan.h"

#include "jsonl.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
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
    long tokens = 0;
    double credits = 0;
    UsageParts usage;                  // 今天（或窗内）四档；tokens 与它同一口径
    std::string model;                 // 窗内用得最多的模型
    std::map<std::string, long> models;
    std::array<long, 24> hourly{};     // 本机 0–23 点
    long y_tokens = 0;      // 昨天同一个窗内的量，只喂表头的「较昨日」
    double y_credits = 0;
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
                if (!cmd.empty()) r.last_cmd = first_line(cmd);
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
                }
                if (msg) {
                    if (const std::string* m = msg->str_at("model")) {
                        if (!m->empty() && *m != "<synthetic>")
                            r.models[*m] += p.total();
                    }
                }
            } else {
                r.y_tokens += p.total();
                r.y_credits += cr;
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
        if (kv.second > best) { best = kv.second; r.model = kv.first; }

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
            s.last_activity = r.last_ts;
            s.error_text = r.error_text;
            s.state = derive(r, now, s.inferred_wait);
            s.turns = r.turns;
            s.spark = r.spark;
            // 「连续」只在起点也落在今天时成立；跨夜就从今天零点起算，免得把昨天的
            // 时间算进「连续」。面板那边拿到的是秒数，画不画由它按 span_sec>0 判。
            s.first_activity = r.first_ts >= since ? r.first_ts : 0;
            s.last_cmd = r.last_cmd;
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
                        if (m.name == kv.first) { m.tokens += kv.second; found = true; break; }
                    if (!found) snap.models.push_back({kv.first, kv.second});
                }
                for (int h = 0; h < 24; h++) snap.hourly[h] += r.hourly[h];
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
        std::string project = "Codex", title, err, last_cmd;
        long tokens = 0;
        long prev_total = 0;
        UsageParts prev_parts, total_parts;
        std::array<long, 24> hourly{};
        std::map<std::string, long> models;
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
                if (!model.empty()) models[model] += (d >= since ? delta.total() : 0);

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
                    if (const std::string* pl = rl->str_at("plan_type")) if (!pl->empty()) quota.plan = *pl;
                    const Json* cr = rl->at("credits");
                    if (cr) {
                        const Json* bal = cr->at("balance");
                        if (bal && bal->kind == Json::NUM) {
                            quota.credits_balance = bal->num;
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
            if (kv.second <= 0) continue;
            bool found = false;
            for (auto& m : snap.models)
                if (m.name == kv.first) { m.tokens += kv.second; found = true; break; }
            if (!found) snap.models.push_back({kv.first, kv.second});
        }
        if (quota.five.seen_at > snap.quota.five.seen_at) snap.quota.five = quota.five;
        if (quota.week.seen_at > snap.quota.week.seen_at) snap.quota.week = quota.week;
        if (quota.credits_known && !snap.quota.credits_known) {
            snap.quota.credits_balance = quota.credits_balance;
            snap.quota.credits_known = true;
        }
        if (!quota.plan.empty()) snap.quota.plan = quota.plan;
        snap.quota.known = snap.quota.five.known || snap.quota.week.known || snap.quota.credits_known;

        long best = 0;
        std::string top_model;
        for (const auto& kv : models)
            if (kv.second > best) { best = kv.second; top_model = kv.first; }

        Session s;
        s.id = "codex-" + sid;
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

std::string col_text(sqlite3_stmt* st, int i) {
    const char* p = (const char*)sqlite3_column_text(st, i);
    return p ? std::string(p) : std::string();
}

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
        q << "select session_id, "
             "sum(coalesce(json_extract(data,'$.tokens.input'), 0)), "
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
    // 按本机小时分桶：一条 group by 就够了，别为 24 格再读一遍表。
    {
        std::ostringstream q;
        q << "select cast(strftime('%H', time_created/1000, 'unixepoch', 'localtime') as integer) h, "
             "sum(coalesce(json_extract(data,'$.tokens.input'), 0) "
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
        if (now - s.last_activity > kNeedsUserStale) s.state = ST_RESTING;
        snap.sessions.push_back(std::move(s));
    }
    sqlite3_finalize(st);

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
        if (age >= kStalledToolUse) { s.state = ST_AWAIT; s.inferred_wait = true; }
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
                    const Json* pd = rec.at("providerData");
                    const Json* u = pd ? pd->at("usage") : nullptr;
                    if (!u) return;
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
    return snap;
}

/// OpenCode（开源 CLI，macOS/Linux 同一个路径）：~/.local/share/opencode/opencode.db。
/// 它的库自己就把用量按轮记全了（part 里的 step-finish），所以窗内词元能从源头按天切，
/// 不必拿 session 表那列整生累计值冒充「今天」。
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
    // 按本机小时分桶；用量在 part 上，message 那一侧没有 tokens。
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
    if (sqlite3_prepare_v2(db, q.str().c_str(), -1, &st, nullptr) != SQLITE_OK) {
        snap.scan_error = std::string("查询失败：") + sqlite3_errmsg(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return snap;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::string sid = col_text(st, 0);
        std::string title = trim(col_text(st, 1));
        std::string dir = col_text(st, 2);
        long long upd = sqlite3_column_int64(st, 3);
        int running = (int)sqlite3_column_int64(st, 4);
        if (upd <= 0) continue;

        Session s;
        s.id = "opencode-" + sid;
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
        snap.usage.input += s.usage.input;
        snap.usage.output += s.usage.output;
        snap.usage.cache_read += s.usage.cache_read;
        snap.usage.cache_write += s.usage.cache_write;
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
        if (now - s.last_activity > kNeedsUserStale && s.state != ST_FAILED) s.state = ST_RESTING;
        snap.sessions.push_back(std::move(s));
    }
    sqlite3_finalize(st);
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

/// 问句与候选项。实测形状：
///   details.message            —— 「Allow Bash to run: …」这类短提示
///   details.input.questions[]  —— AskUserQuestion：{question, header, options[{label,description}]}
/// input 实测见过字典、也见过写成字符串，两种都接。
void hook_question(const Json& obj, std::string& q, std::vector<std::string>& opts,
                   std::vector<std::string>& desc) {
    const Json* det = obj.at("details");
    const std::string* top_msg = obj.str_at("message");
    if (!det || det->kind != Json::OBJ) {
        if (top_msg) q = hook_one_line(*top_msg, 160);
        return;
    }
    Json parsed;
    const Json* input = det->at("input");
    if (input && input->kind == Json::STR) {
        if (json_parse(input->str.c_str(), input->str.size(), parsed)) input = &parsed;
        else input = nullptr;
    } else if (!input || input->kind != Json::OBJ) {
        input = nullptr;
    }
    const std::string* det_msg = det->str_at("message");
    const std::string* fallback = det_msg ? det_msg : top_msg;

    const Json* qs = input ? input->at("questions") : nullptr;
    if (!qs || qs->kind != Json::ARR || qs->arr.empty()) {
        if (fallback) q = hook_one_line(*fallback, 160);
        return;
    }
    const Json& first = qs->arr.front();
    const std::string* fq = first.str_at("question");
    if (fq) q = hook_one_line(*fq, 160);
    else if (fallback) q = hook_one_line(*fallback, 160);
    const Json* os = first.at("options");
    if (os && os->kind == Json::ARR)
        for (std::size_t i = 0; i < os->arr.size() && opts.size() < 3; i++) {
            const std::string* lb = os->arr[i].str_at("label");
            if (!lb || lb->empty()) continue;
            opts.push_back(hook_one_line(*lb, 48));
            const std::string* ds = os->arr[i].str_at("description");
            desc.push_back(ds ? hook_one_line(*ds, 120) : std::string());
        }
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
        if (asks) s.await_since = g.at;
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

Snapshot scan_all(double now_epoch) {
    double now = now_epoch > 0 ? now_epoch : (double)time(nullptr);
    double since = window_start(now);
    Snapshot snap;
    snap.scanned_at = now;
    snap.window_start = since;

    std::string h = home();
#ifdef AP_HAVE_SQLITE
    std::map<std::string, std::string> titled =
        sidebar_titles(join(h, "Library/Application Support/com.qoder.app.stable/main.sqlite"));
#else
    std::map<std::string, std::string> titled;
#endif
    snap.agents.push_back(scan_transcript("qoder", "Qoder CLI", join(h, ".qoder/projects"), now, since, titled,
                                          &snap.yesterday_tokens, &snap.yesterday_credits));
    // Claude 的标题写在日志里（customTitle/agentName），不借 Qoder 侧栏那张表。
    snap.agents.push_back(scan_transcript("claude", "Claude Code", join(h, ".claude/projects"), now, since, {},
                                          &snap.yesterday_tokens, &snap.yesterday_credits));
    snap.agents.push_back(scan_codex("Codex", join(h, ".codex/sessions"), now, since));
#ifdef AP_HAVE_SQLITE
    snap.agents.push_back(scan_zcode("ZCode", join(h, ".zcode/cli/db/db.sqlite"), now, since));
    snap.agents.push_back(scan_workbuddy("WorkBuddy", join(h, ".workbuddy/workbuddy.db"),
                                         join(h, ".workbuddy/projects"), now, since));
    // OpenCode 走 XDG 数据目录（macOS 上它自己也落在这条路径），没设环境变量就按默认。
    {
        const char* xd = getenv("XDG_DATA_HOME");
        std::string root = (xd && *xd) ? std::string(xd) : join(h, ".local/share");
        snap.agents.push_back(scan_opencode("OpenCode", join(root, "opencode/opencode.db"), now, since));
    }
#endif

    for (const AgentSnap& a : snap.agents)
        for (const Session& s : a.sessions)
            if (s.last_activity >= since) { snap.today_tokens += s.tokens; snap.today_credits += s.credits; }
    apply_hook_signals(snap, now);
    return snap;
}

std::string snapshot_dump(const Snapshot& s) {
    char buf[512];
    std::string out;
    snprintf(buf, sizeof buf,
             "scanned_at=%.0f window_start=%.0f today_credits=%.2f today_tokens=%ld "
             "| yesterday_credits=%.2f yesterday_tokens=%ld | hook 落地%d/有效%d/解析不了%d\n",
             s.scanned_at, s.window_start, s.today_credits, s.today_tokens,
             s.yesterday_credits, s.yesterday_tokens, s.hook_landed, s.hook_live, s.hook_dropped);
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
            out += "\n";
        }
        for (const Session& x : a.sessions) {
            // 按字节截（%.70s）会把 UTF-8 砍成半个字，这份 dump 是对账用的，必须可读
            snprintf(buf, sizeof buf, "  %-10s %-14s %.0f  tok=%ld cr=%.2f turn=%d q=%d  %s%s%s\n",
                     state_name(x.state), utf8_clip(x.project, 14, "").c_str(), x.last_activity,
                     x.tokens, x.credits, x.turns, x.queue_pos, utf8_clip(x.title, 56, "…").c_str(),
                     x.question.empty() ? "" : "  ⟵", x.question.empty() ? "" : utf8_clip(x.question, 40, "…").c_str());
            out += buf;
        }
    }
    return out;
}

std::string hook_inbox_dir() { return hook_dir(); }

}  // namespace ap
