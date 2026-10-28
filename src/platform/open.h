/* 平台层：把一条会话「送到人能回答它的地方」。
   core 只认这个 C 接口，不许 include 任何平台头（与 statusbar.h 同规矩）。 */
#ifndef AGENTPING_OPEN_H
#define AGENTPING_OPEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* 落点逐级退：①这个 App 自己认领的 deep link（能精确到那一条对话）
   ②本机装了它的 .app → 唤起（在跑就直接 activate，不在跑才冷启动）
   ③都没有 → 在文件管理器里打开它的会话记录目录。
   三层全落空返回非 0，reason 写「无处可去」，调用方必须把这句打出来，不许静默。
   reason 一律 UTF-8，out_len 是缓冲区大小（含结尾 0）。 */
int ap_open(const char* agent_id, const char* session_id, char* reason, int out_len);

/* ap_open 的加 cwd 版。cwd 目前不参与落点：ZCode 的工作区 URL（zcode://workspace/open）
   实测会「新开一个任务」而不是定位已有会话，已撤（过程见 open_macos.mm ①′ 的注释）；
   参数留着，是因为会话级落点一旦有了（App 开放 deep link / ⌘K 驱动落地）就该用它。 */
int ap_open2(const char* agent_id, const char* session_id, const char* cwd, char* reason,
             int out_len);

/* ZCode 专用：激活 App 后替用户敲 ⌘K → 粘贴 title → 回车，让 Command Center
   定位到那条会话（ZCode 没有会话级 URL，这是唯一通路，Swift 版 KeyDriver 同款）。
   需要辅助功能授权；未授权/未命中返回非 0，reason 写明原因，调用方退回 ap_open2。 */
int ap_zcode_navigate(const char* title, char* reason, int out_len);

/* 只回答「这一点下去会落到哪」，什么都不真的打开（取证用）。
   与 ap_open 共用同一段判据，探针和实点开的是同一个口径。 */
int ap_open_plan(const char* agent_id, const char* session_id, char* reason, int out_len);

#ifdef __cplusplus
}
#endif
#endif
