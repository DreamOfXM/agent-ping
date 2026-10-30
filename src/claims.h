#pragma once
#include <string>
#include <vector>

// 权益领取层的数据契约。与 scan.h 的 QuotaWindow 同一套规矩：
// 每一档自带 known／非零判定，取不到的格子界面整块不画，绝不用 0 或猜测顶上去。
//
// 这一层只读 —— 读三家各自写在磁盘上的东西。不点按钮、不读凭据、不碰 keychain。
// 「自动领取」是另一档能力，必须先有只读证据链，才有资格讨论要不要动它的手。
namespace ap {

struct Claim {
    std::string vendor;         // workbuddy / qoder / zcode
    std::string name;           // 界面上念的名字
    /// false ＝ 这一轮本机没有任何可用读数。界面走「状态未知」那块，不猜。
    bool known = false;
    bool ready = false;         // 它亲口说现在有可领的
    bool done = false;          // 本期已经领过
    long credit = 0; bool credit_known = false;
    /// 连续签到天数 —— 实测只有 WorkBuddy 给（streak_days）。
    int streak = 0; bool streak_known = false;
    /// 未读通知条数 —— 实测只有 Qoder 给（notificationCount）。与 streak 不同义，不混用。
    int notif = 0; bool notif_known = false;
    /// 它自己的活动面板被打没打开过。领没领是服务端的事，这条只用来去重：
    /// 他自己已经点开看过了，就不该再催第二次。
    bool opened = false;
    double appears_at = 0;      // 这一期从哪一刻起可见（本机观测，不是服务端给的）
    double expires_at = 0;      // 服务端给的失效时刻；0 ＝ 这一家没给 → 时间轨画虚线
    /// 上一次本机读到「有得领」的时刻。0 ＝ 日志里从没 true 过。
    /// 界面用它说「上次说有卡是几点」，不用它反推「已经领过了」。
    double last_ready_at = 0;
    /// 本机最后一次看到服务端完整清单（含每期 claimStatus／endAt）的时刻。
    /// 早于 last_ready_at 的清单已被之后的读数取代，只能当出处看，不许据此判「已领」。
    double list_at = 0;
    double seen_at = 0;         // 这条读数最后一次被观察到的时刻
    double log_mtime = 0;
    std::string source;         // 读的是哪个文件，原样路径
    /// 卡上那句「差在哪一档」的文案键；空 ＝ 没有缺口。采集层只给键，翻成哪种语言由界面定。
    std::string note_key;
    /// note_key 是个带 %s 的两段式模板时，嵌进去的那半句的键；空 ＝ note_key 自身就是全文。
    std::string note_arg_key;
};

std::vector<Claim> claims_scan(double now_epoch);
std::string claims_dump(const std::vector<Claim>& cs);

}  // namespace ap
