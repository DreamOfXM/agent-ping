#include "cursor.h"

// X11 侧要 XQueryPointer 才能拿全局点，而本目标不链 Xlib：这一档返回 false，
// 主循环退回「窗口原点＋视口内鼠标」的老算法（指针一出窗就会停摆）。
bool ap_global_cursor(double* x, double* y) {
    (void)x;
    (void)y;
    return false;
}
