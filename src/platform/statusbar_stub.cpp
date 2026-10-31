// 非 Apple 平台的占位实现：Linux 的指示器在 statusbar_sni.cpp（要 libdbus），
// 没装 libdbus 开发包时就退到这里。core 必须能照常跑，所以这里返回「没有状态项」
// 而不是链接失败——返回非 0 会让调用方打一行「本平台还没有实现」。
#include "statusbar.h"

extern "C" int sb_show(const char*, const char*, const char*, const char*, sb_click_fn, void*) {
    return 1;
}
extern "C" void sb_set_label(const char*) {}
extern "C" void sb_set_tooltip(const char*) {}
extern "C" int sb_capture(const char*) { return 1; }   // 没有状态项，也就没有可拍的按钮
extern "C" void sb_pump(void) {}
extern "C" int sb_float_window(void) { return -1; }   // X11/Wayland 没有 borderless+alpha 阴影这套
extern "C" int sb_hit_test(double, double) { return -1; }   // 没有可穿透的浮层，这一档问不出来
