#include <CoreGraphics/CoreGraphics.h>

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
