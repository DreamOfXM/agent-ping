// macOS「减弱动效」：系统设置 → 辅助功能 → 显示 → Reduce motion。
//
// 这一格在 macOS 上没有公开 API，实测核对过（2026-11-03，本机 SDK 13+）：
//   · AppKit 全套头文件里 grep "ReduceMotion" 零命中——iOS 上那个
//     NSWorkspace.accessibilityDisplayShouldReduceMotion 在 macOS 根本不存在；
//   · SwiftUI 的 accessibilityReduceMotion 在 macos.swiftinterface 里也没有，
//     它只覆盖 iOS/iPadOS/visionOS。
// 公开符号只有 HIServices 里那三个下划线开头的（_AXInterfaceGetReduceMotionEnabled 等），
// 那是别人的私有实现，不链。这里改读苹果写进偏好域的那个值本身——
// 用户的无障碍 plist，只读不写、也不替谁翻这一格。
//
// 读法用 CFPreferences 而不是 fopen 那个 plist：cfprefsd 缓存了它，直接读文件可能拿到
// 系统还没落盘的旧值，而「用户明明开了我们没看见」正是这一层最不该犯的错。
#import <CoreFoundation/CoreFoundation.h>

#include <stdio.h>

#include "motion.h"

extern "C" int ap_reduce_motion(char* src, int out_len) {
    const CFStringRef key = CFSTR("ReduceMotionEnabled");
    const CFStringRef app = CFSTR("com.apple.Accessibility");
    CFPropertyListRef value = CFPreferencesCopyAppValue(key, app);
    if (!value) {
        snprintf(src, out_len, "com.apple.Accessibility/ReduceMotionEnabled 读不到（未设）");
        return 0;
    }
    int on = 0;
    if (CFGetTypeID(value) == CFBooleanGetTypeID())
        on = CFBooleanGetValue((CFBooleanRef)value) ? 1 : 0;
    else if (CFGetTypeID(value) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &on);
    else
        on = 0;
    snprintf(src, out_len, "CFPreferences com.apple.Accessibility/ReduceMotionEnabled=%d", on);
    CFRelease(value);
    return on;
}
