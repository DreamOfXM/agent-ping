#include "alert.h"

#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ap {

bool should_alert(State st, bool inferred) {
    switch (st) {
    case ST_FAILED:
    case ST_NEEDS_USER:   return true;
    case ST_AWAIT:        return !inferred;
    case ST_QUEUED:
    case ST_WORKING:
    case ST_RESTING:      return false;
    }
    return false;
}

bool alertable(const AlertCfg& cfg, State st, bool inferred, bool just_finished) {
    if (st == ST_RESTING) return cfg.done && just_finished;
    if (!should_alert(st, inferred)) return false;
    switch (st) {
    case ST_NEEDS_USER:   return cfg.needs_user;
    case ST_AWAIT:        return cfg.await_perm;
    case ST_FAILED:       return cfg.failed;
    default:              return false;   // 排队中/运行中：should_alert 就不给，开关也不许开这一档
    }
}

std::string alert_key(const std::string& agent_id, const std::string& session_id) {
    return agent_id + "\x1f" + session_id;   // 0x1f：分隔符不可能出现在路径或 uuid 里
}

AlertRound select_alerts(const Snapshot& snap, const std::set<std::string>& ledger,
                         const AlertCfg& cfg, std::set<std::string>* out_ledger,
                         const std::set<std::string>* just_finished) {
    AlertRound r;
    r.dnd = cfg.dnd;
    std::set<std::string> next = ledger;
    // dnd 只压我们这一侧的投递，且**不清账**：清了他哪天把通知打开，
    // 几天前那些早已处理完的会话会一起涌出来。
    if (cfg.dnd) { if (out_ledger) *out_ledger = next; return r; }

    std::set<std::string> live;
    for (const auto& ag : snap.agents) {
        for (const auto& se : ag.sessions) {
            const std::string k = alert_key(ag.id, se.id);
            const bool jumped = just_finished && just_finished->count(k) > 0;
            if (!alertable(cfg, se.state, se.inferred_wait, jumped)) continue;
            live.insert(k);
            r.hits++;
            if (ledger.count(k)) continue;
            r.fresh++;
            r.to_post.push_back({ag.id, ag.name, se.id, se.title, se.state, se.inferred_wait});
        }
    }
    // 已经不再待处理的会话要从账本里摘掉，否则它下次回到该状态不会再响
    // （「等你回复」→ 你回了 → 又问你一次，第二问必须还能吵你）。
    // 券那类键不归这一轮扫：它们的「不再待处理」由 select_claim_alerts 自己摘，
    // 在这里一起清掉会让本期那条券在下一轮重扫时又变成「首次」。
    for (auto it = next.begin(); it != next.end(); ) {
        if (is_claim_key(*it)) { ++it; continue; }
        if (!live.count(*it)) { it = next.erase(it); r.retired++; } else ++it;
    }
    if ((int)r.to_post.size() > kMaxPerRound) {
        r.capped = (int)r.to_post.size() - kMaxPerRound;
        r.to_post.resize(kMaxPerRound);
    }
    for (const auto& p : r.to_post) next.insert(alert_key(p.agent_id, p.session_id));
    r.posted = (int)r.to_post.size();
    if (out_ledger) *out_ledger = next;
    return r;
}

std::string claim_alert_key(const std::string& vendor, double appears_at) {
    char day[16] = {0};
    if (appears_at > 0) {
        const time_t tt = (time_t)appears_at;
        struct tm lt{};
        ::localtime_r(&tt, &lt);
        ::strftime(day, sizeof day, "%Y-%m-%d", &lt);
    }
    // 取不到期次就不带日期：宁可这一家整条只响一次，也不要每 3 秒的重扫各吵一遍。
    return std::string(kClaimKeyPrefix) + vendor + (day[0] ? ":" : "") + day;
}

AlertRound select_claim_alerts(const std::vector<Claim>& claims,
                               const std::set<std::string>& ledger, const AlertCfg& cfg,
                               std::set<std::string>* out_ledger) {
    AlertRound r;
    r.dnd = cfg.dnd;
    std::set<std::string> next = ledger;
    // 免打扰与「权益到账」那颗开关都只压投递、不改账本：关掉期间攒下的期次，
    // 不许在他重新打开的那一刻一起涌出来（会话那轮同一口径）。
    if (cfg.dnd || !cfg.claims) { if (out_ledger) *out_ledger = next; return r; }

    std::set<std::string> live;
    for (const auto& c : claims) {
        if (!c.ready) continue;              // 只有它亲口说「现在有得领」才够格
        const std::string k = claim_alert_key(c.vendor, c.appears_at);
        live.insert(k);
        r.hits++;
        if (ledger.count(k)) continue;
        r.fresh++;
        Pending p;
        p.agent_id = c.vendor;
        p.agent_name = c.name;
        p.session_id = k;                    // 整条券键：横幅点回来靠前缀认身份
        r.to_post.push_back(p);
    }
    // 这一期不再可领（领掉了／换期了）就把键摘掉：不摘，账本会天天涨，
    // 而且明年同一天若真又是这一期，它会顶着「已提醒过」永远不再响。
    for (auto it = next.begin(); it != next.end(); ) {
        if (!is_claim_key(*it)) { ++it; continue; }
        if (!live.count(*it)) { it = next.erase(it); r.retired++; } else ++it;
    }
    if ((int)r.to_post.size() > kMaxPerRound) {
        r.capped = (int)r.to_post.size() - kMaxPerRound;
        r.to_post.resize(kMaxPerRound);
    }
    for (const auto& p : r.to_post) next.insert(p.session_id);
    r.posted = (int)r.to_post.size();
    if (out_ledger) *out_ledger = next;
    return r;
}

// ── 落盘 ────────────────────────────────────────────────────────────────────

namespace {
std::string home_() {
    const char* h = getenv("HOME");
    return (h && *h) ? std::string(h) : std::string("/");
}
std::string join_(const std::string& a, const std::string& b) { return a + "/" + b; }
void mkdirs(const std::string& p) {
    // 一层层建：状态目录是「AgentPing」这一级，父目录（Library/Application Support、
    // ~/.config）在别的机器上不一定存在，mkdir 失败时下面自然报得出来。
    std::string acc;
    for (size_t i = 0; i < p.size(); i++) {
        acc += p[i];
        if (p[i] != '/' && i + 1 != p.size()) continue;
        std::string d = acc;
        if (!d.empty() && d.back() == '/') d.pop_back();
        if (d.empty()) continue;
        mkdir(d.c_str(), 0755);   // 已存在返回 EEXIST，这里不需要区分
    }
}
}  // namespace

std::string state_dir() {
    const char* over = getenv("AGENTPING_STATE_DIR");
    if (over && *over) return over;
#ifdef __APPLE__
    return home_() + "/Library/Application Support/AgentPing";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    std::string base = (xdg && *xdg) ? std::string(xdg) : join_(home_(), ".config");
    return join_(base, "AgentPing");
#endif
}

std::string alert_ledger_path() { return join_(state_dir(), "alerted.txt"); }
std::string alert_settings_path() { return join_(state_dir(), "settings.txt"); }

std::set<std::string> load_alerted() {
    std::set<std::string> out;
    std::ifstream f(alert_ledger_path(), std::ios::binary);
    if (!f) return out;    // 文件不存在＝本机还没提醒过任何一条，不是「全部已提醒」
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // 身份键里带 0x1f，整行就是键；行尾 \r（有人在 Windows 侧手改过这文件时）去掉。
        if (line.back() == '\r') line.pop_back();
        if (!line.empty()) out.insert(line);
    }
    return out;
}

bool save_alerted(const std::set<std::string>& ids, std::string* err) {
    mkdirs(state_dir());
    const std::string p = alert_ledger_path();
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "写不了「" + p + "」"; return false; }
    for (const auto& k : ids) f << k << "\n";
    f.flush();
    if (!f) { if (err) *err = "写到一半失败（" + p + "）"; return false; }
    return true;
}

std::string seen_path() { return join_(state_dir(), "seen.txt"); }

std::vector<std::string> load_seen() {
    std::vector<std::string> out;
    std::ifstream f(seen_path(), std::ios::binary);
    if (!f) return out;    // 没有这文件＝升级后还没点过任何一条，不是「全都看过」
    std::string line;
    while (std::getline(f, line)) {
        if (line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

bool append_seen(const std::string& key, std::string* err) {
    mkdirs(state_dir());
    const std::string p = seen_path();
    std::ofstream f(p, std::ios::binary | std::ios::app);
    if (!f) { if (err) *err = "写不了「" + p + "」"; return false; }
    f << key << "\n";
    f.flush();
    if (!f) { if (err) *err = "写到一半失败（" + p + "）"; return false; }
    return true;
}

bool save_seen(const std::vector<std::string>& keys, std::string* err) {
    mkdirs(state_dir());
    const std::string p = seen_path();
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "写不了「" + p + "」"; return false; }
    for (const auto& k : keys) f << k << "\n";
    f.flush();
    if (!f) { if (err) *err = "写到一半失败（" + p + "）"; return false; }
    return true;
}

AlertCfg load_alert_cfg() {
    AlertCfg c;
    std::ifstream f(alert_settings_path(), std::ios::binary);
    std::string line;
    // 一行一个 key=值，读不认识的键直接跳过：设置文件是用户会用手改的东西，
    // 一个错键不许把其它七档一起吞掉。
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int b = (v == "1") ? 1 : (v == "0") ? 0 : -1;
        if (k == "sound") { if (b >= 0) c.sound = b; }
        else if (k == "dnd") { if (b >= 0) c.dnd = b; }
        else if (k == "alert_needs") { if (b >= 0) c.needs_user = b; }
        else if (k == "alert_await") { if (b >= 0) c.await_perm = b; }
        else if (k == "alert_failed") { if (b >= 0) c.failed = b; }
        else if (k == "alert_done") { if (b >= 0) c.done = b; }
        else if (k == "alert_claims") { if (b >= 0) c.claims = b; }
        else if (k == "on_top") { if (b >= 0) c.on_top = b; }
        else if (k == "max_rows") {
            int n = atoi(v.c_str());
            if (n >= 1 && n <= 40) c.max_rows = n;      // 0 或负数＝整组摆不出来，那是失效不是设置
        }
        else if (k == "lang") {
            int n = atoi(v.c_str());
            if (n >= 0 && n <= 5) c.lang = n;           // -1（没写过）留在探测那条路上
        }
        else if (k == "seen_until") { c.seen_until = atoll(v.c_str()); }
        else if (k == "last_claim_at") { c.last_claim_at = atoll(v.c_str()); }
        else if (k == "win_x") { c.win_x = atoi(v.c_str()); }
        else if (k == "win_y") { c.win_y = atoi(v.c_str()); }
    }
    // 没写过水位线（0＝1970，真实值不可能是它）＝第一次用：存量完成记录不倒灌成未读，
    // 只给今后新完成的起点。以后每次看过一条会话，水位线推进并落盘。
    if (c.seen_until == 0) c.seen_until = (long long)std::time(nullptr);
    // 环境变量只用于取证：同一份判据要能在不碰他真实设置文件的前提下验出「免打扰确实压住了」。
    if (const char* e = getenv("AGENTPING_DND")) c.dnd = (*e == '1');
    if (const char* e = getenv("AGENTPING_SOUND")) c.sound = (*e != '0');
    if (const char* e = getenv("AGENTPING_CLAIMS")) c.claims = (*e == '1');
    return c;
}

bool save_alert_cfg(const AlertCfg& cfg, std::string* err) {
    mkdirs(state_dir());
    const std::string p = alert_settings_path();
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "写不了「" + p + "」"; return false; }
    f << "sound=" << (cfg.sound ? 1 : 0) << "\n"
      << "dnd=" << (cfg.dnd ? 1 : 0) << "\n"
      << "alert_needs=" << (cfg.needs_user ? 1 : 0) << "\n"
      << "alert_await=" << (cfg.await_perm ? 1 : 0) << "\n"
      << "alert_failed=" << (cfg.failed ? 1 : 0) << "\n"
      << "alert_done=" << (cfg.done ? 1 : 0) << "\n"
      << "alert_claims=" << (cfg.claims ? 1 : 0) << "\n"
      << "on_top=" << (cfg.on_top ? 1 : 0) << "\n"
      << "max_rows=" << cfg.max_rows << "\n"
      << "lang=" << cfg.lang << "\n"
      << "seen_until=" << cfg.seen_until << "\n"
      << "last_claim_at=" << cfg.last_claim_at << "\n"
      << "win_x=" << cfg.win_x << "\n"
      << "win_y=" << cfg.win_y << "\n";
    f.flush();
    if (!f) { if (err) *err = "写到一半失败（" + p + "）"; return false; }
    return true;
}

}  // namespace ap
