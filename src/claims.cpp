#include "claims.h"

#include "jsonl.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ap {
namespace {

std::string home_dir() {
    const char* h = getenv("HOME");
    return (h && *h) ? std::string(h) : std::string("/");
}

double mtime_of(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? (double)st.st_mtime : 0;
}

/// `[2026-10-29T14:29:18.181Z]` 这种行首时间戳 → epoch 秒（按 UTC 读）。取不到返回 0。
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

/// 本机当天 00:00 —— 签到窗按他那台机器的日子翻，不是 UTC 零点。
double local_midnight(double now) {
    const time_t tt = (time_t)now;
    struct tm lt{};
    ::localtime_r(&tt, &lt);
    lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
    return (double)::mktime(&lt);
}

/// `[2026-10-30 00:12:38.982]` —— ZCode 自己那份 app 日志的行首时间戳。
/// 它与 parse_bracket_iso 不是一支：不带 Z、也不当 UTC 读（实测 00:12:39 那条的
/// server_time 换成本地正是 00:12:39，两者同一天同一秒）。取不到返回 0。
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

/// 从文件尾往前按 512 KB 一块扫，返回命中 needle 的整行，新的在前。
/// cap_bytes 限住最多读多少：面板每几秒扫一轮，不能让一个大日志把这一轮吃掉。
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
        buf += carry;                       // 前一块的开头接在这一块之后
        std::vector<std::string> parts;
        std::size_t s = 0;
        while (true) {
            const std::size_t nl = buf.find('\n', s);
            if (nl == std::string::npos) { parts.push_back(buf.substr(s)); break; }
            parts.push_back(buf.substr(s, nl - s));
            s = nl + 1;
        }
        if (pos > 0 && !parts.empty()) {    // 块边界把一行切开了，那半行攒到下一轮
            carry = parts.front();
            parts.erase(parts.begin());
        } else {
            carry.clear();
        }
        for (auto it = parts.rbegin(); it != parts.rend() && out.size() < max_hits; ++it) {
            if (it->find(needle) != std::string::npos) out.push_back(std::move(*it));
        }
        if (got < (ssize_t)n) break;        // 短读＝到文件头了
    }
    ::close(fd);
    if (out.size() > max_hits) out.resize(max_hits);
    return out;
}

const Json* payload_of(const Json& rec) {
    // WorkBuddy 的形状：{"message":["[Checkin] …", {…}]}，真正的内容在 message[1]
    const Json* m = rec.at("message");
    if (!m || m->kind != Json::ARR || m->arr.size() < 2) return nullptr;
    const Json* p = &m->arr[1];
    return p->kind == Json::OBJ ? p : nullptr;
}

long num_at(const Json* o, const char* k, bool* got) {
    if (got) *got = false;
    if (!o) return 0;
    const Json* v = o->at(k);
    if (!v || v->kind != Json::NUM) return 0;
    if (got) *got = true;
    return (long)(v->num + 0.5);
}

bool bool_at(const Json* o, const char* k, bool* got) {
    if (got) *got = false;
    if (!o) return false;
    const Json* v = o->at(k);
    if (!v || v->kind != Json::BOOL) return false;
    if (got) *got = true;
    return v->boolean;
}

const std::string* str_at(const Json* o, const char* k) {
    if (!o) return nullptr;
    const Json* v = o->at(k);
    return (v && v->kind == Json::STR) ? &v->str : nullptr;
}

// ── WorkBuddy：每日签到 ───────────────────────────────────────────────────
// 源：~/.workbuddy/logs/main.log 的 `[Checkin] fetchCheckinStatus success` JSON 行。
// 原文实测（10-29 23:18）：active/today_checked_in/streak_days/today_credit 四档齐全，
// 所以这是三家里唯一「本机就能把这一期说完整」的：能不能领、领了没、面值、连击都有。
// 节律量过三个夜晚：10-27 00:00:48 / 10-28 00:00:49 / 10-29 00:00:59 都看到
// today_checked_in 从 true 掉回 false ⇒「本期从本机 00:00 起可见」是实测的。
// 失效时刻相反：10-29 从 00:00 到 23:18 都没等到它翻回未领，服务端也没给 endAt 字段，
// 所以 expires_at 留 0，时间轨画虚线，不许写成「当天 24:00 作废」。
void from_workbuddy(Claim& c, double now) {
    c.vendor = "workbuddy";
    c.name = "WorkBuddy";
    c.source = home_dir() + "/.workbuddy/logs/main.log";
    c.log_mtime = mtime_of(c.source);
    if (!c.log_mtime) { c.note_key = "cl_note_wb_nolog"; return; }
    const auto lines = tail_lines(c.source, "[Checkin] fetchCheckinStatus success", 1, 8ull << 20);
    if (lines.empty()) {
        c.note_key = "cl_note_wb_noline";
        return;
    }
    Json rec;
    if (!json_parse(lines.front().c_str(), lines.front().size(), rec)) {
        c.note_key = "cl_note_wb_badjson";
        return;
    }
    c.seen_at = json_date_at(rec, "timestamp");
    const Json* p = payload_of(rec);
    bool got = false;
    const bool active = bool_at(p, "active", &got);
    const bool checked = bool_at(p, "today_checked_in", &got);
    if (!got) { c.note_key = "cl_note_wb_nofield"; return; }
    c.known = true;
    c.none_reported = true;   // active／today_checked_in 都是它自己写的布尔
    c.done = checked;
    c.ready = active && !checked;
    const long credit = num_at(p, "today_credit", &got);
    if (got) { c.credit = credit; c.credit_known = true; }
    const long streak = num_at(p, "streak_days", &got);
    if (got) { c.streak = (int)streak; c.streak_known = true; }
    c.appears_at = local_midnight(now);
    c.note_key = checked ? "cl_note_wb_done"
               : (active ? "cl_note_wb_ready" : "cl_note_wb_inactive");
}

// ── Qoder：活动卡 ─────────────────────────────────────────────────────────
// 源：logs/<启动目录>/main.log 里 [Campaign] 前缀的三类行，逐条实测于 2026-10-29：
//   ①「通知状态已更新」{"hasClaimableCampaign":bool,"shouldShowCampaignPopup":bool,
//       "notificationCount":int} —— 它自己轮询完写的。43 条里 true 7 条 / false 36 条，
//       true 集中在 10-28 21:08–21:24（本地），22:22 起翻回 false ⇒ 这个布尔真会翻，
//       「有卡了」这件事本机拿得到。
//   ②「活动 Surface 已打开」{...,"claimable":bool,"source":"settings_menu",...} —— 他自己
//       点开过那张网页。10-28 21:24 那条就带着 claimable:true。用来去重：看过就不催。
//   ③「活动状态请求返回」{...,"payload":{"campaigns":[{campaignKey,actionType,startAt,
//       endAt,claimStatus}]}} —— 服务端的完整清单，endAt 是真失效时刻。但这型行只在
//       10-22 那几份日志里有（10-23 之后一次没写过），所以「有没有 deadline」取决于它当时
//       写不写这条，不能当成永远拿得到。
//   placements[].content 那层在日志里是字面量 "maximum log depth reached"：卡面文案与面值
//   被它自己的日志深度截断，本机拿不到 —— 这条是量出来的，不是猜的。
struct QoderEvents {
    struct Poll { double ts = 0; bool claimable = false; int notif = 0; bool has_notif = false; };
    std::vector<Poll> poll;                         // 新在前
    double opened_at = 0; bool opened = false;
    double deadline = 0; bool has_deadline = false;
    double list_at = 0;                             // 最后一份服务端清单的时刻
    bool all_claimed = false, any_pending = false;
    bool any = false;
};

void feed_qoder_line(const std::string& ln, QoderEvents& ev) {
    const std::size_t brace = ln.find('{');
    if (brace == std::string::npos) return;
    Json rec;
    if (!json_parse(ln.c_str() + brace, ln.size() - brace, rec)) return;
    const double ts = parse_bracket_iso(ln);
    bool got = false;
    if (ln.find("通知状态已更新") != std::string::npos) {
        const bool claimable = bool_at(&rec, "hasClaimableCampaign", &got);
        if (!got) return;
        ev.any = true;
        QoderEvents::Poll p;
        p.ts = ts;
        p.claimable = claimable;
        const long n = num_at(&rec, "notificationCount", &got);
        if (got) { p.notif = (int)n; p.has_notif = true; }
        ev.poll.push_back(p);
        return;
    }
    if (ln.find("活动 Surface 已打开") != std::string::npos) {
        ev.any = true;
        ev.opened = true;
        ev.opened_at = std::max(ev.opened_at, ts);
        return;
    }
    if (ln.find("活动状态请求返回") != std::string::npos) {
        const Json* pay = rec.at("payload");
        const Json* cs = pay ? pay->at("campaigns") : nullptr;
        if (!cs || cs->kind != Json::ARR) return;
        ev.any = true;
        ev.list_at = std::max(ev.list_at, ts);
        double pending_end = 0;
        for (const Json& one : cs->arr) {
            const std::string* st = str_at(&one, "claimStatus");
            const bool claimed = st && *st == "CLAIMED";
            claimed ? ev.all_claimed = true : ev.any_pending = true;
            bool dgot = false;
            const long end = num_at(&one, "endAt", &dgot);
            // 只有「还没领的那一期」的 endAt 才是界面的失效时刻；已领那期的日子跟当前无关。
            if (dgot && end > 0 && !claimed)
                pending_end = pending_end == 0 ? (double)end : std::min(pending_end, (double)end);
        }
        if (pending_end > 0 && (ev.deadline == 0 || pending_end > ev.deadline)) {
            ev.deadline = pending_end;      // 取最新一份清单的，不按最小值跨期混
            ev.has_deadline = true;
        }
    }
}

std::string newest_qoder_log(std::vector<std::string>* rest) {
    const std::string dir = home_dir() + "/Library/Application Support/com.qoder.app.stable/logs";
    DIR* d = ::opendir(dir.c_str());
    if (!d) return "";
    std::vector<std::pair<double, std::string>> files;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string p = dir + "/" + e->d_name + "/main.log";
        const double m = mtime_of(p);
        if (m > 0) files.emplace_back(m, std::move(p));
    }
    ::closedir(d);
    std::sort(files.begin(), files.end(), [](const std::pair<double, std::string>& a,
                                            const std::pair<double, std::string>& b) {
        return a.first > b.first;
    });
    for (std::size_t i = 1; i < files.size() && rest && rest->size() < 7; i++)
        rest->push_back(files[i].second);
    return files.empty() ? std::string() : files.front().second;
}

void from_qoder(Claim& c) {
    c.vendor = "qoder";
    c.name = "Qoder";
    std::vector<std::string> rest;
    const std::string newest = newest_qoder_log(&rest);
    if (newest.empty()) { c.note_key = "cl_note_qd_nodir"; return; }
    c.source = newest;
    c.log_mtime = mtime_of(newest);

    QoderEvents ev;
    auto eat = [&](const std::string& f, std::size_t cap) {
        for (const auto& ln : tail_lines(f, "[Campaign]", cap, 6ull << 20)) feed_qoder_line(ln, ev);
    };
    eat(newest, 400);
    // 最新那份里翻不到翻转，就往前几份启动日志找（「上一次有得领是几点」跨重启才看得出来）
    bool flipped = ev.poll.size() < 2;
    for (std::size_t i = 0; !flipped && i + 1 < ev.poll.size(); i++)
        if (ev.poll[i].claimable != ev.poll[i + 1].claimable) flipped = true;
    if (!flipped) for (const auto& f : rest) eat(f, 200);
    // 服务端完整清单（带每期 claimStatus／endAt）只在它肯写 payload 的那几版日志里有，
    // 而且写在哪一份不确定 —— 单独一遍小预算专找它，读到就当出处，读不到就明说没落盘。
    if (ev.list_at == 0) {
        for (const auto& f : rest) {
            for (const auto& ln : tail_lines(f, "活动状态请求返回", 3, 3ull << 20))
                feed_qoder_line(ln, ev);
            if (ev.list_at) break;
        }
    }
    if (!ev.any) { c.note_key = "cl_note_qd_noline"; return; }

    std::sort(ev.poll.begin(), ev.poll.end(),
              [](const QoderEvents::Poll& a, const QoderEvents::Poll& b) { return a.ts > b.ts; });
    c.known = true;
    c.none_reported = true;   // hasClaimableCampaign 是它自己轮询完写的布尔
    c.seen_at = ev.poll.empty() ? 0 : ev.poll.front().ts;
    c.ready = ev.poll.empty() ? ev.any_pending : ev.poll.front().claimable;
    if (!ev.poll.empty() && ev.poll.front().has_notif) { c.notif = ev.poll.front().notif; c.notif_known = true; }
    c.opened = ev.opened;

    // appears_at ＝ 当前这个值最早被读到的时刻（往后走到第一次不一样为止）
    double earliest = c.seen_at;
    if (!ev.poll.empty()) {
        for (std::size_t i = 1; i < ev.poll.size(); i++) {
            if (ev.poll[i].claimable != ev.poll.front().claimable) break;
            if (ev.poll[i].ts > 0) earliest = ev.poll[i].ts;
        }
    }
    c.appears_at = earliest;
    // 上一次「有得领」被读到的时刻，用来解释「它现在说没有」到底是本来就没有还是已经没了
    for (const auto& p : ev.poll) if (p.claimable) { c.last_ready_at = p.ts; break; }
    c.list_at = ev.list_at;

    // 服务端清单只有在「不比最后一次轮询旧」时才算数：10-22 那份写着两张都 CLAIMED，
    // 但 10-28 又读到过 claimable=true，那份清单就已被取代 —— 拿它判「已领」是把旧读数当现状。
    const bool list_fresh = ev.list_at != 0 && ev.list_at >= c.last_ready_at && ev.list_at >= c.seen_at;
    if (list_fresh) {
        if (ev.has_deadline) c.expires_at = ev.deadline;
        if (ev.all_claimed && !ev.any_pending) c.done = true;
    }

    // 卡上只留一句「这一期它自己报了什么」＋一个结构性缺口；旧清单为什么不参与判定的推理
    // 留在 claims_probe 的「逐条时刻展开」里（那里连服务端清单的读数时刻一起打）。
    // 面值/卡面文案的缺口有原文依据：它把响应体写进日志时那一段是字面量
    // 「[maximum log depth reached]」，placements[].content 根本没落盘（2026-10-29 实测）。
    c.note_arg_key = c.ready   ? "cl_note_qd_ready"
                   : c.done    ? "cl_note_qd_done"
                   : c.last_ready_at ? "cl_note_qd_none_seen"
                                     : "cl_note_qd_none_never";
    c.note_key = c.opened ? "cl_note_qd_gap_open" : "cl_note_qd_gap";
}

// ── ZCode：额度写在它自己 app 日志的响应体里 ─────────────────────────────
// 2026-10-30 推翻旧结论。旧口径只扫了 CLI 那层（~/.zcode/cli/log/zcode-<日期>.jsonl，
// 那里确实一个字不落），没扫 GUI 那层。真源是 ~/.zcode/v2/logs/<日期>.log 里
// `[coding-plan-availability] billing/balance 请求完成 {…}` 这一行 —— 它每几分钟请求一次
// 并把整个响应体原样写盘。当天实测 44 条，00:12:39 那条：
//   plans[0] = {name:"ZCODE GLOBAL BUILD",status:"active",starts_at:1793271791,
//               ends_at:1793322000,entitlements:[{show_name:"GLM-5.3-Flash",
//               unit_type:"token",grant_units:300000000,period:"one_time"}]}
//   balances[0] 同刻给 total_units:300000000 / used_units:61946662 / remaining_units:238053338。
// ends_at 换成本地正是 10-30 09:00:00，之后每条响应的 plans 都是空 ⇒「本期已过」这条也有读数。
// 三家里只有它给服务端真失效时刻，所以时间轨画实线。面值取 entitlements 的 grant_units（与它
// 屏上那张 Weekend Banner 同一口径：同单位相加、one_time 优先），不是余额。
struct ZcRead {
    double ts = 0;
    bool parsed = false;      // 响应体读出来了（plans 为空也算读出来）
    bool any = false;         // plans[] 非空 = 这一期它有额度
    std::string status;
    double starts = 0, ends = 0;
    /// entitlements[].effective_at —— 额度真正开始可用的时刻。它比 starts_at 晚（10-30 实测
    /// starts_at=19:47:17 是领的那一刻，effective_at=23:00:00 才是它屏上写的「将于…生效」），
    /// 所以「本期起」这一格取 effective_at，拿不到才退回 starts_at。
    double eff = 0;
    long grant = 0; bool has_grant = false;
    std::string unit;
    long remaining = 0; bool has_remaining = false;
};

/// 单位归一，照它自己 quota.js 的 normalize：token/tokens 一支、credit/credits 一支。
std::string zc_unit(const std::string& raw) {
    std::string u;
    for (char ch : raw) u += (char)std::tolower((unsigned char)ch);
    if (u == "token" || u == "tokens") return "tokens";
    if (u == "credit" || u == "credits") return "credits";
    return u;
}

ZcRead parse_zc_line(const std::string& ln) {
    ZcRead z;
    z.ts = parse_bracket_local(ln);
    const std::size_t tag = ln.find("请求完成");
    const std::size_t brace = tag == std::string::npos ? std::string::npos
                                                          : ln.find('{', tag);
    if (brace == std::string::npos) return z;
    Json rec;
    if (!json_parse(ln.c_str() + brace, ln.size() - brace, rec)) return z;
    z.parsed = true;
    const Json* pay = rec.at("payload");
    const Json* data = pay ? pay->at("data") : nullptr;
    const Json* plans = data ? data->at("plans") : nullptr;
    if (!plans || plans->kind != Json::ARR || plans->arr.empty()) return z;
    z.any = true;
    const Json& p = plans->arr.front();
    bool got = false;
    if (const std::string* s = p.str_at("status")) z.status = *s;
    z.starts = (double)num_at(&p, "starts_at", &got);
    if (!got) z.starts = 0;
    z.ends = (double)num_at(&p, "ends_at", &got);
    if (!got) z.ends = 0;
    const Json* ents = p.at("entitlements");
    std::string unit;
    if (ents && ents->kind == Json::ARR) {
        for (const Json& e : ents->arr) {
            const std::string* ut = e.str_at("unit_type");
            const std::string u = zc_unit(ut ? *ut : std::string());
            if (unit.empty()) unit = u;
            if (!unit.empty() && u != unit) continue;    // 不同单位不相加，与它 quota.js 同律
            bool g4 = false;
            const double ea = (double)num_at(&e, "effective_at", &g4);
            if (g4 && ea > 0 && (z.eff == 0 || ea < z.eff)) z.eff = ea;
            bool g2 = false;
            const long v = num_at(&e, "grant_units", &g2);
            if (g2 && v >= 0) { z.grant += v; z.has_grant = true; }
        }
    }
    z.unit = unit;
    const Json* bals = data->at("balances");
    if (bals && bals->kind == Json::ARR) {
        for (const Json& b : bals->arr) {
            const std::string* ut = b.str_at("unit_type");
            if (!unit.empty() && zc_unit(ut ? *ut : std::string()) != unit) continue;
            bool g3 = false;
            const long v = num_at(&b, "remaining_units", &g3);
            if (g3 && v >= 0) { z.remaining += v; z.has_remaining = true; }
        }
    }
    return z;
}

void from_zcode(Claim& c, double now) {
    c.vendor = "zcode";
    c.name = "ZCode";
    const std::string dir = home_dir() + "/.zcode/v2/logs";
    DIR* d = ::opendir(dir.c_str());
    if (!d) { c.source = dir; c.note_key = "cl_note_zc_nodir"; return; }
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d))
        if (e->d_name[0] != '.') names.push_back(e->d_name);
    ::closedir(d);
    if (names.empty()) { c.source = dir; c.note_key = "cl_note_zc_nodir"; return; }
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) { return a > b; });
    // 文件名是 YYYY-MM-DD.log，倒序即新在前。本期常跨零点（10-29 19:03 开的那一期
    // 到 10-30 09:00 才作废），所以最新两份一起扫，且只扫这两份。
    std::vector<ZcRead> reads;
    std::string hit_file;
    for (std::size_t i = 0; i < names.size() && i < 2; i++) {
        const std::string p = dir + "/" + names[i];
        for (const auto& ln : tail_lines(p, "billing/balance 请求完成", 200, 8ull << 20)) {
            ZcRead z = parse_zc_line(ln);
            if (z.parsed) reads.push_back(z);
        }
        if (!reads.empty()) { hit_file = p; break; }
    }
    c.source = hit_file.empty() ? dir + "/" + names.front() : hit_file;
    c.log_mtime = mtime_of(c.source);
    if (reads.empty()) { c.note_key = "cl_note_zc_noline"; return; }

    std::sort(reads.begin(), reads.end(),
              [](const ZcRead& a, const ZcRead& b) { return a.ts > b.ts; });
    c.known = true;
    c.seen_at = reads.front().ts;
    // 「活着的这一期」只有一个判法：最新那条响应里清单非空、状态是 active、且没越过它自己
    // 报的 ends_at。旧读数里的 active 只能回答「上一次有是几点」，不许顶成现状 ——
    // 10-30 09:14 起清单就空了，拿 00:12 那条判「有得领」会把过期说成在售。
    const ZcRead* live = nullptr;
    for (const ZcRead& z : reads) {
        if (!z.any || z.status != "active") continue;
        if (z.ends > 0 && z.ends <= now) continue;
        live = &z;
        break;
    }
    if (!live) {
        for (const ZcRead& z : reads)
            if (z.any && z.status == "active") { c.last_ready_at = z.ts; break; }
    }
    if (live) {
        c.done = true;             // 额度已在账户里并可消费：同一条 balances 给 used/remaining
        // 「本期起」优先用它自己的 effective_at（额度可用时刻），领的动作发生在更早的 starts_at。
        c.appears_at = live->eff > live->starts ? live->eff : live->starts;
        c.expires_at = live->ends;
        if (live->has_grant) {
            c.credit = live->grant;
            c.credit_known = true;
            c.credit_unit = live->unit;
        }
        c.note_key = "cl_note_zc_done";
    } else if (c.last_ready_at > 0) {
        c.note_key = "cl_note_zc_gone";
    } else if (reads.front().any) {
        c.note_key = "cl_note_zc_other";   // 清单非空但状态不是本机见过的 active，不猜
    } else {
        c.note_key = "cl_note_zc_empty";
    }
}

}  // namespace

std::vector<Claim> claims_scan(double now_epoch) {
    std::vector<Claim> out(3);
    from_workbuddy(out[0], now_epoch);
    from_qoder(out[1]);
    from_zcode(out[2], now_epoch);
    return out;
}

std::string claims_dump(const std::vector<Claim>& cs) {
    char buf[512];
    std::string s;
    for (const auto& c : cs) {
        std::snprintf(buf, sizeof buf, "%-10s known=%d ready=%d done=%d opened=%d none_reported=%d",
                      c.vendor.c_str(), (int)c.known, (int)c.ready, (int)c.done, (int)c.opened,
                      (int)c.none_reported);
        s += buf;
        if (c.credit_known) {
            std::snprintf(buf, sizeof buf, " credit=%ld%s", c.credit,
                          c.credit_unit.empty() ? "" : (" " + c.credit_unit).c_str());
            s += buf;
        } else s += " credit=—";
        if (c.streak_known) { std::snprintf(buf, sizeof buf, " streak=%d", c.streak); s += buf; }
        if (c.notif_known) { std::snprintf(buf, sizeof buf, " notif=%d", c.notif); s += buf; }
        s += "\n           源=" + c.source;
        std::snprintf(buf, sizeof buf,
                      "\n           mtime=%.0f seen=%.0f appears=%.0f expires=%.0f",
                      c.log_mtime, c.seen_at, c.appears_at, c.expires_at);
        s += buf;
        s += "\n           note=" + c.note_key;
        if (!c.note_arg_key.empty()) s += " arg=" + c.note_arg_key;
        s += "\n";
    }
    return s;
}

}  // namespace ap
