#pragma once

/// 把一块窗口在「点击穿过它」与「接收点击」之间切换。win 是 GLFWwindow*——用 void*
/// 是为了调用方不必为了这一个开关把 GLFW 头塞进平台层。
///
/// 手柄舱是一条贴着右缘、整条工作区高的透明带：默认必须穿透，否则它把桌面挡下一条
/// 320pt 宽、上千 pt 高的竖条，指针看着在桌面上、点下去全给了舱。
///
/// 返回值说清生效没有，调用方必须把 -1 显示出来：这一档做不到时若静默继续，
/// 屏幕上留下的是「挡点击的透明带」，比没有舱更糟。
///   1 ＝已按下穿透（外面的 App 收到点击）
///   0 ＝已收回点击（舱自己收到）
///  -1 ＝这台机器做不到，窗口维持上一个状态
///
/// reason 写回走的是哪条路（"glfw 属性" / "x11 ShapeInput"）或做不到的原因。
/// 与 lang.h / motion.h 同规矩：「读不到」和「读到否」是两件事，不许合并。
int ap_set_mouse_passthrough(void* win, int on, char* reason, int out_len);

/// 这块窗口做得到穿透吗——只问，不改状态。1＝做得到，-1＝做不到（reason 写为什么）。
/// 灯舱在出现之前必须先过这一问：做不到穿透的平台上亮出来，等于在桌面上盖一条挡点击的
/// 透明竖条，比没有舱更糟。
int ap_mouse_passthrough_supported(void* win, char* reason, int out_len);

/// 这一档穿透能力从哪条路来，建窗时问一次，写进日志与取证探针。
/// 可能值："glfw>=3.4" / "x11 shape" / "none"。
const char* ap_passthrough_backend(void);
