#include "alert.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
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

std::string alert_key(const std::string& agent_id, const std::string& session_id) {
    return agent_id + "\x1f" + session_id;   // 0x1f：分隔符不可能出现在路径或 uuid 里
}

AlertRound select_alerts(const Snapshot& snap, const std::set<std::string>& ledger,
                         bool dnd, std::set<std::string>* out_ledger) {
    AlertRound r;
    r.dnd = dnd;
    std::set<std::string> next = ledger;
    // dnd 只压我们这一侧的投递，且**不清账**：清了他哪天把通知打开，
    // 几天前那些早已处理完的会话会一起涌出来。
    if (dnd) { if (out_ledger) *out_ledger = next; return r; }

    std::set<std::string> live;
    for (const auto& ag : snap.agents) {
        for (const auto& se : ag.sessions) {
            if (!should_alert(se.state, se.inferred_wait)) continue;
            const std::string k = alert_key(ag.id, se.id);
            live.insert(k);
            r.hits++;
            if (ledger.count(k)) continue;
            r.fresh++;
            r.to_post.push_back({ag.id, ag.name, se.id, se.title, se.state, se.inferred_wait});
        }
    }
    // 已经不再待处理的会话要从账本里摘掉，否则它下次回到该状态不会再响
    // （「等你回复」→ 你回了 → 又问你一次，第二问必须还能吵你）。
    for (auto it = next.begin(); it != next.end(); ) {
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

AlertCfg load_alert_cfg() {
    AlertCfg c;
    std::ifstream f(alert_settings_path(), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (line == "sound=0") c.sound = false;
        else if (line == "dnd=1") c.dnd = true;
    }
    // 环境变量只用于取证：同一份判据要能在不碰他真实设置文件的前提下验出「免打扰确实压住了」。
    if (const char* e = getenv("AGENTPING_DND")) c.dnd = (*e == '1');
    if (const char* e = getenv("AGENTPING_SOUND")) c.sound = (*e != '0');
    return c;
}

bool save_alert_cfg(const AlertCfg& cfg, std::string* err) {
    mkdirs(state_dir());
    const std::string p = alert_settings_path();
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "写不了「" + p + "」"; return false; }
    f << "sound=" << (cfg.sound ? 1 : 0) << "\n" << "dnd=" << (cfg.dnd ? 1 : 0) << "\n";
    f.flush();
    if (!f) { if (err) *err = "写到一半失败（" + p + "）"; return false; }
    return true;
}

}  // namespace ap
