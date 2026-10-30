#pragma once

/// 全局光标点，y 向下，与 glfwGetWindowPos / glfwSetWindowPos 同一坐标系。拿不到返回 false。
/// 拖动必须用它：io.MousePos 只在指针位于窗口内时才更新，按住表头快速往上拖，指针一出
/// 顶边就冻住，窗口跟着停摆再追（2026-10-30 用户原话「不能稳定随鼠标移动，还有点飘」）。
/// 全局点不掺窗口位置，算差值时没有反馈回路。
bool ap_global_cursor(double* x, double* y);
