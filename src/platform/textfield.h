#pragma once

/// 搜索框的原生 NSTextField 桥。
///
/// 2026-10-28 那次为了绕搜狗组词挂死（IMKCFRunLoopWakeUpReliable），把 ImGui 视图的
/// inputContext 整个关掉了：ASCII 直进字符队列，中文只剩 ⌘V 一条路——搜索框打不了字。
/// 正解是给搜索框垫一颗真 NSTextField：输入法、候选窗、光标全部原生（搜狗在这台机器上
/// 对所有正常文本框都工作），文字变化经回调同步回面板自己的查询串。
///
/// 坐标是 ImGui 空间（内容视图左上原点，pt）；实现侧负责翻转与窗口定位。
/// show/hide 由「焦点变化」驱动，frame 每帧跟随（面板会拖动）。
/// callback 指针在 main 起点装一次；不装则字段不回落任何行为。

#ifdef __APPLE__
void ap_field_show(void* glfw_window, float x, float y, float w, float h,
                   const char* placeholder, const char* text, int tag);
void ap_field_hide(void);
void ap_field_frame(void* glfw_window, float x, float y, float w, float h);
void ap_field_set_text(const char* text);
bool ap_field_active(void);
void ap_field_set_change_cb(void (*cb)(const char* text, int tag));
void ap_field_set_done_cb(void (*cb)(bool commit, int tag));
#else
// 非 Apple 平台没有这一档（Linux 搜索框继续走字符队列直收）。
inline void ap_field_hide(void) {}
inline void ap_field_frame(void*, float, float, float, float) {}
inline void ap_field_set_text(const char*) {}
inline bool ap_field_active(void) { return false; }
inline void ap_field_set_change_cb(void (*)(const char*, int)) {}
inline void ap_field_set_done_cb(void (*)(bool, int)) {}
#endif
