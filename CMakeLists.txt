cmake_minimum_required(VERSION 3.20)
project(agent_ping_cpp VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

find_library(GLFW_LIB glfw HINTS /opt/homebrew/lib /usr/local/lib REQUIRED)
find_path(GLFW_INC GLFW/glfw3.h HINTS /opt/homebrew/include /usr/local/include REQUIRED)
find_library(FREETYPE_LIB freetype HINTS /opt/homebrew/lib /usr/local/lib /usr/lib REQUIRED)
# freetype 2 的头文件不在 include/ 根下，而在 include/freetype2/ 里；两个 brew 前缀都要试
# （Apple 芯片是 /opt/homebrew，Intel 是 /usr/local），否则 Intel 上找不到 ft2build.h。
find_path(FREETYPE_INC ft2build.h
  HINTS /opt/homebrew/opt/freetype/include /usr/local/opt/freetype/include
        /opt/homebrew/include /usr/local/include /usr/include
  PATH_SUFFIXES freetype2
  REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# macOS：glfw / freetype 一律静态链进来。
#
# 动态链会在产物里留下 /usr/local/opt/glfw/lib/libglfw.3.dylib、
# /usr/local/opt/freetype/lib/libfreetype.6.dylib 这种**本机绝对路径**（Apple 芯片是
# /opt/homebrew/...）。这样打出来的 .app 只在这台机器上活得下去：换一台没装 glfw 的 Mac，
# 双击就是 dyld: Library not loaded，界面根本不出现——用户看到的现象就是「装了个寂寞」。
# 而让人为了一个 .app 去现编 glfw 更不现实：Homebrew 自 2026-09 起不再为 macOS Intel
# 出 bottle，Intel 上 brew install glfw 是现场编译。
#
# 所以只要找得到 .a 就静态链，.app 自成一体，不欠外部依赖。找不到才退回动态并明确警告。
# ─────────────────────────────────────────────────────────────────────────────
if(APPLE)
  set(_ap_lib_hints /opt/homebrew/lib /usr/local/lib)
  find_file(AP_GLFW_A NAMES libglfw3.a HINTS ${_ap_lib_hints})
  find_file(AP_FREETYPE_A NAMES libfreetype.a HINTS ${_ap_lib_hints})
  if(AP_GLFW_A AND AP_FREETYPE_A)
    # freetype 的 .a 里带着 png / brotli 的引用，静态链必须把它们一起补上，顺序也要对；
    # z / bz2 用系统自带的（macOS 一直随系统提供）。
    find_file(AP_PNG_A NAMES libpng16.a libpng.a HINTS ${_ap_lib_hints})
    find_file(AP_BROTLIDEC_A NAMES libbrotlidec.a HINTS ${_ap_lib_hints})
    find_file(AP_BROTLICOMMON_A NAMES libbrotlicommon.a HINTS ${_ap_lib_hints})
    find_library(AP_Z_LIB z)
    find_library(AP_BZ2_LIB bz2)
    # 新版 freetype 的 hvf.o 引用 libhvf（macOS 系统库，SDK 里只有 .tbd 存根，
    # 运行期在 dyld 共享缓存里），静态链必须把它补上，否则一堆 _HVF_* 未定义符号。
    find_library(AP_HVF_LIB hvf)
    set(GLFW_LIB ${AP_GLFW_A})
    set(FREETYPE_LIB ${AP_FREETYPE_A} ${AP_PNG_A} ${AP_BROTLIDEC_A} ${AP_BROTLICOMMON_A}
                     ${AP_Z_LIB} ${AP_BZ2_LIB} ${AP_HVF_LIB})
    message(STATUS "macOS 依赖：静态链接 glfw / freetype，产物不依赖 Homebrew 路径")
  else()
    message(WARNING
      "没找到 libglfw3.a / libfreetype.a（brew install glfw freetype 会同时装出 .a），"
      "退回动态链接：这样打出的 .app 只能跑在装了 glfw/freetype 的机器上，分发前务必确认")
  endif()
endif()

# sqlite3 只服务 ZCode / WorkBuddy 两路；找不到就退化成「只读 JSONL」，
# 而不是整个编译断掉 —— 采集层用 AP_HAVE_SQLITE 把这两路圈起来了。
find_library(SQLITE3_LIB sqlite3)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)
set(IMGUI_SRC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

if(APPLE)
  find_library(COCOA_LIB Cocoa REQUIRED)
  find_library(IOKIT_LIB IOKit REQUIRED)
  find_library(OPENGL_LIB OpenGL REQUIRED)
  find_library(COREGRAPHICS_LIB CoreGraphics REQUIRED)
  set(STATUSBAR_SRC src/platform/statusbar_macos.mm)
  set_source_files_properties(${STATUSBAR_SRC} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set(OPEN_SRC src/platform/open_macos.mm)
  set_source_files_properties(${OPEN_SRC} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set(NOTIFY_SRC src/platform/notify_macos.mm)
  set_source_files_properties(${NOTIFY_SRC} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set(LANG_SRC src/platform/lang_macos.mm)
  set_source_files_properties(${LANG_SRC} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set(AUTOSTART_SRC src/platform/autostart_macos.mm)
  set_source_files_properties(${AUTOSTART_SRC} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set(FOCUS_SRC src/platform/focus_macos.mm)
  set_source_files_properties(${FOCUS_SRC} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set(CURSOR_SRC src/platform/cursor_macos.mm)
  find_library(USERNOTIFICATIONS_LIB UserNotifications REQUIRED)
  # 静态链 glfw 时 cocoa_window.m 会引用 CAMetalLayer，必须自己带上 QuartzCore
  # （动态链时是 libglfw.3.dylib 的 LC_LOAD_DYLIB 替你带进来的，静态链没人管）。
  find_library(QUARTZCORE_LIB QuartzCore REQUIRED)
  set(PLATFORM_LIBS ${COCOA_LIB} ${IOKIT_LIB} ${OPENGL_LIB} ${COREGRAPHICS_LIB}
                    ${USERNOTIFICATIONS_LIB} ${QUARTZCORE_LIB})
else()
  # Debian 12 (bookworm) aarch64 实测零 error 编过，依赖就这四个包：
  #   apt-get install -y cmake ninja-build libglfw3-dev libfreetype6-dev libsqlite3-dev libgl1-mesa-dev
  # 出图/跑 GUI 用 xvfb-run -a -s "-screen 0 1400x1100x24"（Mesa llvmpipe 软渲染即可，1.0x 栅格）。
  # 提醒层另需两个包才有真行为：libnotify-bin（横幅）、sound-theme-freedesktop + pipewire-utils
  #   或 alsa-utils（出声）—— 少哪一个，ap_notify_post 都会把「哪一边不通」写回 reason。
  # 状态项走 StatusNotifierItem（KDE/GNOME/XFCE/LXQt 共用的 D-Bus 协议），只要 libdbus-1，
  #   不为了一颗托盘图标把 GTK3 拉进依赖：apt-get install -y libdbus-1-dev
  # 模块名是 dbus-1（包里就这一个 dbus-1.pc，实测 1.14.10）；写成 libdbus-1 会查不到而退化。
  # 装了就用真实现，没装退到占位实现并且**明说**——静默退化成「没有状态项」是假通过。
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBDBUS QUIET dbus-1)
  endif()
  find_library(OPENGL_LIB GL REQUIRED)
  set(OPEN_SRC src/platform/open_posix.cpp)
  set(NOTIFY_SRC src/platform/notify_posix.cpp)
  set(LANG_SRC src/platform/lang_posix.cpp)
  set(AUTOSTART_SRC src/platform/autostart_posix.cpp)
  set(FOCUS_SRC src/platform/focus_posix.cpp)
  set(CURSOR_SRC src/platform/cursor_posix.cpp)
  set(PLATFORM_LIBS ${OPENGL_LIB})
  if(LIBDBUS_FOUND)
    set(STATUSBAR_SRC src/platform/statusbar_sni.cpp)
    message(STATUS "状态项：SNI 实现（libdbus-1 ${LIBDBUS_VERSION}）")
  else()
    set(STATUSBAR_SRC src/platform/statusbar_stub.cpp)
    message(STATUS "状态项：占位实现——pkg-config 查不到 dbus-1（装开发包 apt-get install libdbus-1-dev），"
                   "这一档只有窗口，托盘那三个数不会出现")
  endif()
endif()

add_executable(i18n_probe
  src/i18n_probe.cpp
  ${IMGUI_SRC}
)
target_include_directories(i18n_probe PRIVATE ${IMGUI_DIR} ${GLFW_INC} ${FREETYPE_INC})
target_link_libraries(i18n_probe ${GLFW_LIB} ${PLATFORM_LIBS} ${FREETYPE_LIB} Threads::Threads)

add_executable(pin_imgui_shell
  src/pin_imgui_shell.cpp
  src/jsonl.cpp
  src/scan.cpp
  src/alert.cpp
  src/claims.cpp
  src/hook_install.cpp
  ${STATUSBAR_SRC}
  ${OPEN_SRC}
  ${NOTIFY_SRC}
  ${LANG_SRC}
  ${AUTOSTART_SRC}
  ${FOCUS_SRC}
  ${CURSOR_SRC}
  ${IMGUI_SRC}
  # 粗体字形要真加粗轮廓（FT_GlyphSlot_Embolden）才不糊：只给 pin_imgui_shell 编进来，
  # i18n_probe 只量覆盖，不需要加载器。
  ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
)
target_include_directories(pin_imgui_shell PRIVATE src ${IMGUI_DIR} ${GLFW_INC} ${FREETYPE_INC})
target_compile_definitions(pin_imgui_shell PRIVATE AP_VERSION="0.1.3")
target_link_libraries(pin_imgui_shell ${GLFW_LIB} ${PLATFORM_LIBS} ${FREETYPE_LIB} Threads::Threads)
if(LIBDBUS_FOUND)
  target_include_directories(pin_imgui_shell PRIVATE ${LIBDBUS_INCLUDE_DIRS})
  target_link_directories(pin_imgui_shell PRIVATE ${LIBDBUS_LIBRARY_DIRS})
  target_link_libraries(pin_imgui_shell ${LIBDBUS_LIBRARIES})
endif()
if(SQLITE3_LIB)
  target_compile_definitions(pin_imgui_shell PRIVATE AP_HAVE_SQLITE=1)
  target_link_libraries(pin_imgui_shell ${SQLITE3_LIB})
else()
  message(WARNING "没找到 sqlite3：ZCode / WorkBuddy 两路不会接入，只有 JSONL 三家")
endif()

# 提醒层的取证工具：不链 ImGui/GLFW/Freetype —— 「该不该响、响过没有、通道什么状态」
# 这件事必须能在一个没有窗口服务器的环境里判，不然它就是不可验证的主张。
add_executable(alert_probe
  src/alert_probe.cpp
  src/alert.cpp
  src/jsonl.cpp
  src/scan.cpp
  ${OPEN_SRC}
  ${NOTIFY_SRC}
)
target_include_directories(alert_probe PRIVATE src)
target_link_libraries(alert_probe ${PLATFORM_LIBS} Threads::Threads)
if(SQLITE3_LIB)
  target_compile_definitions(alert_probe PRIVATE AP_HAVE_SQLITE=1)
  target_link_libraries(alert_probe ${SQLITE3_LIB})
endif()

# 权益层的取证工具：只依赖 <ctime>/POSIX，不链 ImGui/GLFW/Cocoa ——
# 「这一格本机到底有没有读数」必须能在没有窗口的环境里判，不然它就是不可验证的主张。
add_executable(claims_probe
  src/claims_probe.cpp
  src/claims.cpp
  src/jsonl.cpp
)
target_include_directories(claims_probe PRIVATE src)
target_link_libraries(claims_probe Threads::Threads)

# ─────────────────────────────────────────────────────────────────────────────
# 安装规则：解决「装完之后去哪儿找它」。
#
# macOS 不走这里 —— 那边由 Tools/make_bundle.sh 打成 Agent Ping.app，formula 自己
# 拼包（brew 的构建沙箱写不了 /Applications）。这里服务 Linux / BSD 的标准布局：
#
#   <prefix>/bin/agent-ping                                  可执行文件
#   <prefix>/share/agent-ping/fonts/                         内嵌字体
#   <prefix>/share/agent-ping/sni-theme/                     托盘图标主题
#   <prefix>/share/applications/agent-ping.desktop           应用菜单入口
#   <prefix>/share/icons/hicolor/<size>/apps/agent-ping.png  菜单里那张图
#
# 资源位置由 src/pin_imgui_shell.cpp 的 BundleResources() 与
# src/platform/statusbar_sni.cpp 的 FindThemeDir() 按可执行文件的相对位置解析，
# 所以 bin/ 与 share/ 的相对关系不要改。
# ─────────────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

# 装了却不知道去哪找，等于没装：Linux 用户在应用菜单里得能搜到一个有名字、有图标的条目，
# 而不是一个叫 pin_imgui_shell 的裸二进制。这个开关只用于在非 Linux 上验证路径拼得对。
option(AGENTPING_INSTALL_DESKTOP
       "Install the .desktop entry and hicolor icons (default: on for Linux)" OFF)
if(UNIX AND NOT APPLE)
  set(AGENTPING_INSTALL_DESKTOP ON)
endif()

install(PROGRAMS $<TARGET_FILE:pin_imgui_shell>
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME agent-ping)

if(UNIX AND AGENTPING_INSTALL_DESKTOP)
  install(DIRECTORY assets/sni-theme
          DESTINATION "${CMAKE_INSTALL_DATADIR}/agent-ping")
  install(DIRECTORY fonts
          DESTINATION "${CMAKE_INSTALL_DATADIR}/agent-ping")
  install(FILES assets/agent-ping.desktop
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
  install(FILES assets/desktop-icons/agent-ping-256.png
          DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
          RENAME agent-ping.png)
  install(FILES assets/desktop-icons/agent-ping-512.png
          DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
          RENAME agent-ping.png)
endif()
