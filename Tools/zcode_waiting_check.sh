#!/usr/bin/env bash
# 量的是「ZCode 侧栏写『等待确认』时我们判成什么」，不是「我们觉得应该在跑」。
# 判据来自真库：AskUserQuestion 挂起时那颗 part 的 state.status 停在 running，
# 而回合的 assistant_response 还没有 time.completed —— 只看回合判活就把它读成「还在跑」，
# 灯舱与横幅于是都不响（2026-11-04 用户实拍：ZCode 写着等待确认，右缘只有 1 条别会话）。
#
# 所以这里造一份真 DDL 形状的 ZCode 库，用真二进制、真采集器（--dump 不开窗）判六格：
#   1 正证 回合未完 + AskUserQuestion 挂着     → awaiting（界面念「等待确认」），带上问句与候选
#   2 反例 回合未完 + Bash 挂着                → working（running 不等于等你，这条不能松）
#   3 反例 回合已完 + 问题已答                 → needs_user 但不带问句块（那是「球回到你这边」另一档）
#   4 反例 问题挂了 25 分钟没人答              → resting，且问句/候选一起撤（过窗作废不许留按钮）
#   5 正证 回合未完 + ExitPlanMode 挂着        → awaiting，工具名要写对（它没有 questions，opt=0）
#   6 反例 回合未完 + 问题已答、Bash 在跑      → working（已答过的问题不许复读成等你）
#
# 附带量 WorkBuddy 的来源标注（它没有 hook，也没有问句原文，「等待确认」是从日志尾部
# 反推的）：那一格必须念「会话日志」而不是「会话库」，且未stalled的同一形状仍判 working。
#   7 正证 工具调用 180s 没回执                → awaiting (推断)，src=log，且没有问题原文
#   8 反例 工具调用 30s 没回执                 → working，不挂 ⟦…⟧（跑着的东西不能算等你）
#
# 用法（仓库根）：bash Tools/zcode_waiting_check.sh
# 退出码：0 全合判据 / 1 有不合（数量见末行）/ 2 根本没跑成（缺二进制、缺 sqlite 路）
set -u

BIN=${BIN:-build/pin_imgui_shell}
PY=${PY:-/usr/bin/python3}
SCRATCH=${SCRATCH:-build/tmp-zcode-wait}

for f in "$BIN" "$PY"; do
    [ -e "$f" ] || { echo "跑不成：缺 $f"; exit 2; }
done
[ -x "$BIN" ] || { echo "跑不成：$BIN 不可执行"; exit 2; }

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/home/.zcode/cli/db" "$SCRATCH/state" "$SCRATCH/hooks"
mkdir -p "$SCRATCH/home/.workbuddy/projects/-fixture-wcase"

# 夹具与断言都写在这一份脚本里：夹具由 python 造，判卷由 bash 读 dump。
"$PY" - "$SCRATCH/home/.zcode/cli/db/db.sqlite" \
          "$SCRATCH/home/.workbuddy/workbuddy.db" \
          "$SCRATCH/home/.workbuddy/projects/-fixture-wcase" <<'PYEOF'
import json, sqlite3, sys, time
path = sys.argv[1]
now = time.time()
db = sqlite3.connect(path)
db.executescript("""
create table session (id text primary key, directory text, title text,
  time_created integer, time_updated integer, task_type text, parent_id text, time_deleted integer);
create table message (id text primary key, session_id text, time_created integer,
  sequence integer, data text);
create table part (id text primary key, message_id text, session_id text,
  time_created integer, time_updated integer, sequence integer, data text);
""")

def turn_open(ago):        # 回合未完：只有 time.created，没有 completed
    return {"kind": "assistant", "created": int((now - ago) * 1000)}
def turn_done(ago):
    return {"kind": "assistant", "created": int((now - ago - 5) * 1000), "completed": int((now - ago) * 1000)}

# 问句与候选是编的夹具，不从任何真会话里抄：形状对齐真库（questions[] + options[]），内容无关。
QS = {"questions": [{"question": "要不要先备份再跑这次迁移？", "header": "迁移", "multiSelect": False,
                     "options": [{"label": "先备份（推荐）", "description": "复制一份数据再执行"},
                                 {"label": "直接跑", "description": "原地执行，跳过备份"},
                                 {"label": "这轮不跑", "description": "先看计划，不动数据"}]}]}

def add(case, tupd_ago, msg_time, part=None):
    sid = "sess_" + case
    db.execute("insert into session values (?,?,?,?,?,?,?,?)",
               (sid, "/fixture/" + case, "标题·" + case, int((now - 3600) * 1000),
                int((now - tupd_ago) * 1000), "interactive", None, None))
    t = {"created": msg_time["created"]}
    if "completed" in msg_time: t["completed"] = msg_time["completed"]
    db.execute("insert into message values (?,?,?,?,?)",
               ("msg_" + case, sid, msg_time["created"], 1,
                json.dumps({"semantics": {"kind": "assistant_response"}, "time": t},
                           ensure_ascii=False)))
    if part:
        pkind, ptool, status, pcreated, payload = part
        db.execute("insert into part values (?,?,?,?,?,?,?)",
                   ("part_" + case, "msg_" + case, sid, int((now - pcreated) * 1000),
                    int((now - pcreated) * 1000), 1,
                    json.dumps({"type": "tool", "callID": "call_" + case, "tool": ptool,
                                "state": {"status": status, "input": payload, "time": {}}},
                               ensure_ascii=False)))

add("zcase1", 40, turn_open(60), ("run", "AskUserQuestion", "running", 40, QS))
add("zcase2", 40, turn_open(60), ("run", "Bash", "running", 40, {"command": "sleep 90"}))
add("zcase3", 30, turn_done(30), ("done", "AskUserQuestion", "completed", 120, QS))
add("zcase4", 1500, turn_open(1520), ("run", "AskUserQuestion", "running", 1500, QS))
add("zcase5", 55, turn_open(70), ("run", "ExitPlanMode", "running", 55, {"plan": "第一步…"}))
# 已答过的问题（completed）+ 一颗正在跑的 Bash：问题这一格不该再算「等你」
add("zcase6", 20, turn_open(90), ("done", "AskUserQuestion", "completed", 90, QS))
db.execute("insert into part values (?,?,?,?,?,?,?)",
           ("part_zcase6b", "msg_zcase6", "sess_zcase6", int((now - 20) * 1000),
            int((now - 20) * 1000), 2,
            json.dumps({"type": "tool", "callID": "call_zcase6b", "tool": "Bash",
                        "state": {"status": "running", "input": {"command": "npm run build"}, "time": {}}},
                       ensure_ascii=False)))
db.commit()

# --- WorkBuddy：会话行来自它的库，「等待确认」只可能从日志尾部反推 ---
# 没有 hook、也没有问句原文，所以这一档的来源必须念「会话日志」：念成「会话库」就是
# 把一次推断报成一次读取。
wb, wb_dir = sqlite3.connect(sys.argv[2]), sys.argv[3]
wb.executescript("""
create table sessions (id text primary key, custom_title text, title text, cwd text,
  status text, last_activity_at integer, updated_at integer, deleted_at integer);
""")
def wcase(name, ago):
    ms = int((now - ago) * 1000)
    wb.execute("insert into sessions values (?,?,?,?,?,?,?,?)",
               (name, None, "标题·" + name, "/fixture/" + name, "active", ms, ms, None))
    with open(wb_dir + "/" + name + ".jsonl", "w", encoding="utf-8") as f:
        f.write(json.dumps({"type": "message", "role": "assistant", "status": "completed",
                            "timestamp": now - ago - 60}, ensure_ascii=False) + "\n")
        f.write(json.dumps({"type": "function_call", "callId": "call_" + name,
                            "name": "Bash", "timestamp": now - ago}, ensure_ascii=False) + "\n")
wcase("wcase1", 180)   # 工具发出去三分钟没回执 → 疑似等你（不许响铃，但要标来源）
wcase("wcase2", 30)    # 三十秒没回执 → 还在跑
wb.commit()
print("[夹具] %s｜now=%.0f" % (path, now))
PYEOF
FIX_RC=$?
# 夹具自己没造出来 = 根本没跑成（2），不许把它表现成「六格全不符」（1）——
# 判卷机和被判的卷子坏了，得分含义相反。
[ $FIX_RC -eq 0 ] || { echo "跑不成：夹具脚本退出 $FIX_RC"; exit 2; }
[ -s "$SCRATCH/home/.zcode/cli/db/db.sqlite" ] || { echo "跑不成：夹具库没建出来"; exit 2; }
[ -s "$SCRATCH/home/.workbuddy/workbuddy.db" ] || { echo "跑不成：WorkBuddy 夹具库没建出来"; exit 2; }

DUMP=$(HOME="$PWD/$SCRATCH/home" AGENTPING_STATE_DIR="$PWD/$SCRATCH/state" \
           AGENTPING_HOOK_DIR="$PWD/$SCRATCH/hooks" "$BIN" --dump 2>&1)
RC=$?
[ $RC -eq 0 ] || { echo "跑不成：--dump 退出 $RC"; printf '%s\n' "$DUMP" | tail -5; exit 2; }
printf '%s\n' "$DUMP" | grep -q '^\[zcode\]' || { echo "跑不成：dump 里没有 zcode 这一路（这份构建没链 sqlite3？）"; exit 2; }
printf '%s\n' "$DUMP" | grep '^\[zcode\]'
printf '%s\n' "$DUMP" | grep -q 'available=1' || { echo "跑不成：zcode 这一路 available=0，夹具没被读到"; exit 2; }
printf '%s\n' "$DUMP" | grep -q '^\[zcode\].*sessions=0' && { echo "跑不成：采集器读到 0 条会话，夹具没进判据"; exit 2; }
printf '%s\n' "$DUMP" | grep '^\[workbuddy\]'
printf '%s\n' "$DUMP" | grep -q '^\[workbuddy\].*available=1' || { echo "跑不成：workbuddy 这一路 available=0，夹具没被读到"; exit 2; }

fail=0
checks=0
line() { printf '%s\n' "$DUMP" | grep -- "$1" | head -1; }
chk() {  # chk <格> <这条要同时成立的若干子串>
    local case="$1"; shift
    checks=$((checks+1))
    local L; L=$(line "$case")
    [ -n "$L" ] || { echo "[不符] $case 这一格在 dump 里没有行"; fail=$((fail+1)); return; }
    local miss=""
    for want in "$@"; do
        case "$L" in *"$want"*) ;; *) miss="$miss $want";; esac
    done
    if [ -n "$miss" ]; then echo "[不符] $case 缺：$miss"; echo "        实得：$L"; fail=$((fail+1));
    else echo "[通过] $case → $L"; fi
}
nock() {  # nock <格> <这行里不许出现的子串>：残留的问句/候选会在「静置」行底下摆出可点的按钮
    checks=$((checks+1))
    local L; L=$(line "$1")
    if printf '%s\n' "$L" | grep -q -- "$2"; then
        echo "[不符] $1 不该带「$2」"; echo "        实得：$L"; fail=$((fail+1))
    else echo "[通过] $1 不含「$2」"; fi
}

chk zcase1 awaiting "⟵要不要先备份再跑这次迁移？" "⟦tool=AskUserQuestion opt=3"
nock zcase2 "⟦"
chk zcase3 needs_user
nock zcase3 "⟵"
chk zcase4 resting
nock zcase4 "⟵"
nock zcase4 "⟦"
chk zcase5 awaiting "⟦tool=ExitPlanMode opt=0"
nock zcase6 "⟦"
chk zcase6 working

chk wcase1 awaiting "(推断)" "src=log"
nock wcase1 "⟵"
chk wcase2 working
nock wcase2 "⟦"

if [ $fail -eq 0 ]; then echo "合判据 $checks/$checks 断言（八格夹具）"; exit 0; fi
echo "不合判据 $fail/$checks 项"; exit 1
