#!/bin/sh
# 用**真的第三方宿主**（gtk-sni-tray-standalone，Haskell+GTK 写的独立 SNI 托盘）
# 把我们的状态项截成图，并在同一轮里发真实点击，看真宿主那一侧的图标会不会消失。
#
# 为什么还要这一条：前面所有取证走的是 Tools/sni_host.py——我们自己写的假宿主。
# 假宿主能证明「总线上这些属性读得到、形状对」，证明不了「真托盘把这个项画成了什么」：
# 图标会不会被缩放糊掉、Label 有没有地方显示、点了退出条目后托盘里那颗会不会真的消失，
# 只有真宿主画出来才知道。
#
# 为什么带 --watcher：0.1.8.1 不带这个开关时它自己去查 org.kde.StatusNotifierWatcher，
# 查不到就 `user error (Pattern match failure ... app/Main.hs:177)` 直接退出（实测），
# 会被误读成「这个宿主不支持我们」。
# 为什么用 -c 指定底色：判「这颗图标在浅/深面板上看不看得见」不能靠肉眼估，
# 把两种底各截一张，再拿截图本身量像素，才是可复算的证据。
# 为什么每步查 Map State：xwininfo -root -tree 列的是**所有**子窗口，unmap 掉的照样在，
# 第一版数「树里有没有 Agent Ping 那一行」三步全是 1，等于没测（实测）。
#
# 用法（容器内）：sh Tools/sni_real_tray_shot.sh /tmp/apref/tray
set -e
OUT=${1:-/tmp/apref/tray}
BIN=${2:-/work/build-linux/pin_imgui_shell}
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.log "$OUT"/win_*.txt "$OUT"/seed.txt "$OUT"/quit_seen.txt
rm -rf "$OUT"/home "$OUT"/xdg "$OUT"/state
mkdir -p "$OUT"/home "$OUT"/xdg "$OUT"/state

# 内层脚本写成文件再执行：这里套了三层引号（docker exec / xvfb / dbus-run / sh -c），
# 内联的话 $id 会被外层提前展开成空串。
cat > "$OUT"/inner.sh <<'EOS'
cd /work
snap() {
  id=$(xwininfo -root -tree | sed -n 's/^[[:space:]]*\(0x[0-9a-f]*\) "Agent Ping".*/\1/p' | head -1)
  if [ -n "$id" ]; then
    xwininfo -id "$id" -stats | tr -d ' \n' > "$OUT/win_$1.txt"
  else
    printf 'NO_SUCH_WINDOW' > "$OUT/win_$1.txt"
  fi
  scrot "$OUT/tray-$1.png"
}
run_app() {
  # $1=日志名 $2=取图路（both|name|pixmap），AGENTPING_SNI_ICON 决定发不发像素
  HOME="$OUT/home" XDG_CONFIG_HOME="$OUT/xdg" AGENTPING_STATE_DIR="$OUT/state" \
    AGENTPING_ALERT=0 AGENTPING_SNI_HOLD=40000 AGENTPING_SNI_ICON="$2" \
    "$BIN" /work/fonts --window > "$OUT/$1" 2>&1 &
  echo $!
}
python3 Tools/sni_seed_fixture.py --home "$OUT/home" > "$OUT/seed.txt" 2>&1

# ── 第一轮：深底 ──
gtk-sni-tray-standalone --watcher --top -s 28 -p 2 -c '#1c1c20' > "$OUT/tray_dark.log" 2>&1 &
DP=$!
sleep 3
AP=$(run_app app.log both)
sleep 6
snap 0-dark-shown
python3 Tools/sni_real_click.py "$OUT" showhide > "$OUT/click_showhide.log" 2>&1 || echo FAIL >> "$OUT/click_showhide.log"
sleep 1; snap 1-dark-hidden
python3 Tools/sni_real_click.py "$OUT" activate >> "$OUT/click_activate.log" 2>&1 || echo FAIL >> "$OUT/click_activate.log"
sleep 1; snap 2-dark-activated
kill "$AP" 2>/dev/null; kill "$DP" 2>/dev/null
wait "$AP" 2>/dev/null; wait "$DP" 2>/dev/null; true

# ── 第二轮：浅底 + 点「退出」后看真托盘里那颗会不会消失 ──
# 另起一条总线（外层已经分了两个 dbus-run-session），同一条总线上残留注册名会互相看得见
gtk-sni-tray-standalone --watcher --top -s 28 -p 2 -c '#f2f2f7' > "$OUT/tray_light.log" 2>&1 &
DP=$!
sleep 3
AP=$(run_app app_light.log both)
sleep 6
snap 3-light-shown
python3 Tools/sni_real_click.py "$OUT" quit > "$OUT/click_quit.log" 2>&1 || echo FAIL >> "$OUT/click_quit.log"
sleep 2
snap 4-light-after-quit
grep -c '菜单：退出' "$OUT/app_light.log" > "$OUT/quit_seen.txt" || true
kill "$DP" 2>/dev/null; wait "$DP" 2>/dev/null; true

# ── 第三轮：只发 IconName + IconThemePath（不发像素）──
# 这一轮是「名字那条路到底通不通」的对照：像素关掉后宿主如果还能画出图标，
# 说明我们带来的主题目录真的被读到了；画不出来就是空白，也是结论。
gtk-sni-tray-standalone --watcher --top -s 28 -p 2 -c '#1c1c20' > "$OUT/tray_name.log" 2>&1 &
DP=$!
sleep 3
AP=$(run_app app_name.log name)
sleep 6
snap 5-name-only-dark
kill "$AP" 2>/dev/null; kill "$DP" 2>/dev/null
wait "$AP" 2>/dev/null; wait "$DP" 2>/dev/null; true
EOS

xvfb-run -a -s "-screen 0 1400x1100x24" dbus-run-session -- \
  env OUT="$OUT" BIN="$BIN" sh -c "cp $OUT/inner.sh /work/.tray_inner.sh; cd /work; sh .tray_inner.sh; r=\$?; rm -f .tray_inner.sh; exit \$r"
# 内层是临时夹具：跑完就删。第一版忘了删，仓库根（挂了进容器）留下一个 .tray_inner.sh 被 git 看见。
# `r=$?; ...; exit $r` 是为了删文件不吃掉内层的退出码。

echo "=== 面板窗口的 Map State（IsViewable=在屏上，IsUnMapped=收掉了）==="
for f in "$OUT"/win_*.txt; do
  printf '%-28s %s\n' "$(basename "$f" .txt)" \
    "$(sed -n 's/.*MapState:\([^ ]*\).*/\1/p' "$f" | tr -d '\n')"
done
echo "=== 点击回执 ==="
for f in "$OUT"/click_*.log; do printf '%s: %s\n' "$(basename "$f" .log)" "$(tr '\n' ' ' < "$f")"; done
echo "=== 每一轮的取图路与面板动作 ==="
grep -hE '取图路|\[面板\]|\[状态项\] 菜单' "$OUT"/app.log "$OUT"/app_light.log "$OUT"/app_name.log
echo "=== 落盘 ==="
ls -l "$OUT"
