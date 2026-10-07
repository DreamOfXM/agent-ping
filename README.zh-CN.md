# Agent Ping

![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux-lightgrey.svg)

[English](README.md)

一个菜单栏面板，告诉你此刻**哪一个** AI CLI 会话在等你。

它读各家编程 Agent 已经写在你本机上的日志文件，算出谁在跑、谁在排队、谁报错停了、
谁就搁在那儿等一个回答——然后在你没盯着的时候出声。没有服务端、没有账号、没有埋点：
这个二进制里没有任何网络调用。

```
需要你介入 2 · 运行中 3 · 今日消耗 512 cr
┌ 需要你介入 ────────────────────────────
│ ● 【project-a】修一个分页 bug     [ 允许吗？ ]  等待确认
│ ● 【project-b】排查构建失败       跑一次测试？   已读 / 稍后
```

## 它是什么，以及它故意不做什么

**是**：一个实时状态监视器 + 提醒层。价值单位是一行——一个会话、一个状态、一个跳转落点。

**不是**：用量/成本分析看板。它不按模型拆花费、不算缓存命中率、不跟订阅额度、
不把任何数字折算成钱。它只有一个时间窗：今天。想要对账那一视角，请另找工具；
这是两件事。

## 界面

一个：pin 面板。另有四屏（详情·窄版 / 会话聚焦 / 历史 / 设置）已定稿但**未实现**——
见[已知限制](#已知限制)。

## 隐私——它到底读什么

下面每一条都是「读一个本来就归你的工具所有的文件」。不上传，且除了文末那个状态目录
之外不写任何东西。

| 来源 | 路径（macOS） | 取什么 |
|---|---|---|
| Qoder CLI | `~/.qoder/projects/**/*.jsonl` | 会话 id、标题、cwd、时间戳、`message.usage` 的 token/credit 字段、用户记录条数 |
| Claude Code | `~/.claude/projects/**/*.jsonl` | 同上，但没有 credits（那家根本没有这个字段） |
| Codex | `~/.codex/sessions/**` | `session_meta`（cwd、session id）、`token_count` 记录、结局 |
| ZCode | `~/.zcode/cli/db/db.sqlite` | `chat_sessions` + 每会话最后一条消息、`tokens.input` 求和（只读） |
| WorkBuddy | `~/.workbuddy/workbuddy.db`、`~/.workbuddy/projects` | 会话行、`providerData.usage.inputTokens` |
| OpenCode | `~/.local/share/opencode/opencode.db`（认 `$XDG_DATA_HOME`） | `session` 的标题/工作目录/`time_updated`、`part` 里的 `step-finish` 词元求和与 `tool.state.status`（只读） |
| Hook 收件箱 | `~/Library/Application Support/AgentPing/hooks/*.json` | 待回答的原话和它的选项——这些文件是**你**用 CLI 的 notification hook 写进来的，Agent Ping 从不往这个目录写 |

Linux 上那两处 Agent Ping 自己的路径改到 `$XDG_CONFIG_HOME/AgentPing/…`。

它**不**读钥匙串、不读环境变量里的密钥、不读 auth token、不读 shell 历史，
除上表点名的字段之外也不读你 transcript 的内容。它写的文件只有状态目录里的
`alerted.txt`（去重账本）和 `settings.txt`（Linux 上另有一个 `/tmp/agentping-err-<pid>.log`
诊断文件，权限 `0600`，且只在通知失败时才写）。

会话标题会显示在屏幕上，也是提醒里被引用的内容，所以请把面板当作你自己的终端
回滚缓冲区来看待。

## 屏幕上每个数字的口径

| 屏上所见 | 定义 | 实话 |
|---|---|---|
| `需要你介入 N` | 今天这个窗内，处于「等你授权」**加上**以报错收尾的会话数 | |
| `运行中 N` | 正在跑的 **加上** 排队中的会话数 | |
| `今日消耗 N cr` | 时间戳落在本地零点之后（含）的 assistant 记录里 `message.usage.credits` 之和 | 只有 Qoder CLI 会写 `credits` 字段。那是那家厂商自己的计数，不是我们估的，也不是钱。没有这个字段的厂商贡献 0。 |
| `↑ +12.3% 较昨日` | 分母＝昨天同一个窗口的量 | 昨天为 0 时这一行不画 |
| 行上的 `第 77 轮` | 不带 `toolUseResult` 键的 `type:"user"` 记录条数 | 本机四路日志都没有「总轮数」，所以进度画成流动条，绝不画成百分比 |
| 行上的迷你柱 | 会话尾部 24 格 × 15 分钟的 assistant 记录数 | 某一处凑不出 2 格就整块不画 |

token 数会采集，但**不做跨厂商求和后显示**，因为五家说的「tokens」不是同一个东西：
transcript 求和的是 `input + cache_read + cache_creation`，Codex 给的是累计总量，
ZCode 存 `tokens.input`，WorkBuddy 的 `inputTokens` 已经含缓存命中，OpenCode 逐轮写
`step-finish`，我们取它的 `input + cache.read + cache.write`。
把这五个加起来的数字不属于任何一家的测量。

## 提醒

一个会话按 `(agent, session)` 这个键只提醒一次。账本落盘，所以重启不会把整块面板
重响一遍。每轮最多发 4 条，超出的不是丢掉，而是留到后面几轮再发。
`免打扰` 只压新提醒，不清账本。

`ap_notify_post` 返回四个码之一，区别很要紧：

| 码 | 含义 | 账本 |
|---|---|---|
| 0 | 横幅已投递 | 记账 |
| 1 | 横幅通道不通，退化成提示音 | 记账 |
| 2 | 两边都不通 | 记账，并在设置里暴露 |
| 3 | 平台没在时限内回执 | **抹掉这个键**——本轮不算数，所以下一轮还能重试 |

## 依赖

- C++17、CMake ≥ 3.20、Ninja
- GLFW3、FreeType 2、OpenGL 3.3 core
- 可选 SQLite3（没有就跳过 ZCode、WorkBuddy 与 OpenCode，并在构建时告警）
- macOS 13+ **或** 带 X11 + `libnotify` 的 Linux

## 构建

macOS：

```sh
brew install cmake ninja glfw freetype
cmake -S . -B build -G Ninja && cmake --build build
sh Tools/make_bundle.sh        # -> build/AgentPing-cpp.app
```

Linux（Debian 12 包名，逐 target 真编过）：

```sh
sudo apt-get install -y cmake ninja-build libglfw3-dev libfreetype6-dev \
    libsqlite3-dev libgl1-mesa-dev
cmake -S . -B build-linux -G Ninja && cmake --build build-linux
# 提醒层要真行为还需两个包：libnotify-bin（横幅）、sound-theme-freedesktop（出声）
```

## 不开显示器也能试

```sh
./build/pin_imgui_shell --dump            # 一轮扫描打成文本，不建窗口
D=$(mktemp -d); AGENTPING_STATE_DIR=$D ./build/alert_probe --selftest   # 判据 + 去重账本，完全离线
./build/alert_probe --channel             # 横幅通道活着吗？0/1/2
./build/alert_probe --post                # 一条合成提醒，走完整链路
xvfb-run -a -s "-screen 0 1400x1100x24" ./build/pin_imgui_shell --dump
```

`--dump` 就是对账工具：屏幕上的每个数字在里面都有一行，所以面板跟真相不一致时，
在终端里就能查出来。

## 已知限制

提 issue 前先读这段——每一条都是带成因的已知缺口，不是悬案。

- **macOS 横幅可能被拒。** 没有正确签名并注册的构建会被通知系统静默拒绝，
  面板随即退化成提示音（码 1）。请到系统设置 → 通知里放行。
- **Linux 没有菜单栏项。** `statusbar_stub.cpp` 直接返回失败；面板就是一个普通窗口，
  也没有托盘图标。
- **Linux 出声未验。** 验证环境里没有声卡设备，所以「Linux 上真的响了」这件事没有被
  观察到——只观察到代码路径返回了。
- **Linux 点横幅不跳转。** 点行可以跳，点横幅不行。
- **`已读` / `稍后` 只在内存里。** 重启不保留。
- **只有一屏。** 详情·窄版、会话聚焦、历史、设置都只有设计稿没有实现，因此没有持久化的
  设置界面：提示音与免打扰从 `settings.txt` 读、用环境变量设。
- **只有五家 CLI。** 其余的根本不会出现在面板上。
- **真机 Linux 桌面零台。** 所有 Linux 证据都来自 Xvfb。

## 目录结构

```
src/            面板、采集层、提醒层、平台垫片
src/platform/   notify / open / statusbar，按 OS 拆在 C 头文件后面
vendor/imgui/   Dear ImGui 1.93.0 WIP，内置一份以便构建不联网
fonts/          OFL 授权的界面字体，连同各自的许可正文
assets/         Info.plist、App 图标、菜单栏图标
Tools/          make_bundle.sh、make_menubar_icon.py
CMakeLists.txt  两个平台都在，含上面的依赖清单
```

字体是随仓库提交而不是下载，为的是 clone 之后能离线构建；这就是仓库里有约 40 MB
字体二进制的原因。

## 许可

Apache License 2.0 —— 见 [LICENSE](LICENSE)。

第三方素材保留自己的条款：Dear ImGui 是 MIT（`vendor/imgui/LICENSE.txt`），
界面字体是 SIL OFL 1.1（`fonts/LICENSE-*.txt`）。Noto CJK 那几个文件带保留字体名
`Source`：随便改，但改过的版本不得再用这个名字。
