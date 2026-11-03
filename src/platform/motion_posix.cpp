// Linux 侧没有一个跨桌面的「减弱动效」总开关，能查到的最接近的是 GTK 的
// gtk-enable-animations=false——GNOME/KDE 上的 GTK 应用都认它，用户在国际支付/辅助功能
// 那类语境里关掉动画，落的就是这一行。按 glib 自己的顺序读 gtk-4.0 再 gtk-3.0 的 settings.ini。
//
// 边界要说清：Qt 原生桌面（KDE 自己的 Plasma 动画、Xfce、窗口管理器）不写这两个文件，
// 读不到时 src 里明写「读不到」，调用方照常放动画。把「这台机器查不到」报成
// 「这位用户没要求」是假通过——跟 lang_posix.cpp 同一个规矩。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "motion.h"

// 一个 ini 文件里找 key，命中返回 1 并把值（去空白、转小写）写进 val；没命中返回 0。
static int ini_lookup(const char* path, const char* key, char* val, int val_len) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    const size_t klen = strlen(key);
    int hit = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '[' || strncmp(p, key, klen) != 0) continue;
        p += klen;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        size_t n = 0;
        while (p[n] && p[n] != '\n' && p[n] != '\r' && p[n] != ' ' && p[n] != '\t' &&
               (int)n < val_len - 1) n++;
        if (n == 0) break;
        for (size_t i = 0; i < n; i++)
            val[i] = (p[i] >= 'A' && p[i] <= 'Z') ? (char)(p[i] + 32) : p[i];
        val[n] = '\0';
        hit = 1;
        break;
    }
    fclose(f);
    return hit;
}

extern "C" int ap_reduce_motion(char* src, int out_len) {
    const char* home = getenv("HOME");
    if (!home || !*home) {
        snprintf(src, out_len, "HOME 没设，读不到 gtk settings.ini");
        return 0;
    }
    static const char* kDirs[] = {"gtk-4.0", "gtk-3.0"};
    for (const char* d : kDirs) {
        char path[512];
        snprintf(path, sizeof path, "%s/.config/%s/settings.ini", home, d);
        char val[32] = "";
        if (!ini_lookup(path, "gtk-enable-animations", val, sizeof val)) continue;
        const int off = (!strcmp(val, "false") || !strcmp(val, "0") ||
                         !strcmp(val, "no") || !strcmp(val, "off"));
        snprintf(src, out_len, "%s: gtk-enable-animations=%s", d, val);
        return off ? 1 : 0;   // 认得的值按用户写的办；认不出的值（拼错了）也照样报出来，别当读不到
    }
    snprintf(src, out_len, "gtk-4.0/gtk-3.0 的 settings.ini 里没有 gtk-enable-animations");
    return 0;
}
