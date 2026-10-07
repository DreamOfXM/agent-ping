#pragma once
#include <string>
#include <vector>

// 会话状态采集层：读本机各 CLI 自己的日志/数据库，判「谁在跑、谁等你、今天花了多少」。
// 判据逐条对齐 Swift 侧 Sources/AgentPing/{Models,Collectors/*}.swift 里带实测注释的常量。
namespace ap {

enum State { ST_WORKING, ST_NEEDS_USER, ST_AWAIT, ST_QUEUED, ST_FAILED, ST_RESTING };

struct Session {
    std::string id;
    std::string title;
    std::string project;
    std::string cwd;
    double last_activity = 0;
    State state = ST_RESTING;
    bool inferred_wait = false;   // 「疑似等授权」：反推出来的，不许响铃
    long tokens = 0;
    double credits = 0;
    std::string error_text;
    /// 窗内人打出去的指令条数（= transcript 里不带 toolUseResult 的 user 记录数）。
    /// 稿子的进度条文案「第 77 轮 · 长度未知」就是它 —— 分母本机四路日志都没有，
    /// 所以走流动条而不是编一个总轮数。
    int turns = 0;
    /// 队列实时深度，0 = 没有实时队列。只有 Claude 的 queue-operation 会给出非零值。
    int queue_pos = 0;
    /// CLI 亲口问的原话（Notification hook 正文）。空 = 这一行只有状态、没有问题块。
    std::string question;
    std::vector<std::string> options;
    std::string tool;             // 停在「允许吗」上的工具名
    /// 活跃度迷你柱（稿子 Sparkline 的输入）：会话尾部 24 格 × 15 分钟的 assistant 记录数。
    /// 长度 <2 ＝ 这一路没有可画的形状，界面整块不画。
    std::vector<int> spark;
};

struct AgentSnap {
    std::string id;
    std::string name;
    bool available = false;     // false = 本机根本没有这个数据源，不参与「今天没活动」
    std::string scan_error;     // 有源但读失败；空 = 正常
    std::vector<Session> sessions;
};

struct Snapshot {
    std::vector<AgentSnap> agents;
    double scanned_at = 0;
    double window_start = 0;    // 今天零点，三格统计与「今天没活动」共用同一个窗
    long today_tokens = 0;
    double today_credits = 0;
    /// 昨天同一个钟点之前那一个整日的用量，只为表头第三格的「较昨日」环比服务。
    /// 两侧都是 0 时那一行不画（没数据源不占位）。
    long yesterday_tokens = 0;
    double yesterday_credits = 0;
    /// hook 收件箱的三个数：读到几条、真落到会话上几条、解析不了几条。
    /// 只报 live 会把已作废的也算成待处理，面板就比真相急。
    int hook_live = 0, hook_landed = 0, hook_dropped = 0;
};

/// 全量重扫一轮。内部按 (mtime, size, 窗起点) 缓存「尾部判定」和「窗内用量」，
/// 所以每几秒调一次只会重读这一轮真的被写过的文件。
Snapshot scan_all(double now_epoch);

/// --dump 用：把一轮扫描打成可读文本，便于和面板读数对账。
std::string snapshot_dump(const Snapshot& s);

/// 把 CLI 的 Notification hook 收件箱盖到快照上：只有信号晚于该会话最后一条日志才算数。
/// 三个计数写回 snap（live / landed / dropped）；落不上的会往 stderr 写一行原因。
/// 口径逐条对齐 Swift 侧 HookInbox.swift。
void apply_hook_signals(Snapshot& snap, double now);

}  // namespace ap
