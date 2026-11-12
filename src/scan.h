#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

// 会话状态采集层：读本机各 CLI 自己的日志/数据库，判「谁在跑、谁等你、今天花了多少」。
// 判据逐条对齐 Swift 侧 Sources/AgentPing/{Models,Collectors/*}.swift 里带实测注释的常量。
namespace ap {

enum State { ST_WORKING, ST_NEEDS_USER, ST_AWAIT, ST_QUEUED, ST_FAILED, ST_RESTING };

/// 一行会话的「等你」证据出自哪条通道（详情屏「来源」那一格按它选措辞）。
enum AwaitSrc { QS_LOG = 0, QS_DB, QS_HOOK };

/// 用量四档。各家字段名不同，进这一层前必须已经归一到同一个加法口径：
/// 总用量 = 输入 + 输出 + 缓存读取 + 缓存写入。Codex 的 input 自带 cached，做差后才能放进来。
struct UsageParts {
    long input = 0, output = 0, cache_read = 0, cache_write = 0;
    long total() const { return input + output + cache_read + cache_write; }
};

struct ModelUse {
    std::string name;
    long tokens = 0;
};

/// 账户级额度的一格。known=false ＝ 这个来源没给这一档，界面整格不画（不填 0）。
/// resets_at 是 epoch 秒；seen_at 是这条读数最后一次被观察到的时刻，界面据此判「旧读数」。
struct QuotaWindow {
    bool known = false;
    double used = 0;
    double resets_at = 0;
    int window_min = 0;
    double seen_at = 0;
};

/// 账户余额：不是滚动窗口，而是「这一期还剩多少」。ZCode 在 v2/logs 写
/// billing/balance，Qoder 在 App 的 main.log 写 account.getQuotaUsage，两家都只给这一形态。
/// 分母、单位、到期时刻都可能没给 —— 各自带 known，界面缺哪格不画哪格。
struct Balance {
    bool known = false;
    double remaining = 0;
    double total = 0;           // 0 ＝ 只给了余量，没有分母 → 不编百分比
    std::string unit;           // 归一后的单位：tokens / Credits；空 ＝ 这一路没说
    std::string label;          // 它自己给的名字（ZCode 的 show_name）；空 ＝ 没有
    double period_end = 0;      // 这一期到期的时刻；0 ＝ 没给（Qoder 的 9999 年按没给）
    double seen_at = 0;         // 这条读数写下的时刻（不是我们读它的时刻）
    bool exceeded = false;      // 它自己报「配额已用尽」
    std::string source;         // 读的是哪个文件，对账用
};

/// 账户级额度：只放本机日志真给出的东西。重置卡是另一回事（资源，不是周期重置），
/// 当前没有任何一路本地日志带它，所以这里不建字段，界面也不画那一块。
struct Quota {
    bool known = false;
    std::string plan;
    QuotaWindow five, week;
    double credits_balance = 0;
    bool credits_known = false;
    Balance balance;
};

struct Session {
    std::string id;
    /// 子代理对话（ZCode 的 subagent_child：主任务派出去的调研员/执行者）。
    /// 完工会并回主会话，但不该因此看不见——界面按「任务下可展开的对话」摆。
    struct Child {
        std::string id;      // 裸 session id（不带 zcode- 前缀，与 native_id 同口径）
        std::string title;
        bool running = false;
        double updated = 0;
    };
    std::vector<Child> children;
    std::string title;
    std::string project;
    std::string cwd;
    double last_activity = 0;
    /// 这一条是哪一刻建出来的（ZCode 的 session.time_created）。0 = 这一路没给。
    /// ZCode 落点用它分清两种「侧栏没这一条」：本次启动之后新建的，它这次运行根本不列。
    double created = 0;
    State state = ST_RESTING;
    bool inferred_wait = false;   // 「疑似等授权」：反推出来的，不许响铃
    long tokens = 0;
    double credits = 0;
    /// 今日用量四档与主模型。tokens 是四档之和（与 UsageParts::total 同一口径）。
    UsageParts usage;
    std::string model;
    std::string error_text;
    /// 窗内人打出去的指令条数（= transcript 里不带 toolUseResult 的 user 记录数）。
    /// 稿子的进度条文案「第 77 轮 · 长度未知」就是它 —— 分母本机四路日志都没有，
    /// 所以走流动条而不是编一个总轮数。
    int turns = 0;
    /// 队列实时深度，0 = 没有实时队列。只有 Claude 的 queue-operation 会给出非零值。
    int queue_pos = 0;
    /// CLI 亲口问的原话（hook 正文，或 ZCode 会话库里那颗挂起的提问工具）。
    /// 空 = 这一行只有状态、没有问题块。
    std::string question;
    std::vector<std::string> options;
    /// 与 options 一一对应的候选说明（AskUserQuestion 的 options[].description）。
    /// 长度可能短于 options：缺口那头当「没有说明」，界面不画第二行。
    std::vector<std::string> option_desc;
    std::string tool;             // 停在「允许吗」上的工具名
    /// 问题块从哪条通道拿到的，只决定详情屏「来源」那一行念哪一句，不参与判态与响铃。
    /// ZCode/WorkBuddy/OpenCode 没有 hook 通道（--hook-status 实测 supported=0），
    /// 会话库是它们唯一的来源；hook 盖上来的时候改 QS_HOOK。
    AwaitSrc await_src = QS_LOG;
    /// 这一次「等你」从哪一刻开始：hook 信号落地的时刻，或 ZCode 那颗提问工具落地的时刻。
    /// 0 = 采集层没给（或只是推断），界面退回用 last_activity 算等待时长。
    /// 没有它，今天刚落的授权会显示成日志最后一条的年纪。
    double await_since = 0;
    /// 活跃度迷你柱（稿子 Sparkline 的输入）：会话尾部 24 格 × 15 分钟的 assistant 记录数。
    /// 长度 <2 ＝ 这一路没有可画的形状，界面整块不画。
    std::vector<int> spark;
    /// 今天这一段从哪一刻开始（transcript 里第一条今天的时间戳）。只用来算 `.live` 的
    /// 「连续 4h 12m」：起点落在今天之前就留 0，界面不画这一段 —— 跨夜的三天不能被念成「连续」。
    double first_activity = 0;
    /// 尾部最后一条 tool_use 的真命令（Bash 的 command / 编辑类的 file_path）。
    /// `.live` 的 `$ cmd` 只画这条原文；没有就整行不画，不用标题冒充命令。
    std::string last_cmd;
    /// N1 循环证据：尾部连续 ≥3 条完全相同的 tool_use 原文（日志事实，不是「没输出」推断）。
    /// <3 ＝ 没有循环证据。只标不响——进告警档得先过 alert_probe，这里不动告警语义。
    int loop_count = 0;
    /// 重复段第一条的时刻；0＝没有循环。与 last_activity 相减即「重复了多久」。
    double loop_from = 0;
    /// N2 循环证据逐条 (时刻, 命令原文)，与 loop_count 同一段切出来的。空＝没有。
    std::vector<std::pair<double, std::string>> loop_events;
    /// N3 操作记录（今日窗，tool_use 聚合）：改过的文件去重保序；命令留最后 12 条首行
    /// （连续相同压成一条）。*_more＝超出上限被截断的条数，界面要如实念「另有 N」。
    std::vector<std::string> op_files, op_cmds;
    int op_files_more = 0, op_cmds_more = 0;
    /// 首条 user 指令原文（接力简报的「任务」行）。空＝这一路没给。
    std::string first_prompt;
    /// N7 进程哨兵：回合未收尾（ST_WORKING）但这一路 CLI 已无同名进程＝事实。只标不响。
    /// 只对 comm 可辨的路判（本轮＝codex）；node 系 CLI 的 comm 是 node，认不出，不判。
    bool proc_dead = false;
    /// hook 信号自带的触发说明（Notification 的 message）。只有 hook 真给了才非空：
    /// 授权详情用它顶替「工具」那一行，日志推出来的推断不许编一句说明。
    std::string trigger;
    /// 桌面 App 自己的会话库（main.sqlite → chat_sessions）里有这一条。
    /// Qoder 的 App 与 CLI 共用 ~/.qoder/projects 这一个日志目录，日志里两种会话都写
    /// entrypoint:"cli"，认不出来源；被 App 索引到过是唯一能拿得出手的正证。
    /// 只当正证用：false 不代表「它是 CLI」，只代表「这一条的形态我们不知道」。
    bool app_indexed = false;
};

struct AgentSnap {
    std::string id;
    std::string name;
    bool available = false;     // false = 本机根本没有这个数据源，不参与「今天没活动」
    std::string scan_error;     // 有源但读失败；空 = 正常
    std::vector<Session> sessions;
    /// 今日按工具的汇总。只由本文件今天的会话累出来，与逐会话相加恒等。
    UsageParts usage;
    std::vector<ModelUse> models;        // 今日按模型，降序
    std::array<long, 24> hourly{};       // 今日本机 0–23 点各小时的总 token
    Quota quota;                         // 账户级；空 = 这一路没有可用的额度源
    std::string client = "CLI";          // CLI / IDE（稿子的身份行）
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
    /// Qoder 桌面 App 的运行状态盖上了几条（见 QoderLive）。0 ＝ 这一轮 App 没给可用读数，
    /// 那 qoder 那一行的状态就仍是由日志时间戳反推的。
    int live_landed = 0;
};

/// Qoder 桌面 App 自己按会话提交的运行状态。它每次状态变更往
/// `…/com.qoder.app.stable/logs/<run>/main.log` 写一行
/// `ChatSession control state committed {"sessionId":…,"runtimeState":…,…}`，
/// 实测取值 cold／opening／running／waiting-user／ready／interrupted（本机 14 份日志 1337 行）。
/// 这是「它现在到底在不在跑」的正证，比按日志静默多久反推强一整档。
struct QoderLive {
    double ts = 0;
    std::string state;      // runtimeState 原文
    int pending = 0;        // pendingInteractionCount：>0 ＝ 它正等你点一下
};

/// 认一行 control state；不是这一行返回 false。解析与读盘分开，探针才能拿真日志行喂它。
bool parse_qoder_control(const std::string& line, std::string& sid, QoderLive& out);

/// 读最新一轮 App 日志，按 sessionId 留最新的那行。桌面 App 没跑过 ＝ 空表。
std::map<std::string, QoderLive> read_qoder_live();

/// 把 App 报的运行状态盖到 qoder 那一路上：running/opening 压掉「90 秒没回＝疑似等授权」
/// 的反推，waiting-user（或 pendingInteractionCount>0）才算真等你、才许响铃。
/// 返回盖上几条。必须在 apply_hook_signals 之前调 —— CLI 亲口问出来的那句话优先级更高。
int apply_qoder_live(Snapshot& snap, const std::map<std::string, QoderLive>& live, double now);

/// 全量重扫一轮。内部按 (mtime, size, 窗起点) 缓存「尾部判定」和「窗内用量」，
/// 所以每几秒调一次只会重读这一轮真的被写过的文件。
Snapshot scan_all(double now_epoch);

/// hook 收件箱的实际路径（AGENTPING_HOOK_DIR 覆盖优先）。接线屏要把这条原样写出来，
/// 路径只有这一处真源 —— 面板自己拼一遍就会在 XDG／Linux 上跟采集层分叉。
std::string hook_inbox_dir();

/// --dump 用：把一轮扫描打成可读文本，便于和面板读数对账。
std::string snapshot_dump(const Snapshot& s);

/// 把 CLI 的 Notification hook 收件箱盖到快照上：只有信号晚于该会话最后一条日志才算数。
/// 三个计数写回 snap（live / landed / dropped）；落不上的会往 stderr 写一行原因。
/// 口径逐条对齐 Swift 侧 HookInbox.swift。
void apply_hook_signals(Snapshot& snap, double now);

/// N7 进程哨兵：回合未收尾但这一路 CLI 已无同名进程（只读 comm/pid）→ Session.proc_dead。
/// 只对 comm 可辨的路判（本轮 codex）；只标不响。
void apply_proc_sentinel(Snapshot& snap);

}  // namespace ap
