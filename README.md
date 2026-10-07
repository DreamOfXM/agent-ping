# Agent Ping

![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux-lightgrey.svg)

[中文](README.zh-CN.md)

A menu-bar panel that tells you which AI CLI session needs you **right now**.

It reads the log files that coding agents already write on your machine, works out
who is running, who is queued, who stopped with an error, and who is sitting there
waiting for an answer — then rings when you are not looking. No server, no account,
no telemetry: the binary contains no network calls.

```
需要你介入 2 · 运行中 3 · 今日消耗 512 cr
┌ 需要你介入 ────────────────────────────
│ ● 【project-a】修一个分页 bug     [ 允许吗？ ]  等待确认
│ ● 【project-b】排查构建失败       跑一次测试？   已读 / 稍后
```

## What it is, and what it deliberately is not

**Is**: a live state monitor plus an alert layer. The unit of value is one row —
one session, one state, one jump target.

**Is not**: a usage/cost analytics dashboard. It does not break spend down per
model, does not compute cache-hit ratios, does not track subscription quota, and
does not convert anything into a currency amount. It has one time window — today.
If you want the accounting view, build or use one; the two are different jobs.

## Screens

One: the pin panel. Four more exist as approved designs (detail-narrow, session
focus, history, settings) and are **not implemented** — see
[Known limitations](#known-limitations).

## Privacy — exactly what it reads

Everything below is a read of a file your tools already own. Nothing is uploaded,
and nothing is written outside the state directory listed at the bottom.

| Source | Path (macOS) | What is taken from it |
|---|---|---|
| Qoder CLI | `~/.qoder/projects/**/*.jsonl` | session id, title, cwd, timestamps, `message.usage` token/credit fields, user-record count |
| Claude Code | `~/.claude/projects/**/*.jsonl` | same, minus credits (the field does not exist there) |
| Codex | `~/.codex/sessions/**` | `session_meta` (cwd, session id), `token_count` records, outcome |
| ZCode | `~/.zcode/cli/db/db.sqlite` | `chat_sessions` + last message per session, `tokens.input` sum (read-only) |
| WorkBuddy | `~/.workbuddy/workbuddy.db`, `~/.workbuddy/projects` | session rows, `providerData.usage.inputTokens` |
| OpenCode | `~/.local/share/opencode/opencode.db` (honours `$XDG_DATA_HOME`) | `session` title/cwd/`time_updated`, `part` `step-finish` token sums and `tool.state.status` (read-only) |
| Hook inbox | `~/Library/Application Support/AgentPing/hooks/*.json` | the verbatim question and its options — **you** write these files from a CLI notification hook; Agent Ping never writes into this directory |

On Linux the two Agent Ping paths move to `$XDG_CONFIG_HOME/AgentPing/…`.

It does **not** read Keychain, environment secrets, auth tokens, shell history, or
your transcripts' contents beyond the fields named above. The only files it writes
are `alerted.txt` (the dedup ledger) and `settings.txt` in its own state directory
(plus a `/tmp/agentping-err-<pid>.log` diagnostic on Linux, mode `0600`, only when a
notification fails).

Session titles are shown on screen and are what the alert quotes, so treat the
panel the way you treat your own terminal scrollback.

## Where each number comes from

| On screen | Definition | Honest caveat |
|---|---|---|
| `需要你介入 N` | sessions in `awaiting permission` **plus** sessions that ended in an error, inside today's window | |
| `运行中 N` | sessions working **plus** sessions queued | |
| `今日消耗 N cr` | sum of `message.usage.credits` over assistant records with a timestamp at or after local midnight | only Qoder CLI emits a `credits` field. This is that vendor's own counter, not an estimate by us and not money. Vendors without the field contribute 0. |
| `↑ +12.3% 较昨日` | same window yesterday as denominator | hidden unless yesterday > 0 |
| `第 77 轮` on a row | count of `type:"user"` records without a `toolUseResult` key | there is no total-turn count in any local log, so progress is drawn as a flowing bar, never as a percentage |
| sparkline on a row | assistant records bucketed into 24 × 15-minute slots at the tail of the session | not drawn when a source yields fewer than 2 buckets |

Token counts are collected but **not displayed as a cross-vendor total**, because
the five sources do not mean the same thing by "tokens": transcripts sum
`input + cache_read + cache_creation`, Codex reports a cumulative total, ZCode
stores `tokens.input`, WorkBuddy's `inputTokens` already includes cache reads,
OpenCode writes a per-turn `step-finish` record whose `input + cache.read + cache.write`
we sum. Adding those together would produce a number that is nobody's measurement.

## Alerts

A session alerts once per `(agent, session)` key. The ledger is on disk, so
restarting does not re-alert the whole board. Per round at most 4 alerts fire; the
rest are held and go out on a later round rather than being dropped.
`免打扰` (DND) suppresses new alerts without clearing the ledger.

`ap_notify_post` returns one of four codes, and the difference matters:

| code | meaning | ledger |
|---|---|---|
| 0 | banner delivered | recorded |
| 1 | banner channel unavailable, fell back to a sound | recorded |
| 2 | neither channel works | recorded, surfaced in settings |
| 3 | platform did not answer within the deadline | **key erased** — this round is not counted, so it can retry |

## Requirements

- C++17, CMake ≥ 3.20, Ninja
- GLFW3, FreeType 2, OpenGL 3.3 core
- optional SQLite3 (without it, ZCode, WorkBuddy and OpenCode are skipped and the build warns)
- macOS 13+ **or** Linux with X11 + `libnotify`

## Build

macOS:

```sh
brew install cmake ninja glfw freetype
cmake -S . -B build -G Ninja && cmake --build build
sh Tools/make_bundle.sh        # -> build/AgentPing-cpp.app
```

Linux (Debian 12 package names, verified by compiling every target):

```sh
sudo apt-get install -y cmake ninja-build libglfw3-dev libfreetype6-dev \
    libsqlite3-dev libgl1-mesa-dev
cmake -S . -B build-linux -G Ninja && cmake --build build-linux
# 提醒层要真行为还需两个包：libnotify-bin（横幅）、sound-theme-freedesktop（出声）
```

## Try it without a display

```sh
./build/pin_imgui_shell --dump            # one scan round as text, no window
D=$(mktemp -d); AGENTPING_STATE_DIR=$D ./build/alert_probe --selftest   # judge + dedup ledger, offline
./build/alert_probe --channel             # is the banner channel alive? 0/1/2
./build/alert_probe --post                # one synthetic alert, end to end
xvfb-run -a -s "-screen 0 1400x1100x24" ./build/pin_imgui_shell --dump
```

`--dump` is the reconciliation tool: every number on screen has a line in it, so a
disagreement between the panel and reality is checkable from the terminal.

## Known limitations

Read this before filing a bug — each item is a known gap with its cause, not a
mystery.

- **macOS banners may be denied.** A build that is not properly signed and
  registered is silently refused by the notification system; the panel then falls
  back to a sound (code 1). Allow it under System Settings → Notifications.
- **No menu-bar item on Linux.** `statusbar_stub.cpp` returns failure; the panel is
  an ordinary window and there is no tray icon.
- **Linux audio fallback is untested.** The verification environment had no sound
  device, so "it actually made a noise on Linux" has not been observed — only that
  the code path returns.
- **Linux banner clicks do not jump.** Clicking a row works; clicking the banner
  does not.
- **`已读` / `稍后` are in-memory.** They do not survive a restart.
- **One screen only.** Detail-narrow, session focus, history and settings are
  designed but not built, so there is no persisted settings UI: sound and DND are
  read from `settings.txt` and set through environment variables.
- **Five CLIs.** Anything else is simply absent from the panel.
- **Zero real Linux desktops tested.** All Linux evidence comes from Xvfb.

## Repository layout

```
src/            panel, scan layer, alert layer, platform shims
src/platform/   notify / open / statusbar, split by OS behind C headers
vendor/imgui/   Dear ImGui 1.93.0 WIP, vendored so the build needs no network
fonts/          OFL-licensed UI fonts, with their license texts
assets/         Info.plist, app icon, menu-bar icon
Tools/          make_bundle.sh, make_menubar_icon.py
CMakeLists.txt  both platforms, including the dependency lists above
```

Fonts are committed rather than downloaded so that a clone builds offline; that is
why the repository is ~40 MB of font binaries.

## License

Apache License 2.0 — see [LICENSE](LICENSE).

Third-party material keeps its own terms: Dear ImGui is MIT
(`vendor/imgui/LICENSE.txt`), and the UI fonts are SIL OFL 1.1
(`fonts/LICENSE-*.txt`). The Noto CJK files carry a Reserved Font Name, `Source`:
modify them freely, but a modified version may not use that name.
