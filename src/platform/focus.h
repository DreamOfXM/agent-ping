#pragma once

/// 应用从前台退下去（点了别的 App）时回调一次，在主线程上。
/// macOS 走 NSApplicationWillResignActive：点自己菜单栏那颗不算退前台，所以这条通知
/// 不会误伤「点图标收面板」那一击。X11 没有应用级通知，主循环另挂 GLFW 焦点回调。
/// 平台装不上这条通知时实现为空（不回调），调用方不许假设它一定响。
void ap_set_app_deactivate_cb(void (*cb)());
