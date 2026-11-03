#include "cursor.h"

// X11 侧三件事（全局点、工作区、物理按键）都只能从 X 服务器读：GLFW 的 io.MousePos
// 只在指针位于本窗口内时才更新，而手柄舱要的正是「指针已经离开这条带」那一刻。
// 这一档要链 Xlib（CMake 查到 libX11 才定义 AP_HAVE_X11）；查不到就退回下面的 false，
// 并且调用方会明说「读不到」，不把「读不到」当成「指针不在那儿」。
#if defined(AP_HAVE_X11)

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <cstring>

// 主屏的 root 窗口。多屏（每个 screen 一个 root）不是本项目测过的形状：
// 拿 DefaultScreen，量到的是它自己的绝对坐标原点，与 glfwSetWindowPos 同一套。
static Display* xdisp() { return glfwGetX11Display(); }

bool ap_global_cursor(double* x, double* y) {
    Display* dpy = xdisp();
    if (!dpy || !x || !y) return false;
    Window root_ret = 0, child_ret = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret,
                       &rx, &ry, &wx, &wy, &mask))
        return false;                       // 指针不在这个 screen 上
    *x = rx;
    *y = ry;
    return true;
}

// 没有 WM 就读不到 _NET_WORKAREA；主循环退回整屏坐标（手柄仍贴右缘，只是不避 Dock）。
bool ap_work_area(double* x, double* y, double* w, double* h) {
    Display* dpy = xdisp();
    if (!dpy || !x || !y || !w || !h) return false;
    const Window root = DefaultRootWindow(dpy);
    Atom net_workarea = XInternAtom(dpy, "_NET_WORKAREA", False);
    Atom cardinal = XInternAtom(dpy, "CARDINAL", False);
    Atom actual = None;
    int fmt = 0;
    unsigned long n_items = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, root, net_workarea, 0, 4, False, cardinal,
                           &actual, &fmt, &n_items, &after, &data) != Success ||
        !data || actual != cardinal || fmt != 32 || n_items < 4) {
        if (data) XFree(data);
        return false;
    }
    const long* v = (const long*)data;
    const int screen = DefaultScreen(dpy);
    if (n_items < (unsigned long)(screen * 4 + 4)) { XFree(data); return false; }
    *x = v[screen * 4 + 0];
    *y = v[screen * 4 + 1];
    *w = v[screen * 4 + 2];
    *h = v[screen * 4 + 3];
    XFree(data);
    return *w > 0 && *h > 0;
}

// XQueryPointer 的 mask 带的是这一刻物理按住的键，与窗口焦点、与穿透态无关：
// 手柄穿透着收不到 ButtonPress，只能这样问系统。左键按没按就是 Button1Mask。
bool ap_mouse_down() {
    Display* dpy = xdisp();
    if (!dpy) return false;
    Window root_ret = 0, child_ret = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret,
                       &rx, &ry, &wx, &wy, &mask))
        return false;
    return (mask & Button1Mask) != 0;
}

#else   // 没有 Xlib：这三档一律返回 false，调用方按「读不到」处理并明说。

bool ap_global_cursor(double* x, double* y) {
    (void)x;
    (void)y;
    return false;
}

bool ap_work_area(double* x, double* y, double* w, double* h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return false;
}

bool ap_mouse_down() { return false; }

#endif
