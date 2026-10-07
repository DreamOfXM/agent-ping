#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""造一批假 Claude Code 会话，让状态项那三个数在容器里也能非零。

为什么要有这个文件：容器里没有真 agent 在跑，三档计数天然全 0，而标签构造器
（pin_imgui_shell.cpp:2023）故意跳过 0 的那一档——于是宿主读回 Label 永远是空串，
「三个数到底有没有上总线」这条就无法证伪。没有可核对的期望值，取证就只是看了眼绿的日志。

期望值钉死为 "3 · 7 · 1"（介入 3 / 运行 7（含排队 2）/ 失败 1），
判定口径抄 scan.cpp:263 derive()，不自己发明：
  · ST_AWAIT   = 最后一条有声记录 stop_reason=="tool_use" 且已卡 ≥90s（kStalledToolUse）
  · ST_WORKING = 最后一条有声记录 type=="user"
  · ST_QUEUED  = queue-operation enqueue 净数 > 0
  · ST_FAILED  = isApiErrorMessage 为真
  · 任何一档 age > 1200s（kNeedsUserStale）都被改写成 ST_RESTING，所以时间戳一律取「刚刚」

红线：只写 --home 指的那个目录，且必须显式给；落到真 HOME 直接拒绝。
"""
import argparse
import json
import os
import time

# (档, 条数) —— 改这张表就等于改期望标签，脚本末尾会把它重算成字符串打出来
PLAN = [
    ("await", 3),
    ("working", 5),
    ("queued", 2),
    ("failed", 1),
    ("resting", 2),   # 静默的：证明 0 档不会被误算进来
]
TITLES = {
    "await": "等你允许执行命令",
    "working": "正在跑：逐屏核对裁切",
    "queued": "排队中：等上一个会话让位",
    "failed": "失败中断：上游 500",
    "resting": "已完成：改了 3 个文件",
}
STALE = 20 * 60          # kNeedsUserStale
STALL = 90               # kStalledToolUse


def records(kind, now, idx):
    """一条会话的 JSONL 记录序列，按文件顺序（derive 取最后一条有声记录）。"""
    cid = "seed-%s-%02d" % (kind, idx)
    if kind == "await":
        ts = now - (STALL + 60)            # 卡 150s：过了 90s 门槛，还在 1200s 以内
        tail = [asst(ts, "tool_use")]
    elif kind == "working":
        ts = now - 20
        tail = [user(ts)]
    elif kind == "queued":
        ts = now - 25
        tail = [queue_op(ts - 8, "enqueue"), user(ts)]
    elif kind == "failed":
        ts = now - 35
        tail = [failed(ts)]
    else:                                   # resting：25 分钟前，会被 derive 判静默
        ts = now - (STALE + 300)
        tail = [asst(ts, "end_turn")]
    head = [user(now - 900, cid)]
    return cid, head + tail


def user(ts, cid=None):
    r = {"type": "user", "timestamp": ts,
         "message": {"role": "user", "content": "继续"}}
    if cid:
        r["cwd"] = "/seed/checkout"
        r["sessionId"] = cid
    return r


def asst(ts, stop):
    return {"type": "assistant", "timestamp": ts,
            "message": {"role": "assistant", "stop_reason": stop,
                        "usage": {"input_tokens": 1200 + 77 * len(stop),
                                  "cache_read_input_tokens": 40,
                                  "output_tokens": 300}}}


def queue_op(ts, op):
    return {"type": "queue-operation", "timestamp": ts, "operation": op}


def failed(ts):
    return {"type": "assistant", "timestamp": ts, "isApiErrorMessage": True,
            "error": {"type": "api_error", "message": "上游返回 500，本轮中断"},
            "message": {"role": "assistant", "stop_reason": "end_turn"}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--home", required=True, help="要写入的假 HOME（必须是空目录或专用目录）")
    a = ap.parse_args()

    real = os.path.expanduser("~")
    seed = os.path.abspath(a.home)
    if seed == os.path.abspath(real):
        raise SystemExit("拒绝：--home 解析到了真 HOME %s" % real)
    root = os.path.join(seed, ".claude", "projects", "-seed-checkout")
    os.makedirs(root, exist_ok=True)

    now = time.time()
    made = 0
    counts = {}
    for kind, n in PLAN:
        counts[kind] = n
        for i in range(1, n + 1):
            cid, recs = records(kind, now, i)
            with open(os.path.join(root, cid + ".jsonl"), "w") as f:
                for r in recs:
                    f.write(json.dumps(r, ensure_ascii=False) + "\n")
            made += 1

    # 期望标签：按 derive→GroupOf→标签构造器同一条链重算，不手抄字符串
    await_n = counts["await"]
    run_n = counts["working"] + counts["queued"]      # 2008：排队并进「运行」
    fail_n = counts["failed"]
    parts = [str(x) for x in (await_n, run_n, fail_n) if x]
    print("目录=%s 会话=%d" % (root, made))
    print("期望 Label=\"%s\"（介入 %d · 运行 %d（含排队 %d）· 失败 %d）"
          % (" · ".join(parts), await_n, run_n, counts["queued"], fail_n))


if __name__ == "__main__":
    main()
