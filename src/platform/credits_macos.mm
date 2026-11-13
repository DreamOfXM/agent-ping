#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>

#include "platform/credits.h"

// 千问办公「积分余额」的 AX 读取（动机见 credits.h 头注释）。
// Electron 的无障碍树默认不建：每次都置 AXManualAccessibility——App 已带树时是幂等空操作，
// 没带时这才建得起来。树里 static text 可能为 0（用户停在无侧栏的页面/树还没长好）：
// 如实返回 false，让卡面走「上次读取」的陈旧口径，不编数。

namespace {

/// 深度优先收 AXStaticText 的 value；上限一扇窗 400 条、最多 3 扇窗，防大树拖死扫描。
void WalkTexts(AXUIElementRef el, NSMutableArray<NSString*>* texts, int* budget) {
    if (*budget <= 0 || !el) return;
    CFTypeRef role = nullptr;
    if (AXUIElementCopyAttributeValue(el, kAXRoleAttribute, (CFTypeRef*)&role) == kAXErrorSuccess) {
        NSString* roleNs = (__bridge NSString*)role;
        if ([roleNs isEqualToString:@"AXStaticText"]) {
            CFTypeRef val = nullptr;
            if (AXUIElementCopyAttributeValue(el, kAXValueAttribute, (CFTypeRef*)&val) == kAXErrorSuccess) {
                NSString* s = (__bridge NSString*)val;
                if (s.length) [texts addObject:s];
                if (val) CFRelease(val);
            }
            if (role) CFRelease(role);
            (*budget)--;
            return;   // 静态文本没有子节点
        }
        if (role) CFRelease(role);
    }
    CFTypeRef kids = nullptr;
    if (AXUIElementCopyAttributeValue(el, kAXChildrenAttribute, (CFTypeRef*)&kids) == kAXErrorSuccess) {
        NSArray* arr = (__bridge NSArray*)kids;
        for (id k in arr) {
            if (*budget <= 0) break;
            WalkTexts((__bridge AXUIElementRef)k, texts, budget);
        }
        if (kids) CFRelease(kids);
    }
}

}  // namespace

bool ap_ax_texts(const char* proc_name, const char* bundle_id, char* out, int outlen,
                 int max_texts) {
    if (out && outlen > 0) out[0] = 0;
    NSArray<NSRunningApplication*>* apps =
        bundle_id ? [NSRunningApplication runningApplicationsWithBundleIdentifier:@(bundle_id)] : @[];
    NSRunningApplication* app = apps.firstObject;
    if (!app && proc_name) {
        for (NSRunningApplication* ra in [[NSWorkspace sharedWorkspace] runningApplications])
            if ([ra.localizedName isEqualToString:@(proc_name)]) { app = ra; break; }
    }
    if (!app) return false;
    AXUIElementRef ax = AXUIElementCreateApplication(app.processIdentifier);
    if (!ax) return false;
    AXUIElementSetAttributeValue(ax, (__bridge CFStringRef)@"AXManualAccessibility",
                                 kCFBooleanTrue);
    CFTypeRef wins = nullptr;
    bool any = false;
    if (AXUIElementCopyAttributeValue(ax, kAXWindowsAttribute, (CFTypeRef*)&wins) == kAXErrorSuccess) {
        NSMutableArray<NSString*>* texts = [NSMutableArray array];
        int budget = max_texts > 0 && max_texts <= 512 ? max_texts : 512;
        int windows = 0;
        for (id w in (__bridge NSArray*)wins) {
            if (windows >= 3) break;
            WalkTexts((__bridge AXUIElementRef)w, texts, &budget);
            windows++;
        }
        NSMutableString* joined = [NSMutableString string];
        for (NSString* s in texts) {
            if (joined.length) [joined appendString:@"|"];
            [joined appendString:s];
        }
        if (joined.length > 0 && out && outlen > (int)joined.length + 1) {
            strncpy(out, joined.UTF8String, (size_t)outlen - 1);
            out[outlen - 1] = 0;
            any = true;
        }
        if (wins) CFRelease(wins);
    }
    CFRelease(ax);
    return any;
}
