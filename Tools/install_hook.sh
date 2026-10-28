#!/bin/sh
# 接线 CLI 的 Notification 钩子：与面板里那颗「写入」调的是同一个二进制入口，
# 不另写一套 JSON 处理。用法：
#   Tools/install_hook.sh status
#   Tools/install_hook.sh install claude|qoder
#   Tools/install_hook.sh uninstall claude|qoder
# 退出码沿用全仓口径：0 成功 / 1 失败 / 2 用法错。
set -u

usage() {
    echo "用法: $0 status | install <claude|qoder> | uninstall <claude|qoder>" >&2
    exit 2
}

[ $# -ge 1 ] || usage
cmd=$1
case "$cmd" in
    status|install|uninstall) ;;
    *) usage ;;
esac
if [ "$cmd" != "status" ] && [ $# -lt 2 ]; then usage; fi

# 找可执行文件：环境变量优先，其次源码树里的构建产物，最后是装好的 App。
find_bin() {
    if [ -n "${AGENTPING_BIN:-}" ]; then echo "$AGENTPING_BIN"; return 0; fi
    here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    root=$(dirname -- "$here")
    for p in "$root/build/pin_imgui_shell" \
             "$root/build/AgentPing-cpp.app/Contents/MacOS/AgentPing" \
             "/Applications/AgentPing-cpp.app/Contents/MacOS/AgentPing"; do
        [ -x "$p" ] && { echo "$p"; return 0; }
    done
    return 1
}

bin=$(find_bin) || {
    echo "找不到 Agent Ping 可执行文件：设 AGENTPING_BIN 或先 cmake --build build" >&2
    exit 1
}

case "$cmd" in
    status) exec "$bin" --hook-status ;;
    install) exec "$bin" --install-hook "$2" ;;
    uninstall) exec "$bin" --uninstall-hook "$2" ;;
esac
