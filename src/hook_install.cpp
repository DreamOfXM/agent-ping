#include "hook_install.h"
#include "jsonl.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ap {
namespace {

std::string home_dir() {
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "/tmp";
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool mkdirs(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    for (std::size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty()) {
                struct stat st;
                if (::stat(cur.c_str(), &st) != 0) {
                    if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
                } else if (!S_ISDIR(st.st_mode)) {
                    return false;
                }
            }
        }
        if (i < path.size()) cur += path[i];
    }
    return true;
}

std::string dirname_of(const std::string& p) {
    std::size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

bool read_file(const std::string& path, std::string& out, bool* exists) {
    *exists = false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    *exists = true;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool write_atomic(const std::string& path, const std::string& body, std::string* err) {
    const std::string tmp = path + ".agentping.tmp";
    mode_t mode = 0644;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) mode = st.st_mode & 07777;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "打不开临时文件 " + tmp; return false; }
        f.write(body.data(), (std::streamsize)body.size());
        if (!f) { if (err) *err = "写临时文件失败 " + tmp; std::remove(tmp.c_str()); return false; }
    }
    ::chmod(tmp.c_str(), mode);
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        if (err) *err = "rename 失败：" + std::string(strerror(errno));
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

std::string stamp_now() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    localtime_r(&t, &tmv);
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

/// 备份原文件；返回备份路径。原文件不存在时返回空（没什么可备的）。
std::string backup_file(const std::string& path, std::string* err) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return "";
    std::string body;
    bool exists = false;
    if (!read_file(path, body, &exists) || !exists) { if (err) *err = "读不到原配置"; return ""; }
    std::string base = path + ".bak-agentping-" + stamp_now();
    std::string cand = base;
    for (int i = 2; i < 100; i++) {
        int fd = ::open(cand.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            std::size_t off = 0;
            while (off < body.size()) {
                ssize_t n = ::write(fd, body.data() + off, body.size() - off);
                if (n <= 0) { ::close(fd); if (err) *err = "备份写入失败"; return ""; }
                off += (std::size_t)n;
            }
            ::close(fd);
            return cand;
        }
        if (errno != EEXIST) { if (err) *err = "备份创建失败：" + std::string(strerror(errno)); return ""; }
        cand = base + "-" + std::to_string(i);
    }
    if (err) *err = "备份文件名用尽";
    return "";
}

// ── JSON 序列化：只服务「读原来那份、改两处、写回」，所以保留键序与全部未知字段 ──
void dump_string(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    out += b;
                } else {
                    out += (char)c;
                }
        }
    }
    out += '"';
}

void dump_number(double v, std::string& out) {
    char b[40];
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 9e15) {
        snprintf(b, sizeof b, "%lld", (long long)v);
        out += b;
        return;
    }
    // 最短且能读回同一个 double 的写法：先试 15 位，读回不等再加。
    for (int prec = 15; prec <= 17; prec++) {
        snprintf(b, sizeof b, "%.*g", prec, v);
        if (strtod(b, nullptr) == v) break;
    }
    out += b;
}

void dump_json(const Json& v, std::string& out, int indent) {
    const std::string pad(indent + 2, ' ');
    const std::string close(indent, ' ');
    switch (v.kind) {
        case Json::NUL: out += "null"; break;
        case Json::BOOL: out += v.boolean ? "true" : "false"; break;
        case Json::NUM: dump_number(v.num, out); break;
        case Json::STR: dump_string(v.str, out); break;
        case Json::ARR:
            if (v.arr.empty()) { out += "[]"; break; }
            out += "[\n";
            for (std::size_t i = 0; i < v.arr.size(); i++) {
                out += pad;
                dump_json(v.arr[i], out, indent + 2);
                if (i + 1 < v.arr.size()) out += ',';
                out += '\n';
            }
            out += close;
            out += ']';
            break;
        case Json::OBJ:
            if (v.obj.empty()) { out += "{}"; break; }
            out += "{\n";
            for (std::size_t i = 0; i < v.obj.size(); i++) {
                out += pad;
                dump_string(v.obj[i].first, out);
                out += ": ";
                dump_json(v.obj[i].second, out, indent + 2);
                if (i + 1 < v.obj.size()) out += ',';
                out += '\n';
            }
            out += close;
            out += '}';
            break;
    }
}

// ── 配置里那棵 hooks 树 ────────────────────────────────────────────────────
bool is_agentping_command(const std::string& cmd) {
    return cmd.find("agent-ping-hook.sh") != std::string::npos ||
           cmd.find("/AgentPing/hook.sh") != std::string::npos ||
           cmd.find("/agentping/hook.sh") != std::string::npos;
}

/// 命令串里第一个 word 就是脚本路径（我们写的是 '<path>' <agent>，旧版也是这个形状）。
std::string command_script(const std::string& cmd) {
    std::size_t i = 0;
    while (i < cmd.size() && (cmd[i] == ' ' || cmd[i] == '\t')) i++;
    if (i >= cmd.size()) return "";
    if (cmd[i] == '\'') {
        std::size_t e = cmd.find('\'', i + 1);
        return e == std::string::npos ? "" : cmd.substr(i + 1, e - i - 1);
    }
    if (cmd[i] == '"') {
        std::size_t e = cmd.find('"', i + 1);
        return e == std::string::npos ? "" : cmd.substr(i + 1, e - i - 1);
    }
    std::size_t e = cmd.find_first_of(" \t", i);
    return cmd.substr(i, e == std::string::npos ? std::string::npos : e - i);
}

/// 逐条走 hooks.<事件>[].hooks[].command。
template <typename Fn>
void each_command(Json& cfg, Fn fn) {
    Json* hooks = nullptr;
    for (auto& kv : cfg.obj) if (kv.first == "hooks") hooks = &kv.second;
    if (!hooks || hooks->kind != Json::OBJ) return;
    for (auto& ev : hooks->obj) {
        if (ev.second.kind != Json::ARR) continue;
        for (auto& ent : ev.second.arr) {
            if (ent.kind != Json::OBJ) continue;
            std::string matcher;
            for (auto& kv : ent.obj) if (kv.first == "matcher" && kv.second.kind == Json::STR) matcher = kv.second.str;
            for (auto& kv : ent.obj) {
                if (kv.first != "hooks" || kv.second.kind != Json::ARR) continue;
                for (auto& h : kv.second.arr) {
                    if (h.kind != Json::OBJ) continue;
                    for (auto& hkv : h.obj) {
                        if (hkv.first == "command" && hkv.second.kind == Json::STR)
                            fn(ev.first, matcher, hkv.second.str);
                    }
                }
            }
        }
    }
}

struct AgentCfg { const char* id; const char* name; const char* config_rel; bool writable; };

const AgentCfg kAgents[] = {
    {"claude", "Claude Code", ".claude/settings.json", true},
    {"qoder", "Qoder CLI", ".qoder/settings.json", true},
    {"codex", "Codex", ".codex/config.toml", false},
    {"zcode", "ZCode", "", false},
    {"workbuddy", "WorkBuddy", "", false},
    {"opencode", "OpenCode", "", false},
};

const AgentCfg* find_agent(const std::string& id) {
    for (const auto& a : kAgents) if (id == a.id) return &a;
    return nullptr;
}

std::string agent_config(const AgentCfg& a) {
    return a.config_rel[0] ? join(home_dir(), a.config_rel) : std::string();
}

bool load_config(const std::string& path, Json& cfg, bool* exists, std::string* err) {
    // 先 stat：文件不存在是「还没配过」，要当空对象继续写；存在但读不出来才是错误。
    // 不分开的后果实测过——没装过 Qoder 的机器上，接线屏永远报「读不到配置」，
    // 而那颗「写入」按钮本该能把配置建出来。
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) {
            *exists = false;
            cfg = Json(); cfg.kind = Json::OBJ;
            return true;
        }
        if (err) *err = "读不到 " + path + "（" + strerror(errno) + "）";
        return false;
    }
    std::string body;
    if (!read_file(path, body, exists)) { if (err) *err = "读不到 " + path; return false; }
    if (!*exists) { cfg = Json(); cfg.kind = Json::OBJ; return true; }
    if (body.find_first_not_of(" \t\r\n") == std::string::npos) { cfg = Json(); cfg.kind = Json::OBJ; return true; }
    if (!json_parse(body.c_str(), body.size(), cfg)) {
        if (err) *err = "配置不是合法 JSON，未改动";
        return false;
    }
    if (cfg.kind != Json::OBJ) { if (err) *err = "配置顶层不是对象，未改动"; return false; }
    return true;
}

Json* member(Json& obj, const char* key) {
    for (auto& kv : obj.obj) if (kv.first == key) return &kv.second;
    return nullptr;
}

Json* ensure_object(Json& obj, const char* key, std::string* err) {
    Json* v = member(obj, key);
    if (!v) {
        obj.obj.push_back({key, Json()});
        v = &obj.obj.back().second;
        v->kind = Json::OBJ;
        return v;
    }
    if (v->kind != Json::OBJ) { if (err) *err = std::string(key) + " 不是对象，未改动"; return nullptr; }
    return v;
}

Json* ensure_array(Json& obj, const char* key, std::string* err) {
    Json* v = member(obj, key);
    if (!v) {
        obj.obj.push_back({key, Json()});
        v = &obj.obj.back().second;
        v->kind = Json::ARR;
        return v;
    }
    if (v->kind != Json::ARR) { if (err) *err = std::string(key) + " 不是数组，未改动"; return nullptr; }
    return v;
}

Json& matcher_entry(Json& notification, const std::string& matcher, std::string* err) {
    for (auto& ent : notification.arr) {
        if (ent.kind != Json::OBJ) continue;
        for (auto& kv : ent.obj)
            if (kv.first == "matcher" && kv.second.kind == Json::STR && kv.second.str == matcher) return ent;
    }
    Json ent;
    ent.kind = Json::OBJ;
    ent.obj.push_back({"matcher", Json()});
    ent.obj.back().second.kind = Json::STR;
    ent.obj.back().second.str = matcher;
    ent.obj.push_back({"hooks", Json()});
    ent.obj.back().second.kind = Json::ARR;
    notification.arr.push_back(std::move(ent));
    (void)err;
    return notification.arr.back();
}

void strip_ours(Json& entry) {
    Json* hs = member(entry, "hooks");
    if (!hs || hs->kind != Json::ARR) return;
    hs->arr.erase(std::remove_if(hs->arr.begin(), hs->arr.end(), [](const Json& h) {
        const Json* c = nullptr;
        for (const auto& kv : h.obj) if (kv.first == "command") c = &kv.second;
        return c && c->kind == Json::STR && is_agentping_command(c->str);
    }), hs->arr.end());
}

void add_ours(Json& entry, const std::string& script, const std::string& agent) {
    Json* hs = member(entry, "hooks");
    if (!hs || hs->kind != Json::ARR) {
        entry.obj.push_back({"hooks", Json()});
        hs = &entry.obj.back().second;
        hs->kind = Json::ARR;
    }
    Json h;
    h.kind = Json::OBJ;
    h.obj.push_back({"type", Json()});
    h.obj.back().second.kind = Json::STR;
    h.obj.back().second.str = "command";
    h.obj.push_back({"command", Json()});
    h.obj.back().second.kind = Json::STR;
    h.obj.back().second.str = "'" + script + "' " + agent;
    hs->arr.push_back(std::move(h));
}

}  // namespace

std::string hook_script_path() {
#ifdef __APPLE__
    return join(home_dir(), "Library/Application Support/AgentPing/hook.sh");
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    const std::string root = (xdg && *xdg) ? std::string(xdg) : join(home_dir(), ".local/share");
    return join(root, "agentping/hook.sh");
#endif
}

std::string hook_script_body() {
    return
        "#!/bin/sh\n"
        "# Agent Ping hook writer (v1).\n"
        "# Copies the CLI's notification JSON into Agent Ping's inbox. Always exits 0:\n"
        "# the CLI treats a hook's exit code as a decision, and we never make one.\n"
        "AGENT=\"${1:-claude}\"\n"
        "D=\"${AGENTPING_HOOK_DIR:-}\"\n"
        "if [ -z \"$D\" ]; then\n"
        "  if [ -n \"${XDG_CONFIG_HOME:-}\" ]; then D=\"$XDG_CONFIG_HOME/AgentPing/hooks\"\n"
        "  elif [ \"$(uname -s)\" = \"Darwin\" ]; then D=\"$HOME/Library/Application Support/AgentPing/hooks\"\n"
        "  else D=\"${XDG_CONFIG_HOME:-$HOME/.config}/AgentPing/hooks\"\n"
        "  fi\n"
        "fi\n"
        "mkdir -p \"$D\" 2>/dev/null || exit 0\n"
        "T=\"$D/.$AGENT-$$.tmp\"\n"
        "cat > \"$T\" 2>/dev/null || { rm -f \"$T\"; exit 0; }\n"
        "[ -s \"$T\" ] || { rm -f \"$T\"; exit 0; }\n"
        "mv \"$T\" \"$D/$AGENT-$(date +%s)-$$.json\" 2>/dev/null\n"
        "exit 0\n";
}

bool write_hook_script(std::string* err) {
    const std::string path = hook_script_path();
    if (!mkdirs(dirname_of(path))) { if (err) *err = "建不了目录 " + dirname_of(path); return false; }
    if (!write_atomic(path, hook_script_body(), err)) return false;
    if (::chmod(path.c_str(), 0755) != 0) { if (err) *err = "chmod 失败"; return false; }
    return true;
}

std::vector<HookAgent> probe_hooks() {
    std::vector<HookAgent> out;
    const std::string ours = hook_script_path();
    for (const auto& a : kAgents) {
        HookAgent h;
        h.id = a.id;
        h.name = a.name;
        h.config = agent_config(a);
        h.supported = a.writable;
        if (!a.config_rel[0]) {
            h.note = "no hook channel";
            out.push_back(std::move(h));
            continue;
        }
        if (a.writable) {
            Json cfg;
            bool exists = false;
            std::string err;
            if (!load_config(h.config, cfg, &exists, &err)) {
                h.note = err;
                out.push_back(std::move(h));
                continue;
            }
            bool any_missing = false;
            each_command(cfg, [&](const std::string&, const std::string&, const std::string& cmd) {
                if (!is_agentping_command(cmd)) return;
                const std::string p = command_script(cmd);
                // 「指向我们这条路径」本身不算装好：脚本文件被删/搬走之后配置还留着那行，
                // 光比字符串就会把坏接线报成已接线。两条路都要过 stat。
                struct stat st;
                const bool there = !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
                if (p == ours) {
                    h.script = p;
                    if (there) h.installed = true;
                    else any_missing = true;
                    return;
                }
                if (there) {
                    h.installed = true;
                    h.script = p;
                } else {
                    any_missing = true;
                    if (h.script.empty()) h.script = p;
                }
            });
            h.stale = any_missing && !h.installed;
        } else {
            // codex：唯一的 notify 位。读那一行原文，别去猜 TOML 结构。
            std::string body;
            bool exists = false;
            if (read_file(h.config, body, &exists) && exists) {
                std::size_t i = 0;
                while (i < body.size()) {
                    std::size_t e = body.find('\n', i);
                    if (e == std::string::npos) e = body.size();
                    std::string line = body.substr(i, e - i);
                    std::size_t k = line.find_first_not_of(" \t");
                    if (k != std::string::npos && line.compare(k, 6, "notify") == 0) {
                        std::size_t eq = line.find('=', k);
                        if (eq != std::string::npos) {
                            const std::string val = line.substr(eq + 1);
                            if (val.find("agentping") != std::string::npos ||
                                val.find("AgentPing/hook.sh") != std::string::npos) {
                                h.installed = true;
                            } else {
                                h.occupied = true;
                                h.note = "notify is used by another tool";
                            }
                            break;
                        }
                    }
                    i = e + 1;
                }
            }
        }
        out.push_back(std::move(h));
    }
    return out;
}

bool install_hook(const std::string& agent_id, std::string* backup, std::string* err) {
    if (backup) backup->clear();
    const AgentCfg* a = find_agent(agent_id);
    if (!a || !a->writable) { if (err) *err = "这家 CLI 没有可写的 hook 位"; return false; }
    std::string werr;
    if (!write_hook_script(&werr)) { if (err) *err = werr; return false; }
    const std::string path = agent_config(*a);
    Json cfg;
    bool exists = false;
    std::string lerr;
    if (!load_config(path, cfg, &exists, &lerr)) { if (err) *err = lerr; return false; }

    Json* hooks = ensure_object(cfg, "hooks", err);
    if (!hooks) return false;
    Json* notif = ensure_array(*hooks, "Notification", err);
    if (!notif) return false;

    const char* matchers[2] = {"permission_prompt", "idle_prompt"};
    for (const char* m : matchers) {
        Json& ent = matcher_entry(*notif, m, err);
        strip_ours(ent);
        add_ours(ent, hook_script_path(), a->id);
    }

    std::string out;
    dump_json(cfg, out, 0);
    out += '\n';
    if (!mkdirs(dirname_of(path))) { if (err) *err = "建不了配置目录"; return false; }
    std::string berr;
    const std::string bak = backup_file(path, &berr);
    if (!berr.empty()) { if (err) *err = berr; return false; }
    if (!write_atomic(path, out, err)) return false;
    if (backup) *backup = bak;
    return true;
}

bool uninstall_hook(const std::string& agent_id, std::string* backup, std::string* err) {
    if (backup) backup->clear();
    const AgentCfg* a = find_agent(agent_id);
    if (!a || !a->writable) { if (err) *err = "这家 CLI 没有可写的 hook 位"; return false; }
    const std::string path = agent_config(*a);
    Json cfg;
    bool exists = false;
    std::string lerr;
    if (!load_config(path, cfg, &exists, &lerr)) { if (err) *err = lerr; return false; }
    if (!exists) return true;
    Json* hooks = member(cfg, "hooks");
    if (!hooks || hooks->kind != Json::OBJ) return true;
    Json* notif = member(*hooks, "Notification");
    if (!notif || notif->kind != Json::ARR) return true;
    for (auto& ent : notif->arr) strip_ours(ent);
    // 摘掉命令之后可能只剩 {"matcher":…,"hooks":[]}：留着它是撤销不干净的样子。
    // 空数组在 CLI 里没有任何行为，所以删掉不会改变谁的决定。
    notif->arr.erase(std::remove_if(notif->arr.begin(), notif->arr.end(), [](const Json& ent) {
        const Json* hs = nullptr;
        for (const auto& kv : ent.obj) if (kv.first == "hooks") hs = &kv.second;
        return ent.kind == Json::OBJ && hs && hs->kind == Json::ARR && hs->arr.empty();
    }), notif->arr.end());
    std::string out;
    dump_json(cfg, out, 0);
    out += '\n';
    std::string berr;
    const std::string bak = backup_file(path, &berr);
    if (!berr.empty()) { if (err) *err = berr; return false; }
    if (!write_atomic(path, out, err)) return false;
    if (backup) *backup = bak;
    return true;
}

}  // namespace ap
