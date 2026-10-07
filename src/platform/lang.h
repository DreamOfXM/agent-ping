/* 平台层：这台机器的界面语言是面板支持的六语里的哪一语。
   core 只认这个 C 接口，不许 include 任何平台头（与 open.h / statusbar.h 同规矩）。 */
#ifndef AGENTPING_LANG_H
#define AGENTPING_LANG_H

#include <string.h>   // ap_lang_from_tag 是 inline 的，strcmp 得自己带上

#ifdef __cplusplus
extern "C" {
#endif

/* 下标顺序必须与 pin_imgui_shell.cpp 里 kStr 的六列完全一致（L0..L5）。 */
enum {
    AP_LANG_ZH = 0,
    AP_LANG_EN = 1,
    AP_LANG_DE = 2,
    AP_LANG_IT = 3,
    AP_LANG_JA = 4,
    AP_LANG_KO = 5
};

/* 一个语言标签（"zh-Hans-CN"、"de_DE.UTF-8"、"en"…）→ 六语下标；认不出返回 -1。
   只比第一子标签，所以 zh-Hant 也落回 zh：我们只有一份中文文案表（简体），
   繁体系统上界面念的还是那一份——这是**文案**的既成事实，不是字体能不能画繁体。
   （会话标题里的繁体字走的是另一条路：按 cmap 覆盖率逐 run 回退，与本函数无关。） */
static __inline int ap_lang_from_tag(const char* tag) {
    if (!tag) return -1;
    char prim[16];
    int i = 0;
    for (; tag[i] && i < 15; i++) {
        char c = tag[i];
        if (c == '-' || c == '_' || c == '.' || c == ' ') break;
        prim[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    prim[i] = '\0';
    if (!strcmp(prim, "zh")) return AP_LANG_ZH;
    if (!strcmp(prim, "en")) return AP_LANG_EN;
    if (!strcmp(prim, "de")) return AP_LANG_DE;
    if (!strcmp(prim, "it")) return AP_LANG_IT;
    if (!strcmp(prim, "ja")) return AP_LANG_JA;
    if (!strcmp(prim, "ko")) return AP_LANG_KO;
    return -1;
}

/* 返回面板用的下标（0..5）。src 写回「这个下标是从哪读来的、原文是什么」，
   调用方必须把它打出来：猜错语言是当场看得见的错，不该等他反馈才知道。
   读不到系统语言时返回 AP_LANG_EN 并在 src 里写明退回了英文。 */
int ap_lang(char* src, int out_len);

#ifdef __cplusplus
}
#endif
#endif
