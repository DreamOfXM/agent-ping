#pragma once
#include <set>
#include <string>
#include <vector>

#include "scan.h"

// 提醒层：把「有一条会话在等你」这件事变成一次打断。
// 判据逐条对齐 Swift 侧 Sources/AgentPing/Engine.swift:277-317（shouldAlert / notify），
// 那一版是这台机器上唯一实测跑通过「响过就不重复吵人」的实现。
// 本文件不 include 任何平台头：发横幅与出声走 src/platform/notify.h 的 C 接口。
namespace ap {

/// 该不该打断你。「疑似等授权」（inferred）不算：那条判据和「正在跑一条长命令」
/// 长得一模一样（都是 tool_use 没回），拿它响铃就是拿误报吵人，只在面板上标。
/// 排队中也不算：那是 CLI 自己把消息排进队列，连发几条指令时响铃会连着吵人。
bool should_alert(State st, bool inferred);

/// 一条待提醒的会话。只给身份和状态 —— 六种语言的文案由界面层现做，
/// 这里写死一种就没有「横幅和面板说同一件事」了。
struct Pending {
    std::string agent_id, agent_name, session_id, title;
    State state = ST_RESTING;
    bool inferred = false;
};

/// 一轮的账：命中几条、其中几条已提醒过、真发出几条、被上限压掉几条、从账本摘掉几条。
/// 这五个数必须能打印出来 —— 「没响」有四种成因，只报「已通知」等于什么都没验。
struct AlertRound {
    int hits = 0;      // should_alert 命中（含已提醒过的）
    int fresh = 0;     // 其中不在账本里的
    int posted = 0;    // 真交给平台发出去的（≤ kMaxPerRound）
    int capped = 0;    // 超出上限、本轮没发的（下一轮还会是 fresh，不会被忘掉）
    int retired = 0;   // 已不再待处理，从账本摘掉（下次回到该状态还得再响）
    bool dnd = false;
    std::vector<Pending> to_post;
};

/// 一轮通知的判据部分（不碰平台）：账本进、账本出。
/// dnd 为真时整轮跳过且**不清账**：清了的话他哪天把通知打开，几天前那些早已处理完的会话会一起涌出来。
/// 每轮最多发 kMaxPerRound 条（同 Swift 的 `pending.prefix(4)`），一次吵人四下已经够多了。
constexpr int kMaxPerRound = 4;
AlertRound select_alerts(const Snapshot& snap, const std::set<std::string>& ledger,
                         bool dnd, std::set<std::string>* out_ledger);

/// 一行通知的身份键：agent id + 会话 id。单独用会话 id 会跨采集器撞名（两家都用 uuid）。
std::string alert_key(const std::string& agent_id, const std::string& session_id);

// ── 落盘位置 ────────────────────────────────────────────────────────────────
// Apple: ~/Library/Application Support/AgentPing/    其它: $XDG_CONFIG_HOME|~/.config/AgentPing/
// AGENTPING_STATE_DIR 整体覆盖（取证专用：跑两次证明去重时不许往他正在用的目录里写测试身份）。
std::string state_dir();
std::string alert_ledger_path();     // 去重账本：每行一个身份键
std::string alert_settings_path();   // sound= / dnd= 两行，跨重启留住

std::set<std::string> load_alerted();
/// 写账本。返回 false 且 err 非空时调用方必须把 err 打出来：账本写不进去＝去重失效，
/// 每轮重扫都会重发同一批横幅，那是比不响更糟的失效方式。
bool save_alerted(const std::set<std::string>& ids, std::string* err);

struct AlertCfg { bool sound = true; bool dnd = false; };
/// 读设置；AGENTPING_SOUND / AGENTPING_DND 覆盖（0/1），让「免打扰确实压住了」可机器验证。
AlertCfg load_alert_cfg();
bool save_alert_cfg(const AlertCfg& cfg, std::string* err);

}  // namespace ap
