/* 平台层：把一条会话「送到人能回答它的地方」。
   core 只认这个 C 接口，不许 include 任何平台头（与 statusbar.h 同规矩）。 */
#ifndef AGENTPING_OPEN_H
#define AGENTPING_OPEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* 落点逐级退：①这个 App 自己认领的 deep link（能精确到那一条对话）
   ②本机装了它的 .app → 唤起（在跑就直接 activate，不在跑才冷启动）
   ③只有「本机根本没有客户端」的那几路（claude/codex 这类纯 CLI）才退到打开会话记录目录。
      装了客户端却没跳进去＝返回非 0 并写清卡在哪，不许拿「开了个文件夹」冒充跳转
      （2026-10-29 用户就是被这一条气到的）。Linux 是唯一例外：那边根本没有 ②，
      拦住 ③ 只剩「点了什么也没发生」，所以照样落目录，但 layer=3 要如实报出去。
   三层全落空同样返回非 0，reason 写「无处可去」，调用方必须把这句打出来，不许静默。
   reason 一律 UTF-8，out_len 是缓冲区大小（含结尾 0）。 */
int ap_open(const char* agent_id, const char* session_id, char* reason, int out_len);

/* ap_open 的加 cwd 版。cwd 目前不参与落点：ZCode 的工作区 URL（zcode://workspace/open）
   实测会「新开一个任务」而不是定位已有会话，已撤（过程见 open_macos.mm ①′ 的注释）；
   参数留着，是因为会话级落点一旦有了（App 开放 deep link / ⌘K 驱动落地）就该用它。 */
int ap_open2(const char* agent_id, const char* session_id, const char* cwd, char* reason,
             int out_len);

/* ap_open2 的加 layer 版。layer 回填「这一跳真落到了哪一层」，面板据此把回执说准：
   1＝deep link 到那条对话 / 2＝只把 App 唤到前台（没到具体会话）/
   3＝打开了记录目录（这一路根本没有客户端）/ 0＝一层都没落上。
   layer 传 NULL 就是 ap_open2。 */
int ap_open3(const char* agent_id, const char* session_id, const char* cwd, char* reason,
             int out_len, int* layer);

/* 这一路在本机有没有「可跳的客户端」（macOS 上是 .app，Linux 上是桌面程序）。
   面板据此选文案与预期：有客户端的才敢说「打开客户端里的这条会话」，
   纯 CLI（claude/codex）只能承诺「打开记录目录」。返回 1 有 / 0 没有（表里没有也算没有）。
   读的是 ap_open3 同一张落点表，两处不许各判各的。 */
int ap_has_client(const char* agent_id);

/* ZCode 专用：激活 App 后替用户敲 ⌘K → 粘贴 title → 回车，让 Command Center
   定位到那条会话（ZCode 没有会话级 URL，这是唯一通路，Swift 版 KeyDriver 同款）。
   需要辅助功能授权。返回：0 已敲进并回车 / 1 没成（唤不起、检索没命中）/
   2 未授权辅助功能 / 4 本平台没有这一档（不适用，调用方当没这条路，不许报「没搜到」）。
   非 0 时 reason 写明挡在哪一层，调用方退回 ap_open3。 */
int ap_zcode_navigate(const char* title, char* reason, int out_len);

/* Qoder 专用：读它客户端托盘里的会话条目（「会话标题，工作区名」），按名字点中那一条。
   qoder:// 没有会话级路由（2026-10-29 在 app.asar 里逐条核过），托盘是唯一能落到
   「那一条对话」的入口，因此需要辅助功能授权。
   返回：0 已点中这一条 / 1 托盘这一轮没列它（每组只露 3 条，共 12 条）/
   2 未授权辅助功能 / 3 Qoder 没在跑 / 4 本平台没有这一档（Linux：调用方当没这条路，
   不许报「托盘没列」）。非 0 时 reason 写清挡在哪一层，调用方
   必须把这句摆到屏幕上，不许退化成「打开记录目录」冒充成功。 */
int ap_qoder_tray(const char* title, char* reason, int out_len);

/* 辅助功能这一档现在有没有。返回 0 已授权 / 1 未授权；reason 写成人话。
   prompt!=0 时，未授权会发起系统那次问询（弹「XXX 想控制这台电脑」并把本 App
   列进辅助功能表）。Linux 侧没有这一档：返回 4（不适用），面板据此整格不画，
   不许显示成「未授权」——那是在催他去拨一个这儿不存在的开关。 */
int ap_ax_trusted(int prompt, char* reason, int out_len);

/* 一步跳到「系统设置 → 隐私与安全性 → 辅助功能」，让他在那儿把开关拨上。
   打开失败也要在 reason 里给出他自己能走的那条路径。 */
int ap_open_privacy_pane(char* reason, int out_len);

/* 只回答「这一点下去会落到哪」，什么都不真的打开（取证用）。
   与 ap_open 共用同一段判据，探针和实点开的是同一个口径。 */
int ap_open_plan(const char* agent_id, const char* session_id, char* reason, int out_len);

#ifdef __cplusplus
}
#endif
#endif
