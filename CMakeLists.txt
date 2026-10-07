cmake_minimum_required(VERSION 3.20)
project(agent_ping_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

find_library(GLFW_LIB glfw HINTS /opt/homebrew/lib /usr/local/lib REQUIRED)
find_path(GLFW_INC GLFW/glfw3.h HINTS /opt/homebrew/include /usr/local/include REQUIRED)
find_library(FREETYPE_LIB freetype HINTS /opt/homebrew/lib /usr/lib REQUIRED)
find_path(FREETYPE_INC ft2build.h HINTS /opt/homebrew/opt/freetype/include/freetype2 /usr/include/freetype2 REQUIRED)

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
  find_library(USERNOTIFICATIONS_LIB UserNotifications REQUIRED)
  set(PLATFORM_LIBS ${COCOA_LIB} ${IOKIT_LIB} ${OPENGL_LIB} ${COREGRAPHICS_LIB} ${USERNOTIFICATIONS_LIB})
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
  ${STATUSBAR_SRC}
  ${OPEN_SRC}
  ${NOTIFY_SRC}
  ${IMGUI_SRC}
)
target_include_directories(pin_imgui_shell PRIVATE src ${IMGUI_DIR} ${GLFW_INC} ${FREETYPE_INC})
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
