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

/* ZCode 专用：会话级落点。ZCode 没有会话级 URL（10-29 在 app.asar 里逐条核过，
   只认领 oauth/callback 与 workspace/open?path=），能落到「那一条对话」的只有它自己的界面：
   ① 先读侧栏「任务列表」的行（Chromium 要先置 AXManualAccessibility 才建这棵树），
      按标题匹配后 AXPress 那一行，再拿主区标题当裁判验收；
   ② App 在跑但一个窗口都没有（10-29 实测到）：发一次 LaunchServices 的 reopen
      （openURL 那个 .app 本身，与点 Dock 图标同一件事），实测 3 秒内把原来那一扇按原几何
      恢复出来，不弹框、不多开窗。这一支以「原生 AXWindows 数到 0」为门——那个不依赖
      Chromium 的树，秒读；不许拿「读不到界面」当门：窗口在、树没建起来时照样会多开一扇。
      10-29 撤掉 zcode://workspace/open?path= 这条：它开的是「用外部 URL 打开这个文件夹」，
      ZCode 会先弹一枚模态「是否在 ZCode 中打开此文件夹？」，界面树从此卡在框后面，
      而替他自己按「打开文件夹」是替用户做一个 App 故意要问的安全决定。
   命令中心（⌘K）不是第二把钥匙：它是当前工作区作用域，实测 0/10 且对侧栏明明有的标题
   回「暂无相关结果」（见 open_macos.mm 那段），10-29 撤掉，侧栏没有就按没这一条报。
   需要辅助功能授权。返回：0 已点中且主区换成了这一条 / 1 没成（唤不起、侧栏没这一条、
   按了主区没换、窗口要不回来）/ 2 未授权辅助功能 / 3 ZCode 没在跑 /
   4 本平台没有这一档（不适用，调用方当没这条路，不许报「侧栏没这条」）/
   5 窗口已经让它重开了，但界面还要几秒才长出来（10-29 实测），这一跳不等它——
   调用方要写「过几秒再点一次就能落到这一条」，不许报成成功，也不许混进「侧栏没这条」。
   6 这一条建得比 ZCode 本次启动晚，它这次的侧栏列表里压根没有它（10-29 实测：本次启动后
   新建的 15 条零命中，启动前建的在列）——这一档再怎么翻页都不会命中，调用方要单独说一句，
   不许混进「客户端没点中」（那句的下文是「对着标题在列表里找」，这里找了也找不到）。
   调用方给三条线索：title 用来认行，cwd 末级＝侧栏里的项目分组名（用来只翻「显示更多」
   藏在后面的那一组，别的组一枚都不碰——翻页按钮只按「装着这一组行、且直接子节点只有两枚」
   的那一节里恰好一枚的那颗），created＝这条会话的建立时刻（用来分清下面 6 与 1）。
   非 0 时 reason 写明挡在哪一层，调用方按那一层决定退不退 ap_open3。 */
int ap_zcode_navigate(const char* title, const char* cwd, double created, char* reason, int out_len);

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
