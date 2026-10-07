#!/bin/sh
# 参考实现在真总线上的取证：Ayatana AppIndicator3 的项注册进假宿主后，
# 把它的属性 / 图标 / 菜单形状原样落盘，作为我们 C++ 侧的对齐目标。
#
# 为什么先起宿主再起项：libayatana-appindicator 是靠「名字有没有出现」决定要不要导出的，
# 顺序反了它就是零导出且不报错，会被误读成参考实现坏了。
# 为什么套 xvfb-run：参考实现要建 Gtk.Menu，GTK 没有 DISPLAY 就直接不初始化；
# 而它失败时是**静默**的（ref.log 为空），所以宁可挂个虚拟屏。
set -x
OUT=$1
[ -z "$OUT" ] && OUT=/tmp/sniref
mkdir -p "$OUT"
rm -f "$OUT"/host.log "$OUT"/ref.log "$OUT"/props.txt "$OUT"/host_report.json "$OUT"/*.pam
xvfb-run -a -s "-screen 0 1400x1100x24" dbus-run-session -- sh -c "
  python3 /work/Tools/sni_host.py $OUT 12 > $OUT/host.log 2>&1 &
  HP=\$!
  sleep 1
  python3 /work/Tools/sni_reference_item.py 10 > $OUT/ref.log 2>&1 &
  RP=\$!
  wait \$HP
  echo HOST_EXIT=\$?
  kill \$RP 2>/dev/null
  wait \$RP 2>/dev/null
"
set +x
echo "=== host.log ==="
cat "$OUT/host.log"
echo "=== ref.log ==="
cat "$OUT/ref.log"
echo "=== props.txt ==="
cat "$OUT/props.txt" 2>/dev/null || echo '（没有 props.txt ＝ 一个项都没注册上）'
echo "=== 落盘 ==="
ls -l "$OUT"
