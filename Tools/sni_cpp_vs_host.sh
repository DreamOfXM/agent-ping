#!/bin/sh
# 我们的 C++ 状态项挂到同一个假宿主上，把总线对端真读到的东西落盘。
# 和 Tools/sni_ref_vs_host.sh 用的是同一个 sni_host.py，两份 props.txt 才能逐行 diff——
# 「和参考实现对齐」必须是可核对的，不是形容词。
#
# 取证目录走 ASCII 路径（中文路径在这台机器上偶尔被转码打乱，之后再也找不到证据）。
# AGENTPING_STATE_DIR 指到临时目录：这轮只验总线，不许碰真收件箱和去重账本。
# AGENTPING_ALERT=0：截屏／总线取证轮不许顺手发横幅。
# HOME／XDG_CONFIG_HOME 一并指到 $OUT 下面：采集器读的是 $HOME/.claude 等真实会话目录，
# 容器里没有真 agent 在跑 ⇒ 三档计数恒为 0 ⇒ 标签按「0 档不占字位」的规则就是空串，
# 于是「三个数有没有上总线」这条读回来也证伪不了。所以先把 HOME 关进沙盒，再灌假会话。
set -x
OUT=${1:-/tmp/apref/cpp}
BIN=${2:-/work/build-linux/pin_imgui_shell}
mkdir -p "$OUT"
rm -f "$OUT"/host.log "$OUT"/app.log "$OUT"/props.txt "$OUT"/host_report.json \
      "$OUT"/seed.txt "$OUT"/t_app_start "$OUT"/t_app_exit "$OUT"/*.pam
rm -rf "$OUT"/home "$OUT"/xdg "$OUT"/state
mkdir -p "$OUT"/home "$OUT"/xdg "$OUT"/state
xvfb-run -a -s "-screen 0 1400x1100x24" dbus-run-session -- sh -c "
  cd /work
  python3 Tools/sni_seed_fixture.py --home $OUT/home > $OUT/seed.txt 2>&1
  echo SEED_EXIT=\$?
  cat $OUT/seed.txt
  # --drive：宿主读完属性后真的点一次（Activate → 菜单「显示／隐藏」→ 菜单「退出」）。
  # 探测放在 12 秒、进程挂 20 秒：上一轮宿主 14 秒探测而 App 只挂 9 秒，居然照样读到了
  # 全部属性——那一刻说不清属性是在进程活着时被读走的、还是进程没了还在回。
  # 现在把窗口拉开，并让宿主自己打相对秒（host.log 每行行首），时间线才立得住。
  python3 Tools/sni_host.py $OUT 12 --drive > $OUT/host.log 2>&1 &
  HP=\$!
  sleep 1
  date +%s%N > $OUT/t_app_start
  HOME=$OUT/home XDG_CONFIG_HOME=$OUT/xdg \
    AGENTPING_STATE_DIR=$OUT/state AGENTPING_ALERT=0 AGENTPING_SNI_HOLD=20000 \
    $BIN /work/fonts --window > $OUT/app.log 2>&1
  echo APP_EXIT=\$?
  date +%s%N > $OUT/t_app_exit
  wait \$HP
  echo HOST_EXIT=\$?
"
set +x
echo "=== 时间线（纳秒 epoch，宿主相对秒见 host.log 行首）==="
for f in t_app_start t_app_exit; do
  [ -f "$OUT/$f" ] && echo "$f=$(cat $OUT/$f) ns"
done
echo "=== host.log ==="
cat "$OUT/host.log"
echo "=== app.log（状态项／面板／窗口相关的行）==="
grep -n "状态项\|面板\|提醒\|窗口" "$OUT/app.log" | head -30
echo "=== props.txt ==="
cat "$OUT/props.txt" 2>/dev/null || echo '（没有 props.txt ＝ 一个项都没注册上）'
