#include "passthrough.h"

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <string.h>

// GLFW 3.4 才有 GLFW_MOUSE_PASSTHROUGH 这个窗口属性（3.3 里没有，Debian 12 停在 3.3.8）。
// 3.4 的 X11 后端自己就是靠 ShapeInput 实现的，所以低版本直接补同一条 X11 路：
// 输入形状置空＝服务器把指针事件发给下面的窗口，置回整窗＝自己收。
#if defined(AP_HAVE_X11)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#endif

static void say(char* reason, int out_len, const char* what) {
    if (reason && out_len > 0) snprintf(reason, (size_t)out_len, "%s", what);
}

const char* ap_passthrough_backend(void) {
#if defined(GLFW_MOUSE_PASSTHROUGH)
    return "glfw>=3.4";
#elif defined(AP_HAVE_X11)
    return "x11 shape";
#else
    return "none";
#endif
}

int ap_mouse_passthrough_supported(void* w, char* reason, int out_len) {
    GLFWwindow* win = (GLFWwindow*)w;
    if (!win) {
        say(reason, out_len, "没有窗口");
        return -1;
    }
#if defined(GLFW_MOUSE_PASSTHROUGH)
    say(reason, out_len, "glfw 属性");
    return 1;
#elif defined(AP_HAVE_X11)
    Display* dpy = glfwGetX11Display();
    if (!dpy) {
        say(reason, out_len, "拿不到 X 显示连接（这条会话不是 X11？）");
        return -1;
    }
    int evt_base = 0, err_base = 0;
    if (!XShapeQueryExtension(dpy, &evt_base, &err_base)) {
        say(reason, out_len, "这台 X 服务器没有 Shape 扩展");
        return -1;
    }
    say(reason, out_len, "x11 ShapeInput");
    return 1;
#else
    say(reason, out_len, "GLFW 低于 3.4 且没链 X11");
    return -1;
#endif
}

int ap_set_mouse_passthrough(void* w, int on, char* reason, int out_len) {
    GLFWwindow* win = (GLFWwindow*)w;
    if (!win) {
        say(reason, out_len, "没有窗口：切换失败");
        return -1;
    }
#if defined(GLFW_MOUSE_PASSTHROUGH)
    glfwSetWindowAttrib(win, GLFW_MOUSE_PASSTHROUGH, on ? GLFW_TRUE : GLFW_FALSE);
    say(reason, out_len, "glfw 属性");
    return on ? 1 : 0;
#elif defined(AP_HAVE_X11)
    Display* dpy = glfwGetX11Display();
    if (!dpy) {
        say(reason, out_len, "拿不到 X 显示连接：切换失败");
        return -1;
    }
    int evt_base = 0, err_base = 0;
    if (!XShapeQueryExtension(dpy, &evt_base, &err_base)) {
        say(reason, out_len, "这台 X 服务器没有 Shape 扩展：做不到");
        return -1;
    }
    const Window xw = glfwGetX11Window(win);
    if (on) {
        // 零个矩形 + Set ⇒ 输入形状为空集：整块窗口对指针不存在。
        XShapeCombineRectangles(dpy, xw, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    } else {
        int ww = 0, wh = 0;
        glfwGetWindowSize(win, &ww, &wh);
        XRectangle r;
        r.x = 0;
        r.y = 0;
        r.width = (unsigned short)(ww > 0 ? ww : 1);
        r.height = (unsigned short)(wh > 0 ? wh : 1);
        XShapeCombineRectangles(dpy, xw, ShapeInput, 0, 0, &r, 1, ShapeSet, Unsorted);
    }
    XSync(dpy, False);
    say(reason, out_len, "x11 ShapeInput");
    return on ? 1 : 0;
#else
    (void)win;
    (void)on;
    say(reason, out_len, "GLFW 低于 3.4 且没链 X11：做不到");
    return -1;
#endif
}
