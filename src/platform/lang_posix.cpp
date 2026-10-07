// Linux 界面语言：按 glibc 查译文目录的优先级读环境变量 LC_ALL → LC_MESSAGES → LANG。
// 这是 XDG 桌面把语言传给你的进程的既有通道（GNOME/KDE 改语言后新起的进程就会变这几个值），
// 不需要引任何桌面库。
//
// 要说清的边界：这条通道在**没有会话总线/没走 PAM 的环境**（容器、ssh 不带 SendEnv、
// 直接 sh -c 起的进程）里通常是空的，那时本函数返回英文并在 src 里写「三个变量都没设」——
// 取不到与取到但不在六语里是两件事，分开报，别把「没读到」说成「这台机器是英文」。
#include <stdio.h>
#include <stdlib.h>

#include "lang.h"

extern "C" int ap_lang(char* src, int out_len) {
    static const char* kVars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* v : kVars) {
        const char* val = getenv(v);
        if (!val || !*val) continue;
        int idx = ap_lang_from_tag(val);
        snprintf(src, out_len, "env %s=\"%s\"%s", v, val, idx >= 0 ? "" : "（不在六语里）");
        return idx >= 0 ? idx : AP_LANG_EN;
    }
    snprintf(src, out_len, "LC_ALL/LC_MESSAGES/LANG 都没设");
    return AP_LANG_EN;
}
