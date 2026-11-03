#!/usr/bin/env bash
# 量的是「装上去的那条右缘带挡不挡桌面」，不是「我们的 shim 能不能改输入形状」。
# 所以这里跑的是真二进制：Xvfb 上起一次灯舱态、再起一次面板态，同一支探针往同样的坐标敲，
# 数三块窗各自收到几次——面板态那一列必须读成「App 自己收到」，否则灯舱态的「穿」是假通过。
#
# 用法（容器里，仓库根）：BIN=build-linux/pin_imgui_shell bash Tools/linux_edge_clickthrough.sh
# 退出码＝不合判据的项数（两轮相加）。
set -u

BIN=${BIN:-build-linux/pin_imgui_shell}
PROBE=${PROBE:-build-linux/x11_probe}
FONTS=${FONTS:-./fonts}
DISP=${DISP:-89}
SW=${SW:-1400}
SH=${SH:-1100}
WAIT=${WAIT:-8}
OUT=${OUT:-out_linux_click}
STATE=$OUT/state

for f in "$BIN" "$PROBE"; do
    [ -x "$f" ] || { echo "缺可执行文件：$f（先 cmake --build build-linux）"; exit 2; }
done
command -v Xvfb >/dev/null || { echo "容器里没有 Xvfb"; exit 2; }
mkdir -p "$OUT" "$STATE" || exit 2

Xvfb ":$DISP" -screen 0 "${SW}x${SH}x24" >/dev/null 2>&1 &
XV_PID=$!
trap 'kill $APP_PID 2>/dev/null; kill $XV_PID 2>/dev/null' EXIT
export DISPLAY=":$DISP"
sleep 2

run_case() {  # $1＝dock|panel；剩下的是给 App 的 AGENTPING_* 覆盖
    local name=$1; shift
    echo "=== $name 态 ==="
    env "$@" AGENTPING_STATE_DIR=$STATE AGENTPING_FIXTURE=1 AGENTPING_EDGE_FIXTURE=1 \
        AGENTPING_LANG=zh timeout 40 "$BIN" --window "$FONTS" \
        > "$OUT/app-$name.log" 2>&1 &
    APP_PID=$!
    X11PROBE_MODE=$name timeout 60 "$PROBE" --app-under "$WAIT" > "$OUT/probe-$name.log" 2>&1
    local rc=$?
    kill $APP_PID 2>/dev/null; wait $APP_PID 2>/dev/null
    grep -E "^\[落点\]|^\[x11\] 不合判据|^\[窗口形态\]|^\[灯舱\]|^\[窗口\] 工作区" \
        "$OUT/probe-$name.log" "$OUT/app-$name.log"
    echo "--- $name 探针退出码＝$rc"
    LAST_RC=$rc
}

run_case dock AGENTPING_PANEL=0 AGENTPING_EDGE=dock
DOCK_RC=$LAST_RC
run_case panel AGENTPING_PANEL=1
PANEL_RC=$LAST_RC

echo "灯舱态 rc=$DOCK_RC｜面板态 rc=$PANEL_RC｜合计不合判据＝$((DOCK_RC + PANEL_RC))"
exit $((DOCK_RC + PANEL_RC))
