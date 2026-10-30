#pragma once
#include <set>
#include <string>
#include <vector>

#include "claims.h"
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

struct AlertCfg {
    bool sound = true;
    bool dnd = false;
    // 下面四条是「需要提醒的状态」的四档，与设置屏上那排胶囊一一对应。
    // 前三条只做「允许关」——要关的档位 should_alert 本来就给；
    // done 是唯一能往外扩的一档，代价见 alertable()。
    bool needs_user = true;   // 等你回复（ST_NEEDS_USER：一轮无错收尾，球回到你这边）
    bool await_perm = true;   // 等待确认（ST_AWAIT 且非推断出来的那个）
    bool failed = true;       // 失败中断
    bool done = false;        // 已完成（ST_RESTING）
    /// 有券可领时弹一条。与会话那四档分开放：会话那排是「哪种状态允许吵」，
    /// 这一条是「另一类事」——它不看会话，只看各家自己写在磁盘上的领取读数。
    bool claims = true;
    /// 上一次弹「有券可领」横幅的时刻（0＝从来没有过）。权益屏要把这句话摆在明面上：
    /// 用户想知道的是「它到底有没有吵过我、什么时候」，而账本里那些 claim: 键只有日期、
    /// 且会随券过期被摘掉，回答不了这个问题。
    long long last_claim_at = 0;
    /// 「已读水位线」：完成时刻 ≤ 它的会话都算看过（小点点不亮）。冷启动没有这个键时
    /// 取当前时刻——存量的完成记录不倒灌成未读，只给今后新完成的起点。
    long long seen_until = 0;
    /// 面板窗口位置：拖动过就记住（-1＝没拖过，启动贴菜单栏右下）。
    int win_x = -1, win_y = -1;
    int max_rows = 8;         // 每组最多摆几条，多出来的收进「还有 N 条」
    bool on_top = true;       // 常驻置顶
    /// 界面语言（0..5，见 pin_imgui_shell.cpp 的 kLocTag）。这是界面偏好，不是提醒判据，
    /// 只是与上面几档共用同一个 settings.txt——面板的设置就这一个文件，不再多开一个。
    /// -1＝没写过，由平台层探测 / AGENTPING_LANG 决定，与加这一项之前的行为一致。
    int lang = -1;
};

/// 设置生效后的判据：只许在 should_alert 已成立的前提上按档位关，不许凭空多响——
/// 唯一的例外是 done，它开的是 should_alert 永远不给的 ST_RESTING，而且必须带
/// just_finished 的证据（本进程先前看见过这条会话在动）。不带这条门，第一次打开
/// 「已完成」就会把窗口里几十条几天前的静置会话当成新跳变，连吵十几轮。
/// 默认四档（needs_user/await_perm/failed 开、done 关）必须与 should_alert 逐条等价，
/// alert_probe --selftest 第⑨组就是验这件事。
bool alertable(const AlertCfg& cfg, State st, bool inferred, bool just_finished);

/// 一条待提醒的会话。只给身份和状态 —— 六种语言的文案由界面层现做，
/// 这里写死一种就没有「横幅和面板说同一件事」了。
struct Pending {
    std::string agent_id, agent_name, session_id, title;
    State state = ST_RESTING;
    bool inferred = false;
};

/// 券到账用的身份键前缀。会话键是「agent/会话 id」，券键是「claim:厂商:期次日」——
/// 两边共用同一本账本，靠这个前缀各扫各的：会话那轮不许把券键当「不再待处理」摘掉，
/// 券这轮也不许动会话键。少了这道分界，任何一轮重扫都会清掉对家的去重记录，
/// 于是每天 00:00 那条券会被同一期反复吵。
constexpr const char* kClaimKeyPrefix = "claim:";
inline bool is_claim_key(const std::string& k) { return k.rfind(kClaimKeyPrefix, 0) == 0; }

/// 一条券的到账键：厂商 + 那一期的本地日期（appears_at 落在哪一天）。
/// 用「期次」而不是「本轮 ready 的跳变」做键：WorkBuddy 每天 00:00 翻回未领，
/// 换一天就是一个新键（该再响）；同一期内重扫几十轮只响一次。
std::string claim_alert_key(const std::string& vendor, double appears_at);

/// 一轮的账：命中几条、其中几条已提醒过、真发出几条、被上限压掉几条、从账本摘掉几条。
/// 这五个数必须能打印出来 —— 「没响」有四种成因，只报「已通知」等于什么都没验。
struct AlertRound {
    int hits = 0;      // 设置生效后命中（含已提醒过的）
    int fresh = 0;     // 其中不在账本里的
    int posted = 0;    // 真交给平台发出去的（≤ kMaxPerRound）
    int capped = 0;    // 超出上限、本轮没发的（下一轮还会是 fresh，不会被忘掉）
    int retired = 0;   // 已不再待处理，从账本摘掉（下次回到该状态还得再响）
    bool dnd = false;
    std::vector<Pending> to_post;
};

/// 一轮通知的判据部分（不碰平台）：账本进、账本出。
/// cfg.dnd 为真时整轮跳过且**不清账**：清了的话他哪天把通知打开，几天前那些早已处理完的会话会一起涌出来。
/// just_finished 是「本进程先前看见它在动、这一轮停了」的身份键集合，只有 cfg.done 开着才用得上；
/// 传 nullptr 等于这一档永远不响（界面层没接住跳变记忆时就不该扩这条判据）。
/// 每轮最多发 kMaxPerRound 条（同 Swift 的 `pending.prefix(4)`），一次吵人四下已经够多了。
constexpr int kMaxPerRound = 4;
AlertRound select_alerts(const Snapshot& snap, const std::set<std::string>& ledger,
                         const AlertCfg& cfg, std::set<std::string>* out_ledger,
                         const std::set<std::string>* just_finished = nullptr);

/// 券到账的一轮判定。与 select_alerts 同一套规矩：dnd 整轮跳过且不清账、
/// 每轮最多 kMaxPerRound 条、不再 ready 的券键从账本摘掉。
/// cfg.claims 关掉时不改账本（只跳过），否则他哪天把开关打开，前几天攒下的会一起涌出来。
/// Pending 里：agent_id＝厂商 id、agent_name＝界面上的名字、session_id＝整条券键
/// （横幅点回来时靠它的前缀认出那是券、落到权益屏）、title 留空：文案由界面层按语言现做。
AlertRound select_claim_alerts(const std::vector<Claim>& claims,
                               const std::set<std::string>& ledger, const AlertCfg& cfg,
                               std::set<std::string>* out_ledger);

/// 一行通知的身份键：agent id + 会话 id。单独用会话 id 会跨采集器撞名（两家都用 uuid）。
std::string alert_key(const std::string& agent_id, const std::string& session_id);

// ── 落盘位置 ────────────────────────────────────────────────────────────────
// Apple: ~/Library/Application Support/AgentPing/    其它: $XDG_CONFIG_HOME|~/.config/AgentPing/
// AGENTPING_STATE_DIR 整体覆盖（取证专用：跑两次证明去重时不许往他正在用的目录里写测试身份）。
std::string state_dir();
std::string alert_ledger_path();     // 去重账本：每行一个身份键
std::string alert_settings_path();   // 设置屏那八个开关，一行一个 key=值，跨重启留住

std::set<std::string> load_alerted();
/// 写账本。返回 false 且 err 非空时调用方必须把 err 打出来：账本写不进去＝去重失效，
/// 每轮重扫都会重发同一批横幅，那是比不响更糟的失效方式。
bool save_alerted(const std::set<std::string>& ids, std::string* err);

/// 读设置；AGENTPING_SOUND / AGENTPING_DND 覆盖（0/1），让「免打扰确实压住了」可机器验证。
AlertCfg load_alert_cfg();
bool save_alert_cfg(const AlertCfg& cfg, std::string* err);

}  // namespace ap
