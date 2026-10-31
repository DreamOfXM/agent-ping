#include "cursor.h"

// X11 侧要 XQueryPointer 才能拿全局点，而本目标不链 Xlib：这一档返回 false，
// 主循环退回「窗口原点＋视口内鼠标」的老算法（指针一出窗就会停摆）。
bool ap_global_cursor(double* x, double* y) {
    (void)x;
    (void)y;
    return false;
}

// 没有 Xlib 就量不到工作区；主循环退回整屏坐标（手柄仍贴右缘，只是不避 Dock）。
bool ap_work_area(double* x, double* y, double* w, double* h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return false;
}

// 这条只给「窗口穿透时认点击」用，X11 这一档手柄不走穿透（主循环退回窗口内鼠标）。
bool ap_mouse_down() { return false; }
