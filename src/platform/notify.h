/* 平台层：把「有一条会话在等你」送到人眼前（系统横幅），并在横幅通道不通时兜底出声。
   core 只认这个 C 接口，不许 include 任何平台头（与 statusbar.h / open.h 同规矩）。

   为什么必须有兜底出声：本机实测这个签名（自签/临时签名）的横幅通道是死的 ——
   Swift 侧启动就报「通知授权失败：Notifications are not allowed for this application」。
   一个卖点写着「不用盯着也能被叫醒」的 App，如果横幅发不出就静默什么都不做，
   用户只能靠「没弹」来猜原因，而没弹的成因可能是权限、签名、免打扰、也可能是判据。 */
#ifndef AGENTPING_NOTIFY_H
#define AGENTPING_NOTIFY_H

#ifdef __cplusplus
extern "C" {
#endif

/* 通道真实状态，一条都不发、一声都不出。
   返回 0＝可用（横幅能投递）；1＝不可用；2＝问不出来（系统/守护进程没在时限内回执）。
   reason 写平台原话：授权档位、或「本机没有 notify-send」这种装不回去的事实。
   UTF-8，out_len 含结尾 0。 */
int ap_notify_channel(char* reason, int out_len);

/* 只出声不发横幅：设置屏打开提示音时的即时测试音。
   返回 0＝出了声；非 0＝没出成（reason 写明走的是哪条路、卡在哪）。 */
int ap_sound_test(char* reason, int out_len);

/* 发一条。agent_id / session_id 带进通知的 userInfo：点横幅要能跳回那一条会话，
   没有这两个键，点击下去只是消失。
   want_sound＝0 时任何一档都不许多出声（设置里提示音关着）。
   返回 0＝横幅已投递（此时不许再出声，两边都响就是吵人两遍）
        1＝横幅通道不通、已按 want_sound 兜底出声
        2＝两边都不通，reason 必填回是哪两边
        3＝系统/守护进程没在时限内回执：本轮当作没发过，调用方不许把它记进账本，
           下一轮重扫要再试（记了就等于把「本该吵你一次」静默丢掉）。 */
int ap_notify_post(const char* title, const char* body,
                   const char* agent_id, const char* session_id,
                   int want_sound, char* reason, int out_len);

/* 点横幅后的回调：平台侧把 userInfo 里的两个键交回 core（主线程上）。
   在第一次 ap_notify_post 前登记；传 NULL 表示不接（那点击就只是消失）。 */
typedef void (*ap_notify_click_fn)(const char* agent_id, const char* session_id, void* ud);
void ap_notify_on_click(ap_notify_click_fn fn, void* ud);

/* 这家厂商的桌面 App 现在是不是在前台（1＝是，0＝不是/查不到/没有桌面端）。
   判据：会话提醒要叫回「不在屏幕前」的人；他正盯着那家 App 时横幅只是噪音，
   而且那个 App 自己会把结果写在聊天里。只认前台 App 的 bundle id，不看窗口标题。 */
int ap_app_frontmost(const char* bundle_id);

#ifdef __cplusplus
}
#endif
#endif
