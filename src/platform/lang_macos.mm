// macOS 界面语言：读 NSLocale 的用户偏好列表，第一条就是「系统设置 → 通用 → 语言」
// 选中的那一语（App 单独设过 AppleLanguages 时以 App 的为准，同样走这条）。
//
// 为什么不用 getenv("LANG")：从 Finder 双击起来的 .app 不继承终端环境，实测这类进程里
// LANG 常常是空的或 "C"，拿它判语言会在中文系统上退成英文——那是「检测失败被当成
// 检测结果」，比写死一种语言更难查。所以读不到就明说读不到，不去猜。
#import <Foundation/Foundation.h>

#include <stdio.h>

#include "lang.h"

extern "C" int ap_lang(char* src, int out_len) {
    NSString* tag = nil;
    @autoreleasepool {
        NSArray<NSString*>* pref = [NSLocale preferredLanguages];
        if (pref.count > 0) tag = [pref[0] copy];
    }
    if (tag.length == 0) {   // 偏好列表为空（极少见）：这一条本身就是要看得见的事实
        snprintf(src, out_len, "NSLocale.preferredLanguages 空");
        return AP_LANG_EN;
    }
    int idx = ap_lang_from_tag(tag.UTF8String);
    snprintf(src, out_len, "NSLocale.preferredLanguages[0]=\"%s\"%s", tag.UTF8String,
             idx >= 0 ? "" : "（不在六语里）");
    return idx >= 0 ? idx : AP_LANG_EN;
}
