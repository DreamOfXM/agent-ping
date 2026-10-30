#include "focus.h"

// X11 没有「应用退前台」这条通知；主循环用 GLFW 的窗口焦点回调顶（见 pin_imgui_shell.cpp）。
void ap_set_app_deactivate_cb(void (*cb)()) { (void)cb; }
