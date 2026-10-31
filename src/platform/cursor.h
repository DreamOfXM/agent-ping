#pragma once

/// 全局光标点，y 向下，与 glfwGetWindowPos / glfwSetWindowPos 同一坐标系。拿不到返回 false。
/// 拖动必须用它：io.MousePos 只在指针位于窗口内时才更新，按住表头快速往上拖，指针一出
/// 顶边就冻住，窗口跟着停摆再追（2026-10-30 用户原话「不能稳定随鼠标移动，还有点飘」）。
/// 全局点不掺窗口位置，算差值时没有反馈回路。
bool ap_global_cursor(double* x, double* y);

/// 主显示器的工作区（去掉菜单栏与 Dock），y 向下，与 glfwSetWindowPos 同一坐标系。
/// 手柄窗口按它贴右下角：用整屏坐标会在 Dock 底下露出半截，菜单栏也会压住顶上的卡。
/// 拿不到返回 false，调用方退回「整屏坐标」。
bool ap_work_area(double* x, double* y, double* w, double* h);

/// 鼠标左键此刻是否物理按住。手柄窗口在穿透态收不到 GLFW 的点击事件，
/// 只能从系统层读按键；与窗口焦点无关。
bool ap_mouse_down();
