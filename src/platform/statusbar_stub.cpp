// 非 Apple 平台的占位实现：Linux 的 AppIndicator / Windows 的托盘各自另开文件，
// 在它们落地前 core 必须能照常跑，所以这里返回「没有状态项」而不是链接失败。
#include "statusbar.h"

extern "C" int sb_show(const char*, const char*, const char*, sb_click_fn, void*) { return 1; }
extern "C" void sb_set_label(const char*) {}
extern "C" void sb_pump(void) {}
extern "C" int sb_float_window(void) { return -1; }   // X11/Wayland 没有 borderless+alpha 阴影这套
