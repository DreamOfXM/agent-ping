// 提醒层的取证工具：不渲染任何像素，只把「该不该响、响过没有、通道是什么状态」摊开。
// 存在的理由：菜单栏 App 最常见的失效方式是「系统没给通知权限」，而它表现出来只是不响 ——
// 没有这条，用户只能靠「没弹」来猜，而没弹的成因可能是权限、签名、免打扰、也可能是判据。
//
// 退出码与闸门同规矩：0 通过 / 1 测了且不合格 / 2 根本没跑成。
#include "alert.h"
#include "platform/notify.h"
#include "scan.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void expect(const char* what, long got, long want) {
    bool ok = got == want;
    printf("%s %-46s 期望 %ld｜实得 %ld\n", ok ? "[通过]" : "[不符]", what, want, got);
    ok ? g_pass++ : g_fail++;
}
void expect_true(const char* what, bool got) {
    printf("%s %-46s 期望 真｜实得 %s\n", got ? "[通过]" : "[不符]", what, got ? "真" : "假");
    got ? g_pass++ : g_fail++;
}

ap::Session ses(const char* id, const char* title, ap::State st, bool inferred = false) {
    ap::Session s;
    s.id = id; s.title = title; s.project = "probe"; s.state = st; s.inferred_wait = inferred;
    return s;
}
/// 一张券的样例读数。只填判据看得见的字段：ready 是「它亲口说有得领」，
/// appears_at 是这一期的可见时刻（键就按它落在哪一天算）。
ap::Claim clm(const char* vendor, const char* name, bool ready, double appears_at,
              long credit = 0) {
    ap::Claim c;
    c.vendor = vendor; c.name = name; c.known = true; c.ready = ready; c.appears_at = appears_at;
    if (credit > 0) { c.credit = credit; c.credit_known = true; }
    return c;
}
/// 今天本地 09:00 与明天本地 09:00：跨「期次」的两条读数，差一天。
void two_days(double* d0, double* d1) {
    time_t t = std::time(nullptr);
    struct tm lt{};
    ::localtime_r(&t, &lt);
    lt.tm_hour = 9; lt.tm_min = 0; lt.tm_sec = 0;
    const time_t base = ::mktime(&lt);
    *d0 = (double)base;
    *d1 = (double)(base + 86400);
}
ap::Snapshot snap_with(std::initializer_list<ap::Session> qoder,
                       std::initializer_list<ap::Session> claude = {}) {
    ap::Snapshot s;
    s.scanned_at = 1;
    ap::AgentSnap a; a.id = "qoder"; a.name = "Qoder CLI"; a.available = true;
    for (auto& x : qoder) a.sessions.push_back(x);
    s.agents.push_back(a);
    if (claude.size()) {
        ap::AgentSnap b; b.id = "claude"; b.name = "Claude Code"; b.available = true;
        for (auto& x : claude) b.sessions.push_back(x);
        s.agents.push_back(b);
    }
    return s;
}

int selftest() {
    const char* dir = getenv("AGENTPING_STATE_DIR");
    if (!dir || !*dir) {
        fprintf(stderr, "[自检] 拒绝跑：AGENTPING_STATE_DIR 没设。\n"
                        "       这条会把测试身份写进账本文件，不许落进你正在用的「%s」。\n",
                ap::state_dir().c_str());
        return 2;
    }
    printf("[自检] 状态目录＝%s\n", dir);

    // ① 判据表：逐条对齐 Swift Engine.swift:285-294
    expect_true("判据：失败中断要响", ap::should_alert(ap::ST_FAILED, false));
    expect_true("判据：等你回复要响", ap::should_alert(ap::ST_NEEDS_USER, false));
    expect_true("判据：真卡在授权上要响", ap::should_alert(ap::ST_AWAIT, false));
    expect_true("判据：疑似等授权不响（误报）", !ap::should_alert(ap::ST_AWAIT, true));
    expect_true("判据：排队中不响", !ap::should_alert(ap::ST_QUEUED, false));
    expect_true("判据：运行中/已结束不响",
                !ap::should_alert(ap::ST_WORKING, false) && !ap::should_alert(ap::ST_RESTING, false));

    // ② 首轮：三响（含一条 inferred、一条 queued 必须被排除）
    auto s = snap_with({ ses("a", "第一条等你", ap::ST_NEEDS_USER),
                         ses("b", "第二条失败", ap::ST_FAILED),
                         ses("c", "第三条等授权", ap::ST_AWAIT),
                         ses("d", "疑似等授权", ap::ST_AWAIT, true),
                         ses("e", "排队里", ap::ST_QUEUED) });
    std::set<std::string> led;
    std::set<std::string> out;
    // 后面七轮跑的都是「四档全开、done 关」＝默认设置，与 ① 那条 should_alert 判据表必须等价。
    ap::AlertCfg kOn;
    ap::AlertCfg kDnd; kDnd.dnd = true;
    ap::AlertRound r1 = ap::select_alerts(s, led, kOn, &out);
    expect("首轮：判据命中条数", r1.hits, 3);
    expect("首轮：其中首次出现", r1.fresh, 3);
    expect("首轮：待发出", (long)r1.to_post.size(), 3);
    expect("首轮：被上限压掉", r1.capped, 0);
    expect("首轮：账本条数", (long)out.size(), 3);
    led = out;

    // ③ 第二轮同一份数据：一条都不许多响（去重是真的，不是靠运气）
    ap::AlertRound r2 = ap::select_alerts(s, led, kOn, &out);
    expect("次轮：判据仍命中", r2.hits, 3);
    expect("次轮：首次出现归零", r2.fresh, 0);
    expect("次轮：待发出归零", (long)r2.to_post.size(), 0);
    expect("次轮：账本不增长", (long)out.size(), 3);
    led = out;

    // ④ 会话从待处理里消失 → 必须摘账；再回来 → 必须还能响（不然第二次问就不吵你了）
    auto s2 = snap_with({ ses("a", "第一条等你", ap::ST_NEEDS_USER),
                          ses("b", "答完了", ap::ST_RESTING),
                          ses("c", "第三条等授权", ap::ST_AWAIT),
                          ses("d", "疑似等授权", ap::ST_AWAIT, true) });
    ap::AlertRound r3 = ap::select_alerts(s2, led, kOn, &out);
    expect("摘账：命中", r3.hits, 2);
    expect("摘账：摘掉条数", r3.retired, 1);
    expect("摘账：账本剩", (long)out.size(), 2);
    led = out;
    ap::AlertRound r4 = ap::select_alerts(s, led, kOn, &out);
    expect("回到待处理：还能响", (long)r4.to_post.size(), 1);
    expect("回到待处理：账本补回", (long)out.size(), 3);
    led = out;

    // ⑤ 上限：六条只发四条。Swift 侧把六条全部记进账本，于是第五、六条永远不会有横幅
    //    （Engine.swift:315 formUnion 用的是整个 pending 而不是 prefix(4) 的）——
    //    这里明写为「被压掉的不下轮就忘掉」，是与 Swift 的一处有意分歧，不是抄错。
    auto big = snap_with({ ses("1", "一", ap::ST_FAILED), ses("2", "二", ap::ST_FAILED),
                           ses("3", "三", ap::ST_FAILED), ses("4", "四", ap::ST_FAILED),
                           ses("5", "五", ap::ST_FAILED), ses("6", "六", ap::ST_FAILED) });
    std::set<std::string> blank;
    ap::AlertRound r5 = ap::select_alerts(big, blank, kOn, &out);
    expect("上限：本轮发出", (long)r5.to_post.size(), ap::kMaxPerRound);
    expect("上限：被压掉", r5.capped, 2);
    expect("上限：账本只记发出的", (long)out.size(), (long)ap::kMaxPerRound);
    ap::AlertRound r6 = ap::select_alerts(big, out, kOn, &out);
    expect("上限：压掉的下轮仍是首次", r6.fresh, 2);

    // ⑥ 免打扰：整轮跳过且不清账。先落一份已知账本，两条断言才有真内容可比
    //    （空账本 vs 空账本相等是永远成立的废话）。
    std::string err;
    std::set<std::string> seed = { ap::alert_key("qoder", "a"), ap::alert_key("qoder", "b") };
    expect_true("落盘：先写两份已知身份", ap::save_alerted(seed, &err));
    std::set<std::string> before = ap::load_alerted();
    expect("落盘：读回条数", (long)before.size(), 2);
    led = before;
    ap::AlertRound r7 = ap::select_alerts(s, led, kDnd, &out);
    expect("免打扰：不发", (long)r7.to_post.size(), 0);
    expect("免打扰：命中数不统计（整轮跳过）", r7.hits, 0);
    expect("免打扰：摘账数不统计", r7.retired, 0);
    expect_true("免打扰：账本原样不动（不清账）", out == before);

    // ⑦ 落盘往返：整份账本写进去读出来必须逐键相等；跨重启留住是这条承诺的全部内容
    expect_true("落盘：整份回写成功", ap::save_alerted(led, &err));
    if (!err.empty()) printf("        %s\n", err.c_str());
    expect_true("落盘：读回逐键相等", ap::load_alerted() == led);
    expect("落盘：文件行数＝条数", (long)ap::load_alerted().size(), (long)led.size());

    // ⑧ 设置：sound/dnd 写入读回，env 覆盖只在取证时生效
    ap::AlertCfg cfg; cfg.sound = false; cfg.dnd = true;
    expect_true("设置：写成功", ap::save_alert_cfg(cfg, &err));
    unsetenv("AGENTPING_SOUND"); unsetenv("AGENTPING_DND");
    ap::AlertCfg back = ap::load_alert_cfg();
    expect_true("设置：提示音关着读回来还是关", !back.sound);
    expect_true("设置：免打扰开着读回来还是开", back.dnd);
    setenv("AGENTPING_DND", "0", 1);
    expect_true("设置：env 能压过文件（取证专用）", !ap::load_alert_cfg().dnd);
    unsetenv("AGENTPING_DND");
    setenv("AGENTPING_CLAIMS", "0", 1);
    expect_true("设置：env 能压过文件（权益档）", !ap::load_alert_cfg().claims);
    unsetenv("AGENTPING_CLAIMS");

    // ⑧b 设置屏写下去的每一档必须全数读回来：界面上亮着、文件里没记，重启后就是「骗人的开关」
    ap::AlertCfg all;
    all.sound = false; all.dnd = true; all.needs_user = true; all.await_perm = false;
    all.failed = true; all.done = true; all.on_top = false; all.max_rows = 5;
    all.claims = false;
    expect_true("设置：九档一起写", ap::save_alert_cfg(all, &err));
    ap::AlertCfg rb = ap::load_alert_cfg();
    expect_true("设置：读回逐档相等（提示音/免打扰）", rb.sound == all.sound && rb.dnd == all.dnd);
    expect_true("设置：读回逐档相等（权益到账）", rb.claims == all.claims);
    expect_true("设置：读回逐档相等（四档状态）",
                rb.needs_user == all.needs_user && rb.await_perm == all.await_perm &&
                rb.failed == all.failed && rb.done == all.done);
    expect_true("设置：读回逐档相等（置顶/每组条数）",
                rb.on_top == all.on_top && rb.max_rows == all.max_rows);
    {
        // 用户手改过的设置文件：错键不许吞掉其它档，0 条这种自杀值不许当设置生效
        std::ofstream mf(ap::alert_settings_path(), std::ios::binary | std::ios::trunc);
        mf << "sound=1\nbogus_key=7\nalert_failed=0\nmax_rows=0\n";
        mf.close();
        ap::AlertCfg hand = ap::load_alert_cfg();
        expect_true("手改：认得的键读到了", hand.sound && !hand.failed);
        expect_true("手改：不认得的键没把默认档一起吞掉", hand.needs_user && hand.await_perm);
        expect("手改：max_rows=0 回落默认（摆不出行不是设置）", hand.max_rows, 8);
    }

    // ⑨ 「需要提醒的状态」四档：默认必须与 should_alert 逐条等价，只许关不许开（done 除外，它另有条门）
    ap::AlertCfg def;
    long same = 1;
    const ap::State st_all[6] = { ap::ST_WORKING, ap::ST_NEEDS_USER, ap::ST_AWAIT,
                                  ap::ST_QUEUED, ap::ST_FAILED, ap::ST_RESTING };
    for (ap::State st : st_all)
        for (int inf = 0; inf < 2; inf++)
            if (ap::alertable(def, st, inf != 0, false) != ap::should_alert(st, inf != 0)) same = 0;
    expect_true("档位：默认四档与 should_alert 逐条等价（12 组）", same);
    {
        ap::AlertCfg off = def; off.needs_user = false;
        expect_true("档位：关掉「等你回复」后这一档不响",
                    !ap::alertable(off, ap::ST_NEEDS_USER, false, false));
        off = def; off.await_perm = false;
        expect_true("档位：关掉「等待确认」后这一档不响", !ap::alertable(off, ap::ST_AWAIT, false, false));
        off = def; off.failed = false;
        expect_true("档位：关掉「失败中断」后这一档不响", !ap::alertable(off, ap::ST_FAILED, false, false));
        expect_true("档位：关掉的档不许把误报（inferred）放进来",
                    !ap::alertable(off, ap::ST_AWAIT, true, false));
    }
    {
        // 「已完成」是唯一往外扩的一档：开着但没有跳变证据也不许响，否则第一次打开会把
        // 窗口里所有历史静置会话当成刚跑完，连吵十几轮。
        ap::AlertCfg done = def; done.done = true;
        expect_true("已完成：关着时，带跳变证据也不响",
                    !ap::alertable(def, ap::ST_RESTING, false, true));
        expect_true("已完成：开着但没跳变证据不响",
                    !ap::alertable(done, ap::ST_RESTING, false, false));
        expect_true("已完成：开着且有跳变证据才响",
                    ap::alertable(done, ap::ST_RESTING, false, true));
        auto sd = snap_with({ ses("r1", "刚跑完", ap::ST_RESTING), ses("r2", "早就凉了", ap::ST_RESTING) });
        std::set<std::string> jf = { ap::alert_key("qoder", "r1") };
        std::set<std::string> led2, out2;
        ap::AlertRound r8 = ap::select_alerts(sd, led2, done, &out2, &jf);
        expect("已完成：一轮里只认那条真跳变的", r8.hits, 1);
        expect("已完成：待发出", (long)r8.to_post.size(), 1);
        led2 = out2;
        std::set<std::string> jf2;                       // 下一轮没有新的跳变
        ap::AlertRound r9 = ap::select_alerts(sd, led2, done, &out2, &jf2);
        expect("已完成：次轮不重复吵", (long)r9.to_post.size(), 0);
        expect("已完成：跳变过去后就摘账（下次跑完还能再报）", r9.retired, 1);
    }

    {
        // ⑩ 券到账：键的形状。同一天同一家＝同一条期次（只响一次），换一天＝新期次（该再响）。
        double d0, d1; two_days(&d0, &d1);
        const std::string k0 = ap::claim_alert_key("workbuddy", d0);
        expect("券键：带上前缀才算券（与会话键分家）", ap::is_claim_key(k0) ? 1 : 0, 1);
        expect("券键：会话键不会被误认成券", ap::is_claim_key(ap::alert_key("qoder", "a")) ? 1 : 0, 0);
        expect("券键：同一天同一家同键", ap::claim_alert_key("workbuddy", d0 + 600) == k0 ? 1 : 0, 1);
        expect("券键：换日是新期次", ap::claim_alert_key("workbuddy", d1) == k0 ? 1 : 0, 0);
        expect("券键：别家不共用一条", ap::claim_alert_key("qoder", d0) == k0 ? 1 : 0, 0);

        const std::vector<ap::Claim> one = { clm("workbuddy", "WorkBuddy", true, d0, 100),
                                             clm("qoder", "Qoder", false, d0) };
        std::set<std::string> cled, cout;
        ap::AlertRound c1 = ap::select_claim_alerts(one, cled, kOn, &cout);
        expect("券：只有它说有得领才够格", c1.hits, 1);
        expect("券：首轮待发出", (long)c1.to_post.size(), 1);
        expect_true("券：横幅身份带整条券键",
                    c1.to_post.size() == 1 && c1.to_post[0].session_id == k0);
        expect("券：首轮落账", (long)cout.size(), 1);
        cled = cout;
        // 次轮同一期：不许多响（这一条是「每天 00:00 那条券会不会被 3 秒重扫吵穿」的答案）
        ap::AlertRound c2 = ap::select_claim_alerts(one, cled, kOn, &cout);
        expect("券：次轮仍命中但不重复吵", (long)c2.to_post.size(), 0);
        expect("券：次轮摘账为 0", c2.retired, 0);
        // 换期：新一天是新键，必须再响一次
        const std::vector<ap::Claim> next_day = { clm("workbuddy", "WorkBuddy", true, d1, 100),
                                                  clm("qoder", "Qoder", false, d0) };
        ap::AlertRound c3 = ap::select_claim_alerts(next_day, cled, kOn, &cout);
        expect("券：换期再响", (long)c3.to_post.size(), 1);
        expect("券：旧期次随领掉摘账", c3.retired, 1);
        expect("券：账本只留当期", (long)cout.size(), 1);
        // 领掉了（ready 翻回 false）→ 摘账；摘了账下次这一期还能报（不会顶着「已提醒过」永远沉默）
        const std::vector<ap::Claim> none = { clm("workbuddy", "WorkBuddy", false, d1),
                                              clm("qoder", "Qoder", false, d0) };
        ap::AlertRound c4 = ap::select_claim_alerts(none, cout, kOn, &cout);
        expect("券：领掉后摘账", c4.retired, 1);
        expect("券：摘完账本空", (long)cout.size(), 0);
        ap::AlertRound c5 = ap::select_claim_alerts(next_day, cout, kOn, &cout);
        expect("券：摘过之后再回到可领还能响", (long)c5.to_post.size(), 1);
        // 两颗压投递的开关都不许清账
        std::set<std::string> before_c = cout;
        ap::AlertRound c6 = ap::select_claim_alerts(next_day, before_c, kDnd, &cout);
        expect_true("券：免打扰不发", c6.to_post.empty());
        expect_true("券：免打扰不清账", cout == before_c);
        ap::AlertCfg kOff = kOn; kOff.claims = false;
        ap::AlertRound c7 = ap::select_claim_alerts(next_day, before_c, kOff, &cout);
        expect_true("券：开关关掉不发", c7.to_post.empty());
        expect_true("券：开关关掉不清账", cout == before_c);
        expect("券：开关关掉时命中数不统计（整轮跳过）", c7.hits, 0);

        // 分界：两本共用一个文件，任何一轮都不许清掉对家的去重记录。
        // 少了这道分界，会话每 3 秒重扫就会把券键当「不再待处理」摘掉，那条券每轮都算新的。
        const std::string sk = ap::alert_key("qoder", "a"), ck = ap::claim_alert_key("workbuddy", d0);
        // 方向一：会话这轮重扫，且本轮没有任何待处理会话 —— 它该摘掉自己的会话键，券键必须活着。
        std::set<std::string> mix = { sk, ck }, m1;
        ap::Snapshot es; es.scanned_at = 1;
        ap::AlertRound i1 = ap::select_alerts(es, mix, kOn, &m1);
        expect("分界：会话轮摘掉的只有会话键", i1.retired, 1);
        expect("分界：券键活过会话轮", (long)m1.count(ck), 1);
        // 方向二：券这轮重扫，且这一期已经领掉 —— 反过来，会话的去重记录不许被它清掉。
        // 会话键故意留着：它是「刚响过、还在待处理」的那条，被券轮摘掉就等于下一轮再吵一遍。
        std::set<std::string> mix2 = { sk, ck }, m2;
        ap::AlertRound i2 = ap::select_claim_alerts(none, mix2, kOn, &m2);
        expect("分界：券轮摘掉的只有券键", i2.retired, 1);
        expect("分界：会话键活过券轮", (long)m2.count(sk), 1);
    }

    printf("[自检] 断言 %d 条，通过 %d，不符 %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}

const char* WordOf(ap::State st) {
    switch (st) {
    case ap::ST_FAILED: return "失败中断";
    case ap::ST_AWAIT:  return "等你授权";
    default:            return "已完成";   // 与面板同一说法（Swift Theme:10-01 拍板：答完了不是要你介入）
    }
}

int live_round(bool do_post) {
    ap::Snapshot s = ap::scan_all((double)time(nullptr));
    ap::AlertCfg cfg = ap::load_alert_cfg();
    std::set<std::string> led = ap::load_alerted();
    printf("[实况] 状态目录＝%s\n", ap::state_dir().c_str());
    printf("[实况] 设置＝提示音%s 免打扰%s｜状态四档 等你回复%s/等待确认%s/失败中断%s/已完成%s"
           "｜每组 %d 条｜置顶%s（env 覆盖：%s / %s）\n",
           cfg.sound ? "开" : "关", cfg.dnd ? "开" : "关",
           cfg.needs_user ? "开" : "关", cfg.await_perm ? "开" : "关",
           cfg.failed ? "开" : "关", cfg.done ? "开" : "关",
           cfg.max_rows, cfg.on_top ? "开" : "关",
           getenv("AGENTPING_SOUND") ? getenv("AGENTPING_SOUND") : "未设",
           getenv("AGENTPING_DND") ? getenv("AGENTPING_DND") : "未设");
    printf("[实况] 进账本的条数＝%zu\n", led.size());

    char why[512] = {0};
    int ch = ap_notify_channel(why, (int)sizeof why);
    printf("[通道] %s：%s\n", ch == 0 ? "横幅可用" : ch == 1 ? "横幅不可用" : "问不出来", why);

    std::set<std::string> out;
    ap::AlertRound r = ap::select_alerts(s, led, cfg, &out);
    printf("[判据] 命中 %d｜首次 %d｜待发 %zu｜压掉 %d｜摘账 %d｜免打扰%s\n",
           r.hits, r.fresh, r.to_post.size(), r.capped, r.retired, r.dnd ? "开（整轮跳过）" : "关");
    for (const auto& p : r.to_post) {
        std::string title = p.agent_name + " · " + WordOf(p.state);
        if (!do_post) { printf("  → %s｜%s｜只算不发（--post 才真发）\n", title.c_str(), p.title.c_str()); continue; }
        char rc_w[512] = {0};
        int rc = ap_notify_post(title.c_str(), p.title.c_str(), p.agent_id.c_str(),
                                p.session_id.c_str(), cfg.sound ? 1 : 0, rc_w, (int)sizeof rc_w);
        const char* kind = rc == 0 ? "横幅已投" : rc == 1 ? "兜底出声"
                       : rc == 3 ? "没回执，下轮再试" : "两边都不通";
        printf("  → %s｜%s｜%s：%s\n", title.c_str(), p.title.c_str(), kind, rc_w);
        // rc==3 不记账：这条发没发出去不知道，记了就等于静默丢掉一次「该吵你」
        if (rc == 3) out.erase(ap::alert_key(p.agent_id, p.session_id));
    }
    if (do_post) {
        std::string err;
        if (!ap::save_alerted(out, &err)) {
            printf("[账本] 写失败：%s —— 去重失效，下一轮会重发同一批\n", err.c_str());
            return 1;
        }
        printf("[账本] 已回写，条数 %zu（原 %zu）\n", out.size(), led.size());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool selftest_ = false, post_ = false, channel_ = false;
    int burst_n = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest_ = true;
        else if (!strcmp(argv[i], "--post")) post_ = true;
        else if (!strcmp(argv[i], "--check")) post_ = false;
        else if (!strcmp(argv[i], "--channel")) channel_ = true;
        else if (!strncmp(argv[i], "--burst=", 8)) burst_n = atoi(argv[i] + 8);
        else { fprintf(stderr, "未知参数 %s\n用法：alert_probe [--selftest|--check|--post|--channel|--burst=N]\n", argv[i]); return 2; }
    }
    if (burst_n > 0) {
        // 合成身份连发：不读真会话、不碰账本，只验平台那一层送不送得出去。
        // 存在的理由：Linux 侧要走真数据得先造一整套假日志，而这里要测的只有 notify 后端与出声器。
        int ok = 0;
        const int want_sound = ap::load_alert_cfg().sound ? 1 : 0;   // 与 App 同一口径：设置里关着就不许响
        for (int i = 0; i < burst_n; i++) {
            char ident[32]; snprintf(ident, sizeof ident, "burst-%d", i);
            // 正文必须逐条不同：dunst 会把「同标题 + 同正文」并成一叠并在角上写 (3)，
            // 四条连发在屏幕上就只剩两格 —— 那时截图说不出「四条各自到了」。
            char text[96]; snprintf(text, sizeof text, "取证合成的第 %d 条，不是真会话", i + 1);
            char why[512] = {0};
            int rc = ap_notify_post("Agent Ping 取证", text,
                                    "probe", ident, want_sound, why, (int)sizeof why);
            printf("  → %s｜rc=%d｜%s\n", ident, rc, why);
            if (rc == 0 || rc == 1) ok++;
        }
        printf("[连发] %d/%d 条真送出去了（rc0＝横幅投递，rc1＝兜底出声）\n", ok, burst_n);
        return ok == burst_n ? 0 : 1;
    }
    if (channel_) {
        char why[512] = {0};
        int rc = ap_notify_channel(why, (int)sizeof why);
        printf("[通道] %s｜%s\n", rc == 0 ? "可用" : rc == 1 ? "不可用" : "问不出来（没在时限内回执）", why);
        return rc == 0 ? 0 : (rc == 1 ? 1 : 2);
    }
    if (selftest_) return selftest();
    return live_round(post_);
}
