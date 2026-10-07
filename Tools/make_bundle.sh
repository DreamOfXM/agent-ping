#!/bin/sh
# 出包：C++/ImGui 探针 → AgentPing-cpp.app（带 E4 图标 + 签名）
# 图标配方抄自 Swift 侧已验证的 Tools/bundle.sh：iconset 只认那 10 个名字。
set -e
cd "$(dirname "$0")/.."

SRC_PNG=assets/icon-1024.png
BIN=build/pin_imgui_shell
APP=build/AgentPing-cpp.app

[ -f "$SRC_PNG" ] || { echo "缺源图 $SRC_PNG"; exit 2; }
[ -x "$BIN" ] || { echo "缺可执行 $BIN，先 cmake --build build"; exit 2; }

if [ ! -f assets/AppIcon.icns ] || [ "$SRC_PNG" -nt assets/AppIcon.icns ]; then
  rm -rf assets/AppIcon.iconset && mkdir -p assets/AppIcon.iconset
  for s in 16 32 128 256 512; do
    sips -z $s $s "$SRC_PNG" --out "assets/AppIcon.iconset/icon_${s}x${s}.png" >/dev/null
    d=$((s*2))
    [ $d -le 1024 ] && sips -z $d $d "$SRC_PNG" --out "assets/AppIcon.iconset/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns assets/AppIcon.iconset -o assets/AppIcon.icns
  rm -rf assets/AppIcon.iconset
fi

[ -f assets/menubar-icon@2x.png ] || /usr/bin/python3 Tools/make_menubar_icon.py 2>/dev/null \
  || ~/browser-use-env/bin/python Tools/make_menubar_icon.py

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/fonts"
cp "$BIN" "$APP/Contents/MacOS/AgentPing"
cp assets/Info.plist "$APP/Contents/Info.plist"
cp assets/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
cp assets/menubar-icon.png assets/menubar-icon@2x.png "$APP/Contents/Resources/"
cp fonts/*.ttf "$APP/Contents/Resources/fonts/"
# 字体是 OFL 授权的第三方产物，许可正文必须随包，不然只有 ttf 就是违约分发。
for lic in fonts/LICENSE-* fonts/OFL-*; do
  if [ -f "$lic" ]; then cp "$lic" "$APP/Contents/Resources/fonts/"; fi
done
# 包是独立分发物，收不到仓库根目录，所以本项目的 Apache-2.0 正文也得塞进去。
if [ -f LICENSE ]; then cp LICENSE "$APP/Contents/Resources/LICENSE"; fi
printf 'APPL????' > "$APP/Contents/PkgInfo"

IDENT=$(security find-identity -v -p codesigning 2>/dev/null \
        | awk -F'"' '/Apple Development|Apple Distribution/{print $2; exit}')
if [ -n "$IDENT" ]; then
  codesign --force --sign "$IDENT" --timestamp=none "$APP" >/dev/null 2>&1 \
    || codesign --force --sign - --timestamp=none "$APP" >/dev/null 2>&1
else
  codesign --force --sign - --timestamp=none "$APP" >/dev/null 2>&1
fi

echo "落位: $APP"
du -sh "$APP" | awk '{print "体积:", $1}'
stat -f '%N  %z B  %Sm' "$APP/Contents/MacOS/AgentPing" "$APP/Contents/Resources/AppIcon.icns"
