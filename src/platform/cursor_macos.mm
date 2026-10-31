#include <CoreGraphics/CoreGraphics.h>
#import <Cocoa/Cocoa.h>

#include "cursor.h"

bool ap_global_cursor(double* x, double* y) {
    CGEventRef e = CGEventCreate(nullptr);
    if (!e) return false;
    const CGPoint p = CGEventGetLocation(e);
    CFRelease(e);
    // CGEventGetLocation 本来就是左上原点、y 向下，和 GLFW 的窗口坐标同向，直接给。
    // 先前按 Cocoa 底左原点翻过一次，竖着拖窗口会朝反方向走（2026-10-31 合成拖拽实测）。
    *x = p.x;
    *y = p.y;
    return true;
}

bool ap_work_area(double* x, double* y, double* w, double* h) {
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (screens.count == 0) return false;
    // GLFW 的主屏（坐标原点那块）就是数组第一块；visibleFrame 已去掉菜单栏与 Dock。
    NSScreen* s = screens.firstObject;
    const NSRect full = s.frame, vis = s.visibleFrame;
    *x = vis.origin.x;
    *y = full.size.height - (vis.origin.y + vis.size.height);   // Cocoa 底左 → GLFW 顶左
    *w = vis.size.width;
    *h = vis.size.height;
    return *w > 1.0 && *h > 1.0;
}

bool ap_mouse_down() {
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
}
