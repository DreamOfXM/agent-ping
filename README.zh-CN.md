# Agent Ping

![许可证](https://img.shields.io/badge/license-Apache--2.0-blue.svg)
![平台](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux-lightgrey.svg)
![界面语言](https://img.shields.io/badge/UI-zh%20·%20en%20·%20de%20·%20it%20·%20ja%20·%20ko-lightgrey.svg)
![版本](https://img.shields.io/github/v/release/DreamOfXM/agent-ping?label=release&color=blue)

[English](README.md) · [产品页](https://dreamofxm.github.io/agent-ping/)

[安装](#安装) · [用法](#用法) · [隐私](#隐私) · [限制](#限制)

好几个 agent 工具同时在跑，谁完成了、谁在等你授权，容易漏看。Agent Ping 读这些工具本来就会写的本地日志，把要你动手的那几条放到屏幕右缘，双击就回到那条会话。

[![Agent Ping 动起来：等你队列、会话的原话提问、以及跳回会话（点图看原图）](docs/agent-ping-teaser.gif)](docs/agent-ping-teaser.gif)

## 安装

macOS 13 及以上装预编译版；带 X11 的 Linux 只能从源码构建；Windows 暂不支持。

```sh
brew tap DreamOfXM/agent-ping https://github.com/DreamOfXM/agent-ping
brew trust dreamofxm/agent-ping          # 第三方 tap，只需要跑一次
brew install --cask agent-ping
```

包里自带 glfw 和 freetype，不用先装依赖。安装包用 Developer ID 签名、经 Apple 公证，正常安装不会弹「无法验证开发者」；真弹了别放行，来 [Issues](https://github.com/DreamOfXM/agent-ping/issues) 说一声。

想自己编一份、或者 26 MB 下不动，用同一个 tap 里的 formula 本地编译；它需要 Xcode 命令行工具，Apple 芯片上约一分钟：

```sh
brew tap DreamOfXM/agent-ping https://github.com/DreamOfXM/agent-ping
brew trust dreamofxm/agent-ping
brew install agent-ping && agent-ping
```

<details>
<summary>brew 停在 Homebrew 的 Xcode 准入检查</summary>

`Error: Xcode alone is not sufficient…` 是 Homebrew 对源码构建的要求，不是本项目的；Command Line Tools 太旧，就去系统设置 → 软件更新里更新它。嫌麻烦就直接用上面的 cask，全程不碰编译器。

</details>

升级与卸载按安装时走的那条路：

```sh
# 先更新 tap
brew update

# cask 装的
brew upgrade --cask agent-ping

# formula 装的
brew upgrade agent-ping
```

```sh
# cask 装的
brew uninstall --cask agent-ping

# formula 装的
brew uninstall agent-ping && brew untap DreamOfXM/agent-ping
```

两种都装过会出两个菜单栏图标——留一个就好。

## 用法

今天想知道「谁在等」，得来回切窗口、看每个终端的最后一行。这里改成屏幕边上一份清单，双击就回到那条会话。

- **只在菜单栏。** 装完从启动台打开一次：它没有 Dock 图标，也没有能切回来的窗口，只在菜单栏右端、时钟旁边留一颗「椭圆里一颗星」。第一次启动会自己打开面板。
- **总览屏**把等你授权、等你回答、出错结束的会话排成队列。双击一行，跳回那条会话。
- **用量与额度屏**和**权益屏**看各家的余额、今日用量，以及各家自己记的领取状态。
- **右缘窄柱。** 面板收起后，屏幕右缘留一根 44pt 窄柱：谁在等、谁刚完成，不切窗口就知道。
- **提醒**按会话去重，只发一次。横幅发不出去改用声音。「免打扰」只暂停发送。
- **界面**内置中／英／德／意／日／韩六种语言，设置屏直接切换。

[![01 · 一屏看清谁在等你：左边总览，右边会话详情，圈里的编号对应图例（点图看原图）](docs/fig-01-queue-zh.png)](docs/fig-01-queue-zh.png)

*本文图片由程序自己用演示数据渲染；「样例数据」标签是产品自己的。*

[![04 · 右缘窄柱：左边常驻态，右边指针放上去立即展开的预览（点图看原图）](docs/fig-04-edge-zh.png)](docs/fig-04-edge-zh.png)

**数字哪来的？** 每个数字都读自各家工具自己写的本地日志，不走账单接口、不换算成金额，日志里没有的那一格就空着。逐屏导览和每个数字的完整定义在[产品页](https://dreamofxm.github.io/agent-ping/)。

**想看到问题的原文和选项？** 光读日志不够，要接 hook：Claude Code 和 Qoder CLI 在面板的**接线**屏点一下按钮就接上了。hook 只把问题抄进本地收件箱，从不替你回答。

## 隐私

[![03 · 六个来源，连它们扫出了什么都摊开：左边全部工具，右边今日完成（点图看原图）](docs/fig-03-sources-zh.png)](docs/fig-03-sources-zh.png)

没有服务端、没有账号、没有埋点。下面每一条都读你自己工具本来就有的文件，不上传任何东西，二进制里没有网络调用。

- **Qoder CLI** — `~/.qoder/projects/**/*.jsonl`：会话、标题、工作目录、时间、token 与 Credits 用量、轮数
- **Claude Code** — `~/.claude/projects/**/*.jsonl`：同上，不记 Credits
- **Codex** — `~/.codex/sessions/**`：会话、工作目录、token 用量、结果、额度读数
- **ZCode** — `~/.zcode/cli/db/db.sqlite`：会话、每条的最后一句、token 用量、子代理记录，还有没回完的那条「等你回答」（只读）
- **WorkBuddy** — `~/.workbuddy/workbuddy.db`、`~/.workbuddy/projects`：会话、token 用量
- **OpenCode** — `~/.local/share/opencode/opencode.db`（认 `$XDG_DATA_HOME`）：标题、工作目录、更新时间、token 用量、工具状态（只读）
- **权益领取** — `~/.workbuddy/logs/main.log`、`~/Library/Application Support/com.qoder.app.stable/logs/*/main.log`、`~/.zcode/v2/logs/<日期>.log`：各家自己记的签到结果、可领状态、面值与作废时间
- **账户余额** — `~/.codex/sessions/**`、`~/Library/Application Support/com.qoder.app.stable/logs/*/main.log`、`~/.zcode/v2/logs/<日期>.log`：各家记的「还剩多少／一共多少」，和写下时间
- **Hook 收件箱** — `~/Library/Application Support/AgentPing/hooks/*.json`：等你回答的问题原文与选项。只有装过 hook 才有内容

Linux 上 Agent Ping 自己的路径在 `$XDG_CONFIG_HOME` 和 `$XDG_DATA_HOME` 下，没设就用 `~/.config` 和 `~/.local/share`。

它**不读** Keychain、环境变量里的密钥、auth token、shell 历史，也不读上面没列的内容。

它会写的只有：状态目录里的两个记录文件（提醒去重、设置、未读标记）；装 hook 时的 `hook.sh` 和两份 CLI 配置（改前先逐字节备份）；开着「登录时启动」时的登录项；Linux 通知失败时的一个临时日志。

会话标题会显示在屏幕上，提醒里也有原文——截图分享时注意这一点。

## 限制

提 issue 之前先读这段，每条都写了原因：

- **macOS 横幅可能被系统拒发。** 去系统设置 → 通知里放行。之前面板改用声音。
- **跳进 ZCode 会话是绕路，不是接口。** 靠敲它自己的 ⌘K 检索，需要辅助功能授权。没授权只能唤起 App。
- **未读点不与工具联动。** 它的含义是「没通过本面板打开过」。
- **权益卡只能事后确认。** 没领的那一期不在本机日志里，所以没有预告；读数只在各家写盘时才变。
- **Linux 的提醒和出声还年轻。** 只在容器里验过，没上过真桌面和真硬件。缺依赖时退化成没有托盘图标的普通窗口。
- **Linux 点横幅不跳会话。** 只能唤起 App，或打开会话的工作目录。
- **只有六个来源，不是「所有 agent」。** Qoder CLI、Claude Code、Codex、ZCode、WorkBuddy、OpenCode。后三个要带 SQLite3 的构建。
- **没有真背景模糊；深底菜单栏上托盘图标不太显眼。** 玻璃质感是手画的，改图标意味着重画已定稿的标识。
- **搜索框键盘焦点未实测。** ⌘/Ctrl+F 和 Esc 已接上；打字没过滤效果的话，请报 issue。
- **设置、接线、授权三屏还是旧版面。** 其余屏已重做，这三屏会跟上。
- **资源重置卡不实现。** 本地日志没有这项，就不显示。
- **只开一个工具、一个窗口。** 如果你从不在几个 agent 之间来回切，这个面板基本只是把眼前的东西又摆一遍。

## 构建

硬要求：C++17、CMake ≥ 3.20、Ninja、GLFW3、FreeType 2、OpenGL 3.3 core，macOS 13+ 或带 X11 的 Linux。

可选依赖缺了会怎样，CMake 都会明说：SQLite3 缺了，跳过 ZCode、WorkBuddy、OpenCode 三路；Linux 上 libdbus-1 缺了没有托盘图标，libx11 + libxext 缺了窄柱关着。命令都在仓库根目录跑。

macOS（产出 `build/AgentPing-cpp.app`）：

```sh
brew install cmake ninja glfw freetype
cmake -S . -B build -G Ninja && cmake --build build
sh Tools/make_bundle.sh
```

Linux（Debian 12 包名）：

```sh
sudo apt-get install -y cmake ninja-build libglfw3-dev libfreetype6-dev \
    libsqlite3-dev libgl1-mesa-dev libdbus-1-dev libx11-dev libxext-dev
cmake -S . -B build-linux -G Ninja && cmake --build build-linux
# 提醒层要真行为还需两个包：libnotify-bin（横幅）、sound-theme-freedesktop（出声）
```

每次推 main，CI 都在 Debian 12 容器里把上面那条 apt 命令原样跑一遍，再构建、量窄柱的点击穿透。包列表就是从这份 README 里读的，失效会报错，不会悄悄过期。

装进应用菜单：

```sh
sudo cmake --install build-linux            # 前缀默认 /usr/local
```

菜单里找不到，就加 `--prefix /usr` 重装。

## 文档

- [产品页](https://dreamofxm.github.io/agent-ping/) —— 功能导览、同类对比、常见疑问在那边。它的「隐私」链接回到的是这一节。
- [Issues](https://github.com/DreamOfXM/agent-ping/issues) —— 报 bug、许愿新工具，中英文都行。先读[限制](#限制)。

## 许可

Apache License 2.0 — 见 [LICENSE](LICENSE)。

第三方材料保留各自条款：Dear ImGui 是 MIT（`vendor/imgui/LICENSE.txt`），界面字体是 SIL OFL 1.1（`fonts/LICENSE-*.txt`）。Noto CJK 带保留字体名 `Source`：可以改，但改完不得沿用那个名字。
